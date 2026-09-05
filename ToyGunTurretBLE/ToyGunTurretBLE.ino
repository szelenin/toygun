/*
 * Toy Gun Turret - motion and firing, driven from a Flipper Zero over BLE
 *
 * Runs on the ESP32-WROOM-32E, which owns the servos and the firing relays.
 * The ESP32-CAM alongside it keeps running ToyGunCamWiFi.ino for the camera
 * and its web UI - the two boards share only the 5V rail and a common ground,
 * never a signal line.
 *
 * ---------------------------------------------------------------------------
 * Who connects to whom, and why it is this way round
 * ---------------------------------------------------------------------------
 * The Flipper cannot be a BLE central. Its official firmware exports no
 * scanning, no connecting, no GATT client - checked against api_symbols.csv,
 * there is not one such symbol. It can only advertise and be connected to.
 *
 * So the WROOM is the central. It scans for the Flipper, connects to it, and
 * talks over the Flipper's own Serial service, which IS exported to Flipper
 * apps as a plain byte pipe (ble_profile_serial_tx / set_event_callback).
 *
 *      WROOM (central, GATT client)  ---- connects to ---->  Flipper (peripheral)
 *      subscribes to Flipper TX char  <--- commands ------
 *      writes to Flipper RX char      ---- telemetry ---->
 *
 * The Flipper app must call ble_profile_serial_set_rpc_active(profile, false)
 * or the RPC layer consumes the bytes before they reach us.
 *
 * ---------------------------------------------------------------------------
 * Protocol
 * ---------------------------------------------------------------------------
 * See PROTOCOL.md - that file is the contract, this is one implementation.
 *
 *   U1 U0 D1 D0 L1 L0 R1 R0   press / release a direction
 *   F1 F0                     start / stop firing
 *   C                         center servos, clear the saved position
 *   P                         request a position report
 *   B<0-255>                  PWM duty on the spare GPIO 4 output
 *   K                         keepalive - refreshes the watchdog
 *   V                         handshake, replies VER <protocol> <name>
 *
 * Replies: POS <h> <v> | FIRE <0|1> | LED <n> | VER <n> <name> | ERR <c>
 *
 * ---------------------------------------------------------------------------
 * Safety: the deadman watchdog
 * ---------------------------------------------------------------------------
 * This launches projectiles and BLE links drop without warning, so:
 *   - Losing the Flipper stops firing and all movement immediately.
 *   - Movement and firing auto-cancel after ACTIVE_TIMEOUT_MS with no traffic.
 * The app must resend the held command, or a bare K, every ~500ms.
 *
 * ---------------------------------------------------------------------------
 * Build
 * ---------------------------------------------------------------------------
 *   Library: "NimBLE-Arduino" by h2zero, version 2.x  (Library Manager)
 *   Board:   ESP32 Dev Module
 *   Core:    ESP32 Arduino core 3.x (uses the pin-based ledcAttach API)
 *
 * Pins are the same numbers ToyGunCamWiFi.ino used, so the existing 10-pin
 * perfboard header plugs straight into the WROOM with no rewiring:
 *   GPIO 12 horizontal servo signal   GPIO 14 trigger relay  (active LOW)
 *   GPIO 13 vertical servo signal     GPIO 15 spinner relay  (active LOW)
 *   GPIO  4 spare PWM output
 *   Servos powered from the 6V buck, NEVER from the ESP32.
 *
 * Move that header to the WROOM completely. If both boards stay wired to the
 * same servo signal line, two push-pull outputs fight over it and you lose
 * GPIO pins on one or both.
 */

#include <NimBLEDevice.h>
#include <ESP32Servo.h>
#include <Preferences.h>

// ===========================
// Hardware pins
// ===========================
#define SERVO_PIN_HORIZONTAL 12
#define SERVO_PIN_VERTICAL   13
#define RELAY_PIN_TRIGGER    14
#define RELAY_PIN_SPINNER    15
#define LED_PIN               4   // spare PWM output
#define LED_PWM_FREQ       5000
#define LED_PWM_RESOLUTION    8

