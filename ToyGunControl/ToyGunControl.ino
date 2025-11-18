/*
 * Toy Gun Turret Control - Step 3d
 * Directional control with position persistence
 *
 * Features:
 * - WiFi connection with static IP and mDNS
 * - Web interface with directional control buttons
 * - Press and hold button to move servo
 * - Release button to stop movement
 * - Real-time position display
 * - Position persistence (survives power cycles)
 * - Auto-save after 3 seconds idle
 *
 * Movement ranges:
 * - Vertical: ±15° from center (75° to 105°)
 * - Horizontal: ±90° from center (0° to 180°)
 *
 * Hardware:
 * - Horizontal servo on GPIO 12
 * - Vertical servo on GPIO 13
 * - Both servos powered by 6V buck converter (not ESP32!)
 *
 * Future: GPIO 14 (Relay 1 - Spinner), GPIO 15 (Relay 2 - Trigger)
 */

#include <ESP32Servo.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

// ===========================
// WiFi credentials
// ===========================
const char* ssid = "Edgar";
const char* password = "Password!23";
const char* hostname = "lizardgun3000";  // Access via http://toygun.local

// Static IP configuration (optional - comment out to use DHCP)
// How to find these values:
//   Mac: System Settings → Network → WiFi → Details
//   iPhone: Settings → WiFi → (i) button → Look for Router, Subnet Mask, DNS
//   Windows: ipconfig /all in Command Prompt
IPAddress local_IP(192, 168, 86, 42);      // Your desired static IP
IPAddress gateway(192, 168, 86, 1);        // Your router IP (check your network settings)
IPAddress subnet(255, 255, 255, 0);        // Subnet mask (typically 255.255.255.0)
IPAddress primaryDNS(192, 168, 86, 1);     // DNS server (typically same as gateway for home routers)
IPAddress secondaryDNS(8, 8, 8, 8);        // Fallback DNS (Google's public DNS)

// GPIO pin definitions
#define SERVO_PIN_HORIZONTAL 12
#define SERVO_PIN_VERTICAL   13

// Servo objects
Servo horizontalServo;
Servo verticalServo;

// Webserver
WebServer server(80);

// Preferences for position persistence
Preferences preferences;

// Movement ranges - center positions
const int HORIZONTAL_CENTER = 90;
const int VERTICAL_CENTER = 90;

// Movement range offsets
const int HORIZONTAL_RANGE = 90;  // ±90° from center
const int VERTICAL_RANGE = 15;    // ±15° from center

// Calculated min/max positions
const int HORIZONTAL_MIN = max(0, HORIZONTAL_CENTER - HORIZONTAL_RANGE);      // 0° (clamped)
const int HORIZONTAL_MAX = min(180, HORIZONTAL_CENTER + HORIZONTAL_RANGE);    // 180°
const int VERTICAL_MIN = max(0, VERTICAL_CENTER - VERTICAL_RANGE);            // 75°
const int VERTICAL_MAX = 100;                                                 // 100° (restricted lower limit)

// Current angle variables (tracking servo positions)
int horizontalAngle = HORIZONTAL_CENTER;  // Start at center
int verticalAngle = VERTICAL_CENTER;      // Start at center

// Movement control
bool movingUp = false;
bool movingDown = false;
bool movingLeft = false;
bool movingRight = false;

unsigned long lastMoveTime = 0;
const int MOVE_DELAY = 40;  // milliseconds between each degree of movement (slower = higher value)
const int MOVE_STEP = 1;    // degrees to move per step

// Position persistence
unsigned long lastActivityTime = 0;
const unsigned long SAVE_DELAY = 3000;  // Save 3 seconds after last movement
bool positionChanged = false;
bool positionsSaved = true;  // Start as true (no unsaved changes)

