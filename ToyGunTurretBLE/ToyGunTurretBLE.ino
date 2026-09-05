/*
 * Toy Gun Turret - motion and firing control over NimBLE
 *
 * Runs on the ESP32-WROOM-32E, which owns the servos and the firing relays.
 * The ESP32-CAM alongside it keeps running ToyGunCamWiFi.ino for the camera
 * and its web UI - the two boards share only the 5V rail and a common ground,
 * never a signal line.
 *
 * Why NimBLE and not the stock BLEDevice.h: Bluedroid costs ~100KB of heap,
 * NimBLE costs ~35KB, and nothing here needs the difference.
 *
 * ---------------------------------------------------------------------------
 * GATT interface
 * ---------------------------------------------------------------------------
 * Advertised name:  LizardGun3000
 * Service:          6e400001-b5a3-f393-e0a9-e50e24dcca9e  (Nordic UART)
 *   RX (write/wnr): 6e400002-b5a3-f393-e0a9-e50e24dcca9e  <- commands to turret
 *   TX (notify):    6e400003-b5a3-f393-e0a9-e50e24dcca9e  <- telemetry out
 *
 * Nordic UART was chosen deliberately: every generic BLE client already knows
 * it (nRF Connect, LightBlue, the Flipper BLE serial apps), so the turret is
 * testable from a phone before any Flipper code exists.
 *
 * Commands are plain ASCII, one per write, no terminator required:
 *   U1 U0 D1 D0 L1 L0 R1 R0   press / release a direction
 *   F1 F0                     start / stop firing
 *   C                         center servos, clear saved position
 *   P                         request position notify
 *   B<0-255>                  brightness on the spare GPIO 4 PWM output
 *   K                         keepalive (refreshes the watchdog, changes nothing)
 *   V                         handshake: replies VER <protocol> <name>
 *
 * Notifications sent back:
 *   POS <h> <v>               after every movement tick and on demand
 *   FIRE <0|1>
 *   LED <0-255>
 *   OK / ERR <cmd>
 *
 * ---------------------------------------------------------------------------
 * Safety: the deadman watchdog
 * ---------------------------------------------------------------------------
 * This thing launches projectiles, and BLE links drop without warning. So:
 *   - Any BLE disconnect stops firing and all movement immediately.
 *   - Movement and firing auto-cancel after ACTIVE_TIMEOUT_MS with no traffic.
 * A client holding a button must resend the command (or K) every ~500ms.
 * Fire-and-forget "F1" alone will stop on its own after 1.5 seconds.
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
 *   GPIO  4 spare PWM output (the camera's own flash LED belongs to the CAM)
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
// Hardware pins - match ToyGunCamWiFi.ino exactly
// ===========================
#define SERVO_PIN_HORIZONTAL 12
#define SERVO_PIN_VERTICAL   13
#define RELAY_PIN_TRIGGER    14
#define RELAY_PIN_SPINNER    15
#define LED_PIN               4   // spare PWM output
#define LED_PWM_FREQ       5000
#define LED_PWM_RESOLUTION    8

// ===========================
// BLE identity
// ===========================
#define PROTOCOL_VERSION 1
#define DEVICE_NAME    "LizardGun3000"
#define SERVICE_UUID   "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHAR_RX_UUID   "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHAR_TX_UUID   "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// ===========================
// Movement envelope - matches the WiFi firmware
// ===========================
const int HORIZONTAL_CENTER = 90;
const int VERTICAL_CENTER   = 90;
const int HORIZONTAL_MIN    = 0;
const int HORIZONTAL_MAX    = 180;
const int VERTICAL_MIN      = 75;   // vertical travel is deliberately narrow
const int VERTICAL_MAX      = 100;

const int MOVE_DELAY = 40;          // ms per step -> 25 deg/sec
const int MOVE_STEP  = 1;

const unsigned long SPINUP_MS         = 250;   // flywheels before the pusher
const unsigned long ACTIVE_TIMEOUT_MS = 1500;  // deadman watchdog
const unsigned long SAVE_DELAY        = 3000;  // flash write debounce
const unsigned long NOTIFY_INTERVAL   = 200;   // position notify rate

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
unsigned long lastNotifyTime   = 0;
bool positionChanged = false;
bool positionsSaved  = true;

NimBLECharacteristic *txChar = nullptr;
volatile bool clientConnected = false;

// ===========================
// Position persistence
// ===========================
void loadPositions() {
  preferences.begin("turret", true);
  horizontalAngle = preferences.getInt("hAngle", HORIZONTAL_CENTER);
  verticalAngle   = preferences.getInt("vAngle", VERTICAL_CENTER);
  preferences.end();

  // Clamp in case the envelope changed since the values were written
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
// Telemetry
// ===========================
void notify(const char *fmt, ...) {
  if (!txChar || !clientConnected) return;
  char buf[64];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  txChar->setValue((uint8_t *)buf, strlen(buf));
  txChar->notify();
}

void notifyPosition() {
  notify("POS %d %d", horizontalAngle, verticalAngle);
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
  notify("FIRE 1");
}

void stopFiring() {
  if (!triggerActive && !spinnerActive) return;
  digitalWrite(RELAY_PIN_TRIGGER, HIGH);  // HIGH = off
  digitalWrite(RELAY_PIN_SPINNER, HIGH);
  triggerActive = false;
  spinnerActive = false;
  Serial.println("STOPPED");
  notify("FIRE 0");
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
  notifyPosition();
}

// ===========================
// Command parser
// ===========================
void handleCommand(const char *cmd, size_t len) {
  if (len == 0) return;
  lastCommandTime = millis();

  // Second byte is the press/release flag for the two-character commands
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
    case 'P': notifyPosition(); break;
    case 'K': break;                      // keepalive only

    // Handshake. The Flipper app should send V on connect and refuse to drive
    // a turret whose protocol version it does not know.
    case 'V':
      notify("VER %d %s", PROTOCOL_VERSION, DEVICE_NAME);
      break;

    case 'B': {
      int v = (len > 1) ? atoi(cmd + 1) : 0;
      ledBrightness = constrain(v, 0, 255);
      ledcWrite(LED_PIN, ledBrightness);
      notify("LED %d", ledBrightness);
      break;
    }

    default:
      notify("ERR %c", cmd[0]);
      return;
  }
}

// ===========================
// BLE callbacks (NimBLE-Arduino 2.x signatures)
// ===========================
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server, NimBLEConnInfo &info) override {
    clientConnected = true;
    lastCommandTime = millis();
    Serial.printf("BLE connected: %s\n", info.getAddress().toString().c_str());
    // Ask for a fast connection interval - this is a twitch control link,
    // 7.5-15ms keeps button response tight.
    server->updateConnParams(info.getConnHandle(), 6, 12, 0, 200);
  }

  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &info, int reason) override {
    clientConnected = false;
    stopAll();                              // never keep firing into a dead link
    Serial.printf("BLE disconnected (reason %d) - all outputs off\n", reason);
    NimBLEDevice::startAdvertising();
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
    NimBLEAttValue value = chr->getValue();
    if (value.length() == 0) return;

    // Tolerate clients that append CR/LF or batch several commands per write
    char buf[32];
    size_t n = (size_t)value.length() < sizeof(buf) - 1 ? (size_t)value.length() : sizeof(buf) - 1;
    memcpy(buf, value.data(), n);
    buf[n] = '\0';

    char *save = nullptr;
    for (char *tok = strtok_r(buf, " \r\n", &save); tok; tok = strtok_r(nullptr, " \r\n", &save)) {
      handleCommand(tok, strlen(tok));
    }
  }
};

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
  Serial.println("\n\nToy Gun Turret - NimBLE control");

  loadPositions();

  horizontalServo.attach(SERVO_PIN_HORIZONTAL);
  verticalServo.attach(SERVO_PIN_VERTICAL);
  horizontalServo.write(horizontalAngle);
  verticalServo.write(verticalAngle);

  ledcAttach(LED_PIN, LED_PWM_FREQ, LED_PWM_RESOLUTION);
  ledcWrite(LED_PIN, 0);

  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setMTU(64);   // commands are tiny, no reason to negotiate big

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService *service = server->createService(SERVICE_UUID);

  NimBLECharacteristic *rxChar = service->createCharacteristic(
      CHAR_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rxChar->setCallbacks(new RxCallbacks());

  txChar = service->createCharacteristic(CHAR_TX_UUID, NIMBLE_PROPERTY::NOTIFY);

  service->start();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setName(DEVICE_NAME);
  adv->addServiceUUID(SERVICE_UUID);
  adv->enableScanResponse(true);
  adv->start();

  Serial.printf("Advertising as \"%s\"\n", DEVICE_NAME);
  Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
  Serial.println("Ready.");
}

// ===========================
// Loop
// ===========================
void loop() {
  unsigned long now = millis();

  // Deadman watchdog: a silent link means the operator is not in control
  bool active = movingUp || movingDown || movingLeft || movingRight || triggerActive;
  if (active && (now - lastCommandTime >= ACTIVE_TIMEOUT_MS)) {
    Serial.println("Watchdog: no traffic, stopping everything");
    stopAll();
    notify("OK watchdog");
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

  if (positionChanged && (now - lastNotifyTime >= NOTIFY_INTERVAL)) {
    lastNotifyTime = now;
    notifyPosition();
  }

  if (positionChanged && !positionsSaved && (now - lastActivityTime >= SAVE_DELAY)) {
    savePositions();
    positionChanged = false;
  }

  delay(1);   // let the NimBLE host task run
}