// ===========================
// Identity and the Flipper's Serial service
// ===========================
#define PROTOCOL_VERSION 1
#define TURRET_NAME     "LizardGun3000"

// Advertised-name prefix used to spot the Flipper while scanning. Every Flipper
// is named "Flipper <something>"; set this to the full name to pin one device.
#define FLIPPER_NAME_PREFIX "Flipper"

// 1 = log every advertisement seen while scanning. Useful once, deafening after.
#define VERBOSE_SCAN 0

// From flipperzero-firmware targets/f7/ble_glue/services/serial_service_uuid.inc.
// The firmware stores 128-bit UUIDs least-significant byte first; these are the
// same values byte-reversed into normal UUID order.
#define FLIPPER_SERIAL_SERVICE "8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000"
#define FLIPPER_CHAR_TX        "19ed82ae-ed21-4c9d-4145-228e61fe0000"  // Flipper -> us, notify
#define FLIPPER_CHAR_RX        "19ed82ae-ed21-4c9d-4145-228e62fe0000"  // us -> Flipper, write
#define FLIPPER_CHAR_FLOWCTRL  "19ed82ae-ed21-4c9d-4145-228e63fe0000"
#define FLIPPER_CHAR_RPCSTATUS "19ed82ae-ed21-4c9d-4145-228e64fe0000"

// ===========================
// Movement envelope - matches the camera firmware
// ===========================
const int HORIZONTAL_CENTER = 90;
const int VERTICAL_CENTER   = 90;
const int HORIZONTAL_MIN    = 0;
const int HORIZONTAL_MAX    = 180;
const int VERTICAL_MIN      = 75;
const int VERTICAL_MAX      = 100;

const int MOVE_DELAY = 40;          // ms per step -> 25 deg/sec
const int MOVE_STEP  = 1;

const unsigned long SPINUP_MS         = 250;   // flywheels before the pusher
const unsigned long ACTIVE_TIMEOUT_MS = 1500;  // deadman watchdog
const unsigned long SAVE_DELAY        = 3000;  // flash write debounce
const unsigned long NOTIFY_INTERVAL   = 200;   // position report rate

// ===========================
// State
// ===========================
Servo horizontalServo;
Servo verticalServo;
Preferences preferences;

int horizontalAngle = HORIZONTAL_CENTER;
int verticalAngle   = VERTICAL_CENTER;

bool movingUp = false, movingDown = false, movingLeft = false, movingRight = false;
bool spinnerActive = false, triggerActive = false;
int  ledBrightness = 0;

unsigned long lastMoveTime     = 0;
unsigned long lastCommandTime  = 0;
unsigned long lastActivityTime = 0;
unsigned long lastReportTime   = 0;
int  lastReportedH   = -1;      // suppress repeats of an unchanged position
int  lastReportedV   = -1;
bool positionChanged = false;
bool positionsSaved  = true;

// BLE client state
NimBLEClient              *client   = nullptr;
NimBLERemoteCharacteristic *rxChar  = nullptr;   // we write here
NimBLERemoteCharacteristic *txChar  = nullptr;   // we subscribe here
const NimBLEAdvertisedDevice *foundFlipper = nullptr;
volatile bool shouldConnect = false;
volatile bool linkUp        = false;

// ===========================
// Position persistence
// ===========================
void loadPositions() {
  preferences.begin("turret", true);
  horizontalAngle = preferences.getInt("hAngle", HORIZONTAL_CENTER);
  verticalAngle   = preferences.getInt("vAngle", VERTICAL_CENTER);
  preferences.end();

  horizontalAngle = constrain(horizontalAngle, HORIZONTAL_MIN, HORIZONTAL_MAX);
  verticalAngle   = constrain(verticalAngle, VERTICAL_MIN, VERTICAL_MAX);
  Serial.printf("Loaded positions - H: %d deg  V: %d deg\n", horizontalAngle, verticalAngle);
}