// ===========================
// HTML webpage
// ===========================
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Toy Gun Turret Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
  <style>
    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
      -webkit-touch-callout: none;
      -webkit-user-select: none;
      user-select: none;
    }
    body {
      font-family: Arial, sans-serif;
      text-align: center;
      padding: 20px;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
    }
    h1 {
      color: white;
      margin-bottom: 20px;
      text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
    }
    .container {
      background-color: #fff;
      padding: 20px;
      border-radius: 15px;
      margin: 0 auto;
      max-width: 400px;
      box-shadow: 0 4px 15px rgba(0,0,0,0.3);
    }
    .status {
      font-size: 1.3em;
      margin: 15px 0;
      padding: 10px;
      background-color: #f8f9fa;
      border-radius: 8px;
      color: #333;
    }
    .status span {
      font-weight: bold;
      color: #667eea;
    }
    .controls {
      margin: 30px auto;
      width: 200px;
      height: 200px;
      position: relative;
    }
    .btn {
      position: absolute;
      width: 60px;
      height: 60px;
      border: none;
      border-radius: 10px;
      font-size: 24px;
      cursor: pointer;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
      box-shadow: 0 4px 10px rgba(0,0,0,0.2);
      transition: all 0.1s;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .btn:active {
      transform: scale(0.95);
      box-shadow: 0 2px 5px rgba(0,0,0,0.3);
    }
    .btn.pressed {
      background: linear-gradient(135deg, #764ba2 0%, #667eea 100%);
      transform: scale(0.95);
    }
    #btnUp {
      top: 0;
      left: 50%;
      transform: translateX(-50%);
    }
    #btnDown {
      bottom: 0;
      left: 50%;
      transform: translateX(-50%);
    }
    #btnLeft {
      left: 0;
      top: 50%;
      transform: translateY(-50%);
    }
    #btnRight {
      right: 0;
      top: 50%;
      transform: translateY(-50%);
    }
    .center-dot {
      position: absolute;
      width: 20px;
      height: 20px;
      background-color: #ddd;
      border-radius: 50%;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
    }
    .info {
      margin-top: 20px;
      padding: 10px;
      background-color: #f8f9fa;
      border-radius: 8px;
      font-size: 0.9em;
      color: #666;
    }
  </style>
</head>
<body>
  <h1>🎯 Toy Gun Turret</h1>
  <div class="container">
    <div class="status">
      Horizontal: <span id="hAngle">90</span>°
    </div>
    <div class="status">
      Vertical: <span id="vAngle">90</span>°
    </div>

    <div class="controls">
      <button class="btn" id="btnUp">▲</button>
      <button class="btn" id="btnDown">▼</button>
      <button class="btn" id="btnLeft">◀</button>
      <button class="btn" id="btnRight">▶</button>
      <div class="center-dot"></div>
    </div>

    <div class="info">
      Press and hold buttons to move<br>
      Release to stop
    </div>

    <button id="btnReset" style="margin-top: 20px; padding: 12px 24px; background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%); color: white; border: none; border-radius: 8px; font-size: 1em; cursor: pointer; box-shadow: 0 4px 10px rgba(0,0,0,0.2);">
      🔄 Reset to Center (90°)
    </button>
  </div>

  <script>
    // Movement control functions
    function startMove(direction) {
      fetch('/move?dir=' + direction + '&state=1')
        .catch(err => console.error('Error:', err));
    }

    function stopMove(direction) {
      fetch('/move?dir=' + direction + '&state=0')
        .catch(err => console.error('Error:', err));
    }

    // Update position display
    function updatePosition() {
      fetch('/position')
        .then(response => response.json())
        .then(data => {
          document.getElementById('hAngle').textContent = data.h;
          document.getElementById('vAngle').textContent = data.v;
        })
        .catch(err => console.error('Error:', err));
    }

    // Setup buttons
    const buttons = {
      btnUp: 'up',
      btnDown: 'down',
      btnLeft: 'left',
      btnRight: 'right'
    };

    Object.keys(buttons).forEach(btnId => {
      const btn = document.getElementById(btnId);
      const dir = buttons[btnId];

      // Mouse events
      btn.addEventListener('mousedown', () => {
        btn.classList.add('pressed');
        startMove(dir);
      });
      btn.addEventListener('mouseup', () => {
        btn.classList.remove('pressed');
        stopMove(dir);
      });
      btn.addEventListener('mouseleave', () => {
        btn.classList.remove('pressed');
        stopMove(dir);
      });

      // Touch events for mobile
      btn.addEventListener('touchstart', (e) => {
        e.preventDefault();
        btn.classList.add('pressed');
        startMove(dir);
      });
      btn.addEventListener('touchend', (e) => {
        e.preventDefault();
        btn.classList.remove('pressed');
        stopMove(dir);
      });
      btn.addEventListener('touchcancel', (e) => {
        e.preventDefault();
        btn.classList.remove('pressed');
        stopMove(dir);
      });
    });

    // Reset button
    document.getElementById('btnReset').addEventListener('click', () => {
      if (confirm('Reset servos to center (90°) and clear saved positions?')) {
        fetch('/reset')
          .then(response => response.text())
          .then(data => {
            alert('✓ Reset complete! Servos moved to 90°');
            updatePosition();
          })
          .catch(err => {
            alert('✗ Reset failed: ' + err);
            console.error('Error:', err);
          });
      }
    });

    // Update position every 50ms (20 times per second for smooth display)
    setInterval(updatePosition, 50);
    updatePosition(); // Initial update
  </script>
