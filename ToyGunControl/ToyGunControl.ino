/*
 * Toy Gun Turret Control - Step 3a
 * Webserver with WiFi connection
 *
 * Features:
 * - WiFi connection
 * - Basic web server
 * - Servo control (will add UI in next step)
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

// ===========================
// WiFi credentials
// ===========================
const char* ssid = "Edgar";
const char* password = "Password!23";
const char* hostname = "toygun";  // Access via http://toygun.local

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
const int VERTICAL_MAX = min(180, VERTICAL_CENTER + VERTICAL_RANGE);          // 105°

// Current angle variables (tracking servo positions)
int horizontalAngle = HORIZONTAL_CENTER;  // Start at center
int verticalAngle = VERTICAL_CENTER;      // Start at center

// ===========================
// HTML webpage
// ===========================
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Toy Gun Turret Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {
      font-family: Arial, sans-serif;
      text-align: center;
      margin: 20px;
      background-color: #f0f0f0;
    }
    h1 {
      color: #333;
    }
    .info {
      background-color: #fff;
      padding: 15px;
      border-radius: 10px;
      margin: 20px auto;
      max-width: 400px;
      box-shadow: 0 2px 5px rgba(0,0,0,0.2);
    }
    .status {
      font-size: 1.2em;
      margin: 10px 0;
    }
  </style>
</head>
<body>
  <h1>🎯 Toy Gun Turret</h1>
  <div class="info">
    <div class="status">
      Horizontal: <span id="hAngle">90</span>°
    </div>
    <div class="status">
      Vertical: <span id="vAngle">90</span>°
    </div>
    <p style="color: #666; margin-top: 20px;">
      Control interface coming in next step!
    </p>
  </div>
</body>
</html>
)rawliteral";

// ===========================
// Handler functions
// ===========================
void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}

void setup() {
  // Initialize serial communication for debugging
  Serial.begin(115200);
  Serial.println("\n\nToy Gun Turret - Step 3a: Webserver");

  // Attach servos to GPIO pins
  horizontalServo.attach(SERVO_PIN_HORIZONTAL);
  verticalServo.attach(SERVO_PIN_VERTICAL);

  // Set both servos to center position
  horizontalServo.write(horizontalAngle);
  verticalServo.write(verticalAngle);

  Serial.println("Servos initialized to center position");
  Serial.print("Horizontal: ");
  Serial.print(horizontalAngle);
  Serial.print("° (range: ");
  Serial.print(HORIZONTAL_MIN);
  Serial.print("° to ");
  Serial.print(HORIZONTAL_MAX);
  Serial.println("°)");

  Serial.print("Vertical: ");
  Serial.print(verticalAngle);
  Serial.print("° (range: ");
  Serial.print(VERTICAL_MIN);
  Serial.print("° to ");
  Serial.print(VERTICAL_MAX);
  Serial.println("°)");

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
  server.onNotFound(handleNotFound);

  // Start server
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  // Handle incoming web requests
  server.handleClient();

  // Servos hold position (no auto-movement anymore)
  // Manual control will be added in Step 3b
  // Note: mDNS runs automatically on ESP32 (no update() needed)
}