void savePositions() {
  preferences.begin("turret", false);
  preferences.putInt("hAngle", horizontalAngle);
  preferences.putInt("vAngle", verticalAngle);
  preferences.end();
  positionsSaved = true;
  Serial.printf("Saved positions - H: %d deg  V: %d deg\n", horizontalAngle, verticalAngle);
}

// ===========================
// Telemetry back to the Flipper
// ===========================
void report(const char *fmt, ...) {
  char buf[64];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  Serial.printf("-> %s\n", buf);          // always visible on the serial console
  if (rxChar && linkUp) {
    rxChar->writeValue((uint8_t *)buf, strlen(buf), false);   // no response
  }
}

void reportPosition() {
  lastReportedH = horizontalAngle;
  lastReportedV = verticalAngle;
  report("POS %d %d", horizontalAngle, verticalAngle);
}

// ===========================
// Firing
// ===========================
void startFiring() {
  if (triggerActive) return;
  digitalWrite(RELAY_PIN_SPINNER, LOW);   // active LOW = on
  spinnerActive = true;
  delay(SPINUP_MS);                       // flywheels need to reach speed
  digitalWrite(RELAY_PIN_TRIGGER, LOW);
  triggerActive = true;
  Serial.println("FIRING");
  report("FIRE 1");
}

void stopFiring() {
  if (!triggerActive && !spinnerActive) return;
  digitalWrite(RELAY_PIN_TRIGGER, HIGH);  // HIGH = off
  digitalWrite(RELAY_PIN_SPINNER, HIGH);
  triggerActive = false;
  spinnerActive = false;
  Serial.println("STOPPED");
  report("FIRE 0");
}

void stopAll() {
  movingUp = movingDown = movingLeft = movingRight = false;
  stopFiring();
}

void centerServos() {
  preferences.begin("turret", false);
  preferences.clear();
  preferences.end();
  horizontalAngle = HORIZONTAL_CENTER;
  verticalAngle   = VERTICAL_CENTER;
  horizontalServo.write(horizontalAngle);
  verticalServo.write(verticalAngle);
  positionsSaved  = true;
  positionChanged = false;
  Serial.println("Centered");
  reportPosition();
}

// ===========================
// Command parser - identical whatever the transport
// ===========================
void handleCommand(const char *cmd, size_t len) {
  if (len == 0) return;
  lastCommandTime = millis();

  bool on = (len >= 2 && cmd[1] == '1');

  switch (cmd[0]) {
    case 'U': movingUp    = on; break;
    case 'D': movingDown  = on; break;
    case 'L': movingLeft  = on; break;
    case 'R': movingRight = on; break;

    case 'F':
      if (on) startFiring();
      else    stopFiring();
      break;

    case 'C': centerServos(); break;
    case 'P': reportPosition(); break;
    case 'K': break;                      // keepalive only

    // Handshake. The Flipper app should send V on connect and refuse to drive
    // a turret whose protocol version it does not know.
    case 'V':
      report("VER %d %s", PROTOCOL_VERSION, TURRET_NAME);
      break;

    case 'B': {
      int v = (len > 1) ? atoi(cmd + 1) : 0;
      ledBrightness = constrain(v, 0, 255);
      ledcWrite(LED_PIN, ledBrightness);
      report("LED %d", ledBrightness);
      break;
    }

    default:
      report("ERR %c", cmd[0]);
      return;
  }
}

// Split an incoming chunk into commands. Tolerates clients that append CR/LF or
// batch several commands into one packet.
void handleIncoming(const uint8_t *data, size_t len) {
  char buf[64];
  size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, data, n);
  buf[n] = '\0';
  Serial.printf("<- %s\n", buf);

  char *save = nullptr;
  for (char *tok = strtok_r(buf, " \r\n", &save); tok; tok = strtok_r(nullptr, " \r\n", &save)) {
    handleCommand(tok, strlen(tok));
  }
}