</body>
</html>
)rawliteral";

// ===========================
// Position persistence functions
// ===========================
void loadPositions() {
  preferences.begin("turret", true);  // Read-only mode

  horizontalAngle = preferences.getInt("hAngle", HORIZONTAL_CENTER);
  verticalAngle = preferences.getInt("vAngle", VERTICAL_CENTER);

  preferences.end();

  Serial.println("Loaded saved positions:");
  Serial.print("  Horizontal: ");
  Serial.print(horizontalAngle);
  Serial.println("°");
  Serial.print("  Vertical: ");
  Serial.print(verticalAngle);
  Serial.println("°");
}

void savePositions() {
  preferences.begin("turret", false);  // Read-write mode

  preferences.putInt("hAngle", horizontalAngle);
  preferences.putInt("vAngle", verticalAngle);

  preferences.end();

  positionsSaved = true;

  Serial.println("💾 Positions saved to flash:");
  Serial.print("  Horizontal: ");
  Serial.print(horizontalAngle);
  Serial.println("°");
  Serial.print("  Vertical: ");
  Serial.print(verticalAngle);
  Serial.println("°");
}

// ===========================
// Handler functions
// ===========================
void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

void handleMove() {
  if (server.hasArg("dir") && server.hasArg("state")) {
    String direction = server.arg("dir");
    int state = server.arg("state").toInt();

    if (direction == "up") {
      movingUp = (state == 1);
    } else if (direction == "down") {
      movingDown = (state == 1);
    } else if (direction == "left") {
      movingLeft = (state == 1);
    } else if (direction == "right") {
      movingRight = (state == 1);
    }

    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void handlePosition() {
  String json = "{\"h\":" + String(horizontalAngle) + ",\"v\":" + String(verticalAngle) + "}";
  server.send(200, "application/json", json);
}

void handleReset() {
  Serial.println("\n🔄 Reset requested - clearing saved positions");

  // Clear saved positions from flash
  preferences.begin("turret", false);
  preferences.clear();
  preferences.end();

  // Reset to center positions
  horizontalAngle = HORIZONTAL_CENTER;
  verticalAngle = VERTICAL_CENTER;

  // Move servos to center
  horizontalServo.write(horizontalAngle);
  verticalServo.write(verticalAngle);

  // Mark as saved (no pending changes)
  positionsSaved = true;
  positionChanged = false;

  Serial.println("✓ Reset complete:");
  Serial.print("  Horizontal: ");
  Serial.print(horizontalAngle);
  Serial.println("°");
  Serial.print("  Vertical: ");
  Serial.print(verticalAngle);
  Serial.println("°");
  Serial.println("  Saved positions cleared from flash");

  server.send(200, "text/plain", "Reset complete");
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}

void setup() {
  // Initialize serial communication for debugging
  Serial.begin(115200);
  Serial.println("\n\nToy Gun Turret - Step 3d: Position Persistence");

  // Load saved positions from flash (or use defaults)
  loadPositions();

  // Attach servos to GPIO pins
  horizontalServo.attach(SERVO_PIN_HORIZONTAL);
  verticalServo.attach(SERVO_PIN_VERTICAL);

  // Set servos to loaded/saved position
  horizontalServo.write(horizontalAngle);
  verticalServo.write(verticalAngle);

  Serial.println("\nServo ranges:");
  Serial.print("  Horizontal: ");
  Serial.print(HORIZONTAL_MIN);
  Serial.print("° to ");
  Serial.print(HORIZONTAL_MAX);
  Serial.println("°");

  Serial.print("  Vertical: ");
  Serial.print(VERTICAL_MIN);
  Serial.print("° to ");
  Serial.print(VERTICAL_MAX);
  Serial.println("°");

  // Configure static IP (comment out these lines to use DHCP)
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Static IP configuration failed");
  }

  // Set hostname
  WiFi.setHostname(hostname);

  // Connect to WiFi
  Serial.println("\nConnecting to WiFi...");
  Serial.print("SSID: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Start mDNS responder
    if (MDNS.begin(hostname)) {
      Serial.print("mDNS responder started: http://");
      Serial.print(hostname);
      Serial.println(".local");
    } else {
      Serial.println("Error setting up mDNS responder!");
    }

    Serial.println("\nAccess the turret at:");
    Serial.print("  http://");
    Serial.println(WiFi.localIP());
    Serial.print("  http://");
    Serial.print(hostname);
    Serial.println(".local");
  } else {
    Serial.println("\n✗ WiFi connection failed!");
    Serial.println("Please check your SSID and password");
  }

  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/position", handlePosition);
  server.on("/reset", handleReset);
  server.onNotFound(handleNotFound);

  // Start server
  server.begin();
  Serial.println("Web server started");
  Serial.println("\nReady for control!");
}

void loop() {
  // Handle incoming web requests
  server.handleClient();

  // Handle servo movement based on button presses
  unsigned long currentTime = millis();

  if (currentTime - lastMoveTime >= MOVE_DELAY) {
    lastMoveTime = currentTime;
    bool moved = false;

    // Vertical movement (reversed: up button decreases angle, down button increases)
    if (movingUp && verticalAngle > VERTICAL_MIN) {
      verticalAngle -= MOVE_STEP;
      verticalServo.write(verticalAngle);
      moved = true;
    }
    if (movingDown && verticalAngle < VERTICAL_MAX) {
      verticalAngle += MOVE_STEP;
      verticalServo.write(verticalAngle);
      moved = true;
    }

    // Horizontal movement
    if (movingLeft && horizontalAngle > HORIZONTAL_MIN) {
      horizontalAngle -= MOVE_STEP;
      horizontalServo.write(horizontalAngle);
      moved = true;
    }
    if (movingRight && horizontalAngle < HORIZONTAL_MAX) {
      horizontalAngle += MOVE_STEP;
      horizontalServo.write(horizontalAngle);
      moved = true;
    }

    // Track activity for auto-save
    if (moved) {
      lastActivityTime = currentTime;
      positionChanged = true;
      positionsSaved = false;

      // Debug output when moving
      Serial.print("Position - H: ");
      Serial.print(horizontalAngle);
      Serial.print("° V: ");
      Serial.print(verticalAngle);
      Serial.println("°");
    }
  }

  // Auto-save positions after idle period
  if (positionChanged && !positionsSaved &&
      (currentTime - lastActivityTime >= SAVE_DELAY)) {
    savePositions();
    positionChanged = false;
  }
}