// ===========================
// BLE callbacks (NimBLE-Arduino 2.x signatures)
// ===========================
void onNotify(NimBLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool isNotify) {
  handleIncoming(data, len);
}

class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *c) override {
    Serial.println("Connected, requesting a fast connection interval");
    // 7.5-15ms: this is a twitch control link, latency is the whole point
    c->updateConnParams(6, 12, 0, 200);
  }

  void onDisconnect(NimBLEClient *c, int reason) override {
    linkUp = false;
    rxChar = nullptr;
    txChar = nullptr;
    stopAll();                            // never keep firing into a dead link
    Serial.printf("Disconnected (reason %d) - all outputs off, rescanning\n", reason);
    NimBLEDevice::getScan()->start(0, false, true);
  }

  // The Flipper shows a six-digit code when pairing. Type it into the serial
  // monitor once; the bond is stored in NVS and later connections are silent.
  void onPassKeyEntry(NimBLEConnInfo &connInfo) override {
    Serial.println("\n*** Flipper is showing a pairing code.");
    Serial.println("*** Type the 6 digits here and press Enter:");
    uint32_t pin = 0;
    unsigned long deadline = millis() + 60000;
    String entry;
    while (millis() < deadline) {
      if (Serial.available()) {
        char ch = Serial.read();
        if (ch == '\n' || ch == '\r') { if (entry.length()) break; }
        else if (isDigit(ch))         { entry += ch; }
      }
      delay(10);
    }
    pin = entry.toInt();
    Serial.printf("Injecting passkey %06lu\n", (unsigned long)pin);
    NimBLEDevice::injectPassKey(connInfo, pin);
  }

  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override {
    Serial.printf("Pairing %s\n", connInfo.isEncrypted() ? "OK, link encrypted" : "FAILED");
  }
};

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    bool byService = dev->isAdvertisingService(NimBLEUUID(FLIPPER_SERIAL_SERVICE));
    bool byName    = dev->haveName() &&
                     strncmp(dev->getName().c_str(), FLIPPER_NAME_PREFIX,
                             strlen(FLIPPER_NAME_PREFIX)) == 0;

    if (VERBOSE_SCAN || byService || byName) {
      Serial.printf("  saw %-24s rssi %d%s\n",
                    dev->haveName() ? dev->getName().c_str() : "(no name)",
                    dev->getRSSI(), (byService || byName) ? "   <= FLIPPER" : "");
    }

    if (byService || byName) {
      NimBLEDevice::getScan()->stop();
      foundFlipper  = dev;
      shouldConnect = true;               // connect from loop(), not from here
    }
  }
};

// ===========================
// Connect and wire up the characteristics
// ===========================
bool connectToFlipper() {
  if (!foundFlipper) return false;
  Serial.printf("Connecting to %s...\n", foundFlipper->getAddress().toString().c_str());

  if (!client) {
    client = NimBLEDevice::createClient();
    client->setClientCallbacks(new ClientCallbacks(), false);
    client->setConnectTimeout(10 * 1000);
  }

  if (!client->connect(foundFlipper)) {
    Serial.println("Connect failed");
    return false;
  }

  NimBLERemoteService *svc = client->getService(FLIPPER_SERIAL_SERVICE);
  if (!svc) {
    Serial.println("No Flipper Serial service - is Bluetooth on, and the app running?");
    client->disconnect();
    return false;
  }

  txChar = svc->getCharacteristic(FLIPPER_CHAR_TX);
  rxChar = svc->getCharacteristic(FLIPPER_CHAR_RX);
  if (!txChar || !rxChar) {
    Serial.println("Serial service found but TX/RX characteristics missing");
    client->disconnect();
    return false;
  }

  if (!txChar->canNotify() || !txChar->subscribe(true, onNotify)) {
    Serial.println("Could not subscribe to the Flipper TX characteristic");
    client->disconnect();
    return false;
  }

  linkUp          = true;
  lastCommandTime = millis();
  Serial.println("Link up. Waiting for commands.");
  report("VER %d %s", PROTOCOL_VERSION, TURRET_NAME);
  return true;
}

// ===========================
// Setup
// ===========================
void setup() {
  // Relays first, before anything else can take time. These are active LOW,
  // so an undriven pin during boot can twitch the trigger.
  pinMode(RELAY_PIN_TRIGGER, OUTPUT);
  pinMode(RELAY_PIN_SPINNER, OUTPUT);
  digitalWrite(RELAY_PIN_TRIGGER, HIGH);
  digitalWrite(RELAY_PIN_SPINNER, HIGH);

  Serial.begin(115200);
  Serial.println("\n\nToy Gun Turret - BLE central, talking to a Flipper Zero");

  loadPositions();

  horizontalServo.attach(SERVO_PIN_HORIZONTAL);
  verticalServo.attach(SERVO_PIN_VERTICAL);
  horizontalServo.write(horizontalAngle);
  verticalServo.write(verticalAngle);

  ledcAttach(LED_PIN, LED_PWM_FREQ, LED_PWM_RESOLUTION);
  ledcWrite(LED_PIN, 0);

  NimBLEDevice::init(TURRET_NAME);
  // Bond so the pairing code is only needed once. No MITM is tried first;
  // if the Flipper insists on a passkey, onPassKeyEntry above handles it.
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(new ScanCallbacks(), false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(80);

  Serial.println("Scanning for a Flipper...");
  scan->start(0, false, true);   // 0 = scan until told otherwise
}

// ===========================
// Loop
// ===========================
void loop() {
  unsigned long now = millis();

  // Connect outside the scan callback - connecting from inside it is unsafe
  if (shouldConnect) {
    shouldConnect = false;
    if (!connectToFlipper()) {
      Serial.println("Retrying scan...");
      NimBLEDevice::getScan()->start(0, false, true);
    }
  }

  // Deadman watchdog: a silent link means the operator is not in control
  bool active = movingUp || movingDown || movingLeft || movingRight || triggerActive;
  if (active && (now - lastCommandTime >= ACTIVE_TIMEOUT_MS)) {
    Serial.println("Watchdog: no traffic, stopping everything");
    stopAll();
    report("OK watchdog");
  }

  if (now - lastMoveTime >= MOVE_DELAY) {
    lastMoveTime = now;
    bool moved = false;

    // Vertical is reversed: up decreases the angle
    if (movingUp && verticalAngle > VERTICAL_MIN) {
      verticalServo.write(--verticalAngle);
      moved = true;
    }
    if (movingDown && verticalAngle < VERTICAL_MAX) {
      verticalServo.write(++verticalAngle);
      moved = true;
    }
    if (movingLeft && horizontalAngle > HORIZONTAL_MIN) {
      horizontalServo.write(--horizontalAngle);
      moved = true;
    }
    if (movingRight && horizontalAngle < HORIZONTAL_MAX) {
      horizontalServo.write(++horizontalAngle);
      moved = true;
    }

    if (moved) {
      lastActivityTime = now;
      positionChanged  = true;
      positionsSaved   = false;
    }
  }

  bool moved_since_report = (horizontalAngle != lastReportedH) || (verticalAngle != lastReportedV);
  if (moved_since_report && (now - lastReportTime >= NOTIFY_INTERVAL)) {
    lastReportTime = now;
    reportPosition();
  }

  if (positionChanged && !positionsSaved && (now - lastActivityTime >= SAVE_DELAY)) {
    savePositions();
    positionChanged = false;
  }

  // Bench testing without a Flipper: type commands into the serial monitor
  static char line[32];
  static size_t lineLen = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (lineLen) { handleIncoming((uint8_t *)line, lineLen); lineLen = 0; }
    } else if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = ch;
    }
  }

  delay(1);   // let the NimBLE host task run
}
