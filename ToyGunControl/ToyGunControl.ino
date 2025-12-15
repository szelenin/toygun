/*
 * Toy Gun Turret Control - Step 6
 * ESP32-CAM with camera streaming and turret control
 *
 * Features:
 * - Live camera stream on port 81
 * - Web interface with directional control buttons
 * - Press and hold button to move servo
 * - Release button to stop movement
 * - Real-time position display
 * - Position persistence (survives power cycles)
 * - Auto-save after 3 seconds idle
 * - 2-channel relay control for spinner and trigger motors
 * - Press-and-hold trigger: Hold to fire continuously, release to stop instantly
 * - Flash LED control (GPIO 4) for night vision
 *
 * Movement ranges:
 * - Vertical: 75° to 100°
 * - Horizontal: 0° to 180°
 *
 * Hardware:
 * - Horizontal servo on GPIO 12
 * - Vertical servo on GPIO 13
 * - Relay 1 (Trigger) on GPIO 14
 * - Relay 2 (Spinner) on GPIO 15
 * - OV2640 Camera (internal ESP32-CAM pins)
 * - Servos powered by 6V buck converter (not ESP32!)
 * - Relays powered by 5V buck converter (active-LOW)
 */

#include "esp_camera.h"
#include "esp_http_server.h"
#include <ESP32Servo.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>

// ===========================
// AI Thinker ESP32-CAM pin definitions
// ===========================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ===========================
// WiFi credentials
// ===========================
const char* ssid = "Edgar";
const char* password = "Password!23";
const char* hostname = "lizardgun3000";  // Access via http://lizardgun3000.local

// Static IP configuration (optional - comment out to use DHCP)
IPAddress local_IP(192, 168, 86, 42);
IPAddress gateway(192, 168, 86, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 86, 1);
IPAddress secondaryDNS(8, 8, 8, 8);

// GPIO pin definitions for servos and relays
#define SERVO_PIN_HORIZONTAL 12
#define SERVO_PIN_VERTICAL   13
#define RELAY_PIN_SPINNER    15
#define RELAY_PIN_TRIGGER    14
#define LED_FLASH_PIN         4  // Flash LED on ESP32-CAM
#define LED_PWM_FREQ       5000  // 5kHz PWM frequency
#define LED_PWM_RESOLUTION    8  // 8-bit resolution (0-255)

// Servo objects
Servo horizontalServo;
Servo verticalServo;

// Relay state
bool spinnerActive = false;
bool triggerActive = false;

// Flash LED state (0-255 brightness)
int ledBrightness = 0;

// HTTP server handles (both use esp_http_server for non-blocking operation)
httpd_handle_t control_httpd = NULL;  // Port 80 - control UI
httpd_handle_t stream_httpd = NULL;   // Port 81 - camera stream

// Preferences for position persistence
Preferences preferences;

// Movement ranges
const int HORIZONTAL_CENTER = 90;
const int VERTICAL_CENTER = 90;
const int HORIZONTAL_RANGE = 90;
const int VERTICAL_RANGE = 15;

const int HORIZONTAL_MIN = max(0, HORIZONTAL_CENTER - HORIZONTAL_RANGE);
const int HORIZONTAL_MAX = min(180, HORIZONTAL_CENTER + HORIZONTAL_RANGE);
const int VERTICAL_MIN = max(0, VERTICAL_CENTER - VERTICAL_RANGE);
const int VERTICAL_MAX = 100;

// Current angle variables
int horizontalAngle = HORIZONTAL_CENTER;
int verticalAngle = VERTICAL_CENTER;

// Movement control
bool movingUp = false;
bool movingDown = false;
bool movingLeft = false;
bool movingRight = false;

unsigned long lastMoveTime = 0;
const int MOVE_DELAY = 40;
const int MOVE_STEP = 1;

// Position persistence
unsigned long lastActivityTime = 0;
const unsigned long SAVE_DELAY = 3000;
bool positionChanged = false;
bool positionsSaved = true;

// ===========================
// Camera stream constants
// ===========================
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ===========================
// HTML webpage with camera stream
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
      padding: 10px;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
    }
    h1 {
      color: white;
      margin-bottom: 10px;
      text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
      font-size: 1.5em;
    }
    .container {
      background-color: #fff;
      padding: 15px;
      border-radius: 15px;
      margin: 0 auto;
      max-width: 500px;
      box-shadow: 0 4px 15px rgba(0,0,0,0.3);
    }
    .video-container {
      background: #000;
      border-radius: 10px;
      overflow: hidden;
      margin-bottom: 15px;
      position: relative;
    }
    .video-container img {
      width: 100%;
      height: auto;
      display: block;
    }
    .crosshair {
      position: absolute;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      pointer-events: none;
    }
    .crosshair::before,
    .crosshair::after {
      content: '';
      position: absolute;
      background: rgba(255, 0, 0, 0.8);
    }
    .crosshair::before {
      width: 2px;
      height: 40px;
      left: 50%;
      top: 50%;
      transform: translate(-50%, -50%);
    }
    .crosshair::after {
      width: 40px;
      height: 2px;
      left: 50%;
      top: 50%;
      transform: translate(-50%, -50%);
    }
    .status-row {
      display: flex;
      justify-content: space-around;
      margin-bottom: 10px;
    }
    .status {
      font-size: 1em;
      padding: 8px 15px;
      background-color: #f8f9fa;
      border-radius: 8px;
      color: #333;
    }
    .status span {
      font-weight: bold;
      color: #667eea;
    }
    .controls {
      margin: 15px auto;
      width: 180px;
      height: 180px;
      position: relative;
    }
    .btn {
      position: absolute;
      width: 55px;
      height: 55px;
      border: none;
      border-radius: 10px;
      font-size: 22px;
      cursor: pointer;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
      box-shadow: 0 4px 10px rgba(0,0,0,0.2);
      transition: all 0.1s;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .btn:active, .btn.pressed {
      transform: scale(0.95);
      background: linear-gradient(135deg, #764ba2 0%, #667eea 100%);
    }
    #btnUp { top: 0; left: 50%; transform: translateX(-50%); }
    #btnDown { bottom: 0; left: 50%; transform: translateX(-50%); }
    #btnLeft { left: 0; top: 50%; transform: translateY(-50%); }
    #btnRight { right: 0; top: 50%; transform: translateY(-50%); }
    #btnUp.pressed, #btnDown.pressed { transform: translateX(-50%) scale(0.95); }
    #btnLeft.pressed, #btnRight.pressed { transform: translateY(-50%) scale(0.95); }
    .center-dot {
      position: absolute;
      width: 18px;
      height: 18px;
      background-color: #ddd;
      border-radius: 50%;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
    }
    #btnShoot {
      margin-top: 15px;
      padding: 15px 30px;
      background: linear-gradient(135deg, #f5576c 0%, #e84118 100%);
      color: white;
      border: none;
      border-radius: 12px;
      font-size: 1.3em;
      font-weight: bold;
      cursor: pointer;
      box-shadow: 0 6px 15px rgba(232, 65, 24, 0.4);
    }
    #btnShoot:active, #btnShoot.pressed {
      transform: scale(0.95);
    }
    #btnReset {
      margin-top: 10px;
      padding: 8px 16px;
      background: linear-gradient(135deg, #a29bfe 0%, #6c5ce7 100%);
      color: white;
      border: none;
      border-radius: 8px;
      font-size: 0.85em;
      cursor: pointer;
    }
  </style>
</head>
<body>
  <h1>🎯 Toy Gun Turret</h1>
  <div class="container">
    <div class="video-container">
      <img id="stream" src="">
      <div class="crosshair"></div>
    </div>

    <div class="status-row">
      <div class="status">H: <span id="hAngle">90</span>°</div>
      <div class="status">V: <span id="vAngle">90</span>°</div>
    </div>

    <div class="controls">
      <button class="btn" id="btnUp">▲</button>
      <button class="btn" id="btnDown">▼</button>
      <button class="btn" id="btnLeft">◀</button>
      <button class="btn" id="btnRight">▶</button>
      <div class="center-dot"></div>
    </div>

    <button id="btnShoot">🔫 SHOOT</button>

    <div style="margin-top: 15px; padding: 10px; background: #f8f9fa; border-radius: 10px;">
      <div style="display: flex; align-items: center; gap: 10px;">
        <span>💡</span>
        <input type="range" id="ledSlider" min="0" max="255" value="0" style="flex: 1;">
        <span id="ledValue" style="min-width: 35px;">0%</span>
      </div>
      <div style="font-size: 0.75em; color: #666; margin-top: 5px;">Tip: Keep below 50% to avoid WiFi interference</div>
    </div>

    <button id="btnReset" style="margin-top: 10px; padding: 10px 16px; background: linear-gradient(135deg, #a29bfe 0%, #6c5ce7 100%); color: white; border: none; border-radius: 8px; font-size: 0.85em; cursor: pointer;">🔄 Reset</button>
    <br>
    <a href="/admin" style="display: inline-block; margin-top: 15px; color: #667eea; font-size: 0.85em;">⚙️ Admin Settings</a>
  </div>

  <script>
    // Set stream source to port 81
    const streamUrl = 'http://' + window.location.hostname + ':81/stream';
    document.getElementById('stream').src = streamUrl;

    // Movement control functions
    function startMove(direction) {
      fetch('/move?dir=' + direction + '&state=1').catch(err => console.error('Error:', err));
    }
    function stopMove(direction) {
      fetch('/move?dir=' + direction + '&state=0').catch(err => console.error('Error:', err));
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

    // Setup direction buttons
    const buttons = { btnUp: 'up', btnDown: 'down', btnLeft: 'left', btnRight: 'right' };
    Object.keys(buttons).forEach(btnId => {
      const btn = document.getElementById(btnId);
      const dir = buttons[btnId];
      btn.addEventListener('mousedown', () => { btn.classList.add('pressed'); startMove(dir); });
      btn.addEventListener('mouseup', () => { btn.classList.remove('pressed'); stopMove(dir); });
      btn.addEventListener('mouseleave', () => { btn.classList.remove('pressed'); stopMove(dir); });
      btn.addEventListener('touchstart', (e) => { e.preventDefault(); btn.classList.add('pressed'); startMove(dir); });
      btn.addEventListener('touchend', (e) => { e.preventDefault(); btn.classList.remove('pressed'); stopMove(dir); });
      btn.addEventListener('touchcancel', (e) => { e.preventDefault(); btn.classList.remove('pressed'); stopMove(dir); });
    });

    // Shoot button
    const btnShoot = document.getElementById('btnShoot');
    let firing = false;
    function startFiring() {
      if (firing) return;
      firing = true;
      btnShoot.classList.add('pressed');
      btnShoot.textContent = '🔥 FIRING...';
      fetch('/shoot?state=start').catch(err => console.error('Error:', err));
    }
    function stopFiring() {
      if (!firing) return;
      firing = false;
      btnShoot.classList.remove('pressed');
      btnShoot.textContent = '🔫 SHOOT';
      fetch('/shoot?state=stop').catch(err => console.error('Error:', err));
    }
    btnShoot.addEventListener('mousedown', startFiring);
    btnShoot.addEventListener('mouseup', stopFiring);
    btnShoot.addEventListener('mouseleave', stopFiring);
    btnShoot.addEventListener('touchstart', (e) => { e.preventDefault(); startFiring(); });
    btnShoot.addEventListener('touchend', (e) => { e.preventDefault(); stopFiring(); });

    // LED brightness slider - only sends on release to avoid flooding
    const ledSlider = document.getElementById('ledSlider');
    const ledValue = document.getElementById('ledValue');
    ledSlider.addEventListener('input', (e) => {
      ledValue.textContent = Math.round(e.target.value / 255 * 100) + '%';
    });
    ledSlider.addEventListener('change', (e) => {
      fetch('/led?brightness=' + e.target.value).catch(err => console.error('Error:', err));
    });

    // Reset button
    document.getElementById('btnReset').addEventListener('click', () => {
      if (confirm('Reset servos to center (90°)?')) {
        fetch('/reset').then(() => updatePosition()).catch(err => console.error('Error:', err));
      }
    });

    setInterval(updatePosition, 100);
    updatePosition();
  </script>
</body>
</html>
)rawliteral";

// ===========================
// Admin page HTML
// ===========================
const char* adminPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Turret Admin</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: Arial, sans-serif;
      padding: 20px;
      background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
      min-height: 100vh;
      color: white;
    }
    h1 { margin-bottom: 20px; }
    .container {
      background: rgba(255,255,255,0.1);
      padding: 20px;
      border-radius: 15px;
      max-width: 500px;
      margin: 0 auto;
    }
    .stat {
      display: flex;
      justify-content: space-between;
      padding: 12px;
      background: rgba(0,0,0,0.3);
      border-radius: 8px;
      margin-bottom: 10px;
    }
    .stat-value { font-weight: bold; color: #4ade80; }
    .stat-value.warning { color: #fbbf24; }
    .stat-value.bad { color: #f87171; }
    label { display: block; margin: 15px 0 5px; font-weight: bold; }
    select, input[type=range] {
      width: 100%;
      padding: 10px;
      border-radius: 8px;
      border: none;
      font-size: 1em;
    }
    input[type=range] { padding: 5px; }
    .quality-value { text-align: center; font-size: 1.2em; margin: 5px 0; }
    button {
      width: 100%;
      padding: 15px;
      margin-top: 20px;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
      border: none;
      border-radius: 10px;
      font-size: 1.1em;
      cursor: pointer;
    }
    button:active { transform: scale(0.98); }
    .back-link {
      display: block;
      text-align: center;
      margin-top: 15px;
      color: #a5b4fc;
    }
    .hint { font-size: 0.8em; color: #888; margin-top: 3px; }
  </style>
</head>
<body>
  <div class="container">
    <h1>⚙️ Admin Panel</h1>

    <div class="stat">
      <span>WiFi Signal (RSSI)</span>
      <span class="stat-value" id="rssi">-- dBm</span>
    </div>
    <div class="stat">
      <span>Stream FPS</span>
      <span class="stat-value" id="fps">-- fps</span>
    </div>
    <div class="stat">
      <span>Frame Size</span>
      <span class="stat-value" id="frameSize">--</span>
    </div>
    <div class="stat">
      <span>Free Heap Memory</span>
      <span class="stat-value" id="heap">-- KB</span>
    </div>
    <div class="stat">
      <span>Free PSRAM</span>
      <span class="stat-value" id="psram">-- KB</span>
    </div>
    <div class="stat">
      <span>PSRAM Status</span>
      <span class="stat-value" id="psramStatus">--</span>
    </div>
    <div class="stat">
      <span>Chip Temperature</span>
      <span class="stat-value" id="temp">--°C</span>
    </div>

    <h3 style="margin-top: 20px; margin-bottom: 10px; color: #a5b4fc;">Frame Timing</h3>
    <div class="stat">
      <span>Capture Time</span>
      <span class="stat-value" id="captureMs">-- ms</span>
    </div>
    <div class="stat">
      <span>Send Time</span>
      <span class="stat-value" id="sendMs">-- ms</span>
    </div>
    <div class="stat">
      <span>Avg Frame Size</span>
      <span class="stat-value" id="avgFrameKB">-- KB</span>
    </div>

    <label>Resolution</label>
    <select id="resolution">
      <option value="4">QVGA (320x240) - Fastest</option>
      <option value="5">CIF (400x296)</option>
      <option value="6">HVGA (480x320)</option>
      <option value="8">VGA (640x480) - Default</option>
      <option value="9">SVGA (800x600)</option>
      <option value="10">XGA (1024x768)</option>
      <option value="12">SXGA (1280x1024)</option>
      <option value="13">UXGA (1600x1200) - Slowest</option>
    </select>
    <div class="hint">Lower = faster streaming, less detail</div>

    <label>JPEG Quality: <span id="qualityVal">12</span></label>
    <input type="range" id="quality" min="10" max="63" value="12">
    <div class="hint">10 = best quality (slow), 63 = most compressed (fast)</div>
    <div class="hint" style="color: #4ade80; margin-top: 10px;">Settings auto-apply and save to flash</div>

    <a href="/" class="back-link">← Back to Control</a>
  </div>

  <script>
    let lastUserChange = 0;  // Timestamp of last user change
    const UI_LOCK_MS = 3000; // Don't overwrite UI for 3 seconds after user change

    // Update stats
    function updateStats() {
      fetch('/stats')
        .then(r => r.json())
        .then(data => {
          const rssiEl = document.getElementById('rssi');
          rssiEl.textContent = data.rssi + ' dBm';
          rssiEl.className = 'stat-value' + (data.rssi > -60 ? '' : data.rssi > -75 ? ' warning' : ' bad');

          const fpsEl = document.getElementById('fps');
          fpsEl.textContent = data.fps.toFixed(1) + ' fps';
          fpsEl.className = 'stat-value' + (data.fps > 15 ? '' : data.fps > 8 ? ' warning' : ' bad');

          document.getElementById('frameSize').textContent = data.frameWidth + 'x' + data.frameHeight;

          const heapEl = document.getElementById('heap');
          const heapKB = (data.freeHeap / 1024).toFixed(0);
          heapEl.textContent = heapKB + ' KB';
          heapEl.className = 'stat-value' + (heapKB > 50 ? '' : heapKB > 20 ? ' warning' : ' bad');

          const psramEl = document.getElementById('psram');
          const psramKB = (data.freePsram / 1024).toFixed(0);
          psramEl.textContent = psramKB + ' KB';
          psramEl.className = 'stat-value' + (psramKB > 1000 ? '' : psramKB > 500 ? ' warning' : ' bad');

          const psramStatusEl = document.getElementById('psramStatus');
          psramStatusEl.textContent = data.psramFound ? 'Detected' : 'NOT FOUND';
          psramStatusEl.className = 'stat-value' + (data.psramFound ? '' : ' bad');

          const tempEl = document.getElementById('temp');
          tempEl.textContent = data.tempC.toFixed(1) + '°C';
          tempEl.className = 'stat-value' + (data.tempC < 60 ? '' : data.tempC < 75 ? ' warning' : ' bad');

          // Frame timing stats
          const captureEl = document.getElementById('captureMs');
          captureEl.textContent = data.captureMs + ' ms';
          captureEl.className = 'stat-value' + (data.captureMs < 100 ? '' : data.captureMs < 200 ? ' warning' : ' bad');

          const sendEl = document.getElementById('sendMs');
          sendEl.textContent = data.sendMs + ' ms';
          sendEl.className = 'stat-value' + (data.sendMs < 100 ? '' : data.sendMs < 200 ? ' warning' : ' bad');

          const frameSizeEl = document.getElementById('avgFrameKB');
          frameSizeEl.textContent = data.avgFrameKB.toFixed(1) + ' KB';
          frameSizeEl.className = 'stat-value' + (data.avgFrameKB < 50 ? '' : data.avgFrameKB < 100 ? ' warning' : ' bad');

          // Only update settings UI if user hasn't changed them recently
          if (Date.now() - lastUserChange > UI_LOCK_MS) {
            document.getElementById('resolution').value = data.framesize;
            document.getElementById('quality').value = data.quality;
            document.getElementById('qualityVal').textContent = data.quality;
          }
        })
        .catch(err => console.error('Error:', err));
    }

    // Auto-apply function
    function applySettings() {
      lastUserChange = Date.now();  // Lock UI updates
      const resolution = document.getElementById('resolution').value;
      const quality = document.getElementById('quality').value;
      fetch('/config?framesize=' + resolution + '&quality=' + quality)
        .catch(err => console.error('Error:', err));
    }

    // Auto-apply on resolution change
    document.getElementById('resolution').addEventListener('change', applySettings);

    // Auto-apply on quality change (with debounce)
    let qualityTimeout;
    document.getElementById('quality').addEventListener('input', (e) => {
      lastUserChange = Date.now();  // Lock UI immediately while dragging
      document.getElementById('qualityVal').textContent = e.target.value;
      clearTimeout(qualityTimeout);
      qualityTimeout = setTimeout(applySettings, 300);
    });

    setInterval(updateStats, 1000);
    updateStats();
  </script>
</body>
</html>
)rawliteral";

// FPS and timing tracking
volatile float currentFPS = 0;
volatile unsigned long lastFrameTime = 0;
volatile unsigned long frameCount = 0;
volatile unsigned long avgCaptureTime = 0;
volatile unsigned long avgSendTime = 0;
volatile unsigned long avgFrameSize = 0;

// ===========================
// Camera stream handler
// ===========================
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  // Running averages
  static unsigned long totalCaptureTime = 0;
  static unsigned long totalSendTime = 0;
  static unsigned long totalFrameSize = 0;
  static unsigned long sampleCount = 0;

  while (true) {
    // Measure capture time
    unsigned long captureStart = millis();
    fb = esp_camera_fb_get();
    unsigned long captureEnd = millis();
    unsigned long captureTime = captureEnd - captureStart;

    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if (fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted) {
          Serial.println("JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }

    // Measure send time
    unsigned long sendStart = millis();
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf(part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    unsigned long sendEnd = millis();
    unsigned long sendTime = sendEnd - sendStart;

    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      break;
    }

    // Update timing stats (rolling average)
    totalCaptureTime += captureTime;
    totalSendTime += sendTime;
    totalFrameSize += _jpg_buf_len;
    sampleCount++;

    if (sampleCount >= 10) {
      avgCaptureTime = totalCaptureTime / sampleCount;
      avgSendTime = totalSendTime / sampleCount;
      avgFrameSize = totalFrameSize / sampleCount;
      totalCaptureTime = 0;
      totalSendTime = 0;
      totalFrameSize = 0;
      sampleCount = 0;
    }

    // FPS tracking
    frameCount++;
    unsigned long now = millis();
    if (now - lastFrameTime >= 1000) {
      currentFPS = frameCount * 1000.0 / (now - lastFrameTime);
      frameCount = 0;
      lastFrameTime = now;
    }
  }
  return res;
}

void startStreamServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 81;
  config.ctrl_port = 32769;  // Different control port than default (32768)
  config.recv_wait_timeout = 1;   // 1 second timeout
  config.send_wait_timeout = 1;   // 1 second timeout - kill dead connections fast

  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
  };

  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.println("Stream server started on port 81");
  }
}

// ===========================
// Position persistence functions
// ===========================
void loadPositions() {
  preferences.begin("turret", true);
  horizontalAngle = preferences.getInt("hAngle", HORIZONTAL_CENTER);
  verticalAngle = preferences.getInt("vAngle", VERTICAL_CENTER);
  preferences.end();
  Serial.printf("Loaded positions - H: %d° V: %d°\n", horizontalAngle, verticalAngle);
}

void savePositions() {
  preferences.begin("turret", false);
  preferences.putInt("hAngle", horizontalAngle);
  preferences.putInt("vAngle", verticalAngle);
  preferences.end();
  positionsSaved = true;
  Serial.printf("Saved positions - H: %d° V: %d°\n", horizontalAngle, verticalAngle);
}

// ===========================
// Helper to get query parameter
// ===========================
bool getQueryParam(httpd_req_t *req, const char* key, char* value, size_t maxLen) {
  char query[128];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    if (httpd_query_key_value(query, key, value, maxLen) == ESP_OK) {
      return true;
    }
  }
  return false;
}

// ===========================
// Web handler functions (esp_http_server - non-blocking)
// ===========================
static esp_err_t handle_root(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, htmlPage, strlen(htmlPage));
  return ESP_OK;
}

static esp_err_t handle_move(httpd_req_t *req) {
  char dir[16], state[8];
  if (getQueryParam(req, "dir", dir, sizeof(dir)) &&
      getQueryParam(req, "state", state, sizeof(state))) {
    int st = atoi(state);
    if (strcmp(dir, "up") == 0) movingUp = (st == 1);
    else if (strcmp(dir, "down") == 0) movingDown = (st == 1);
    else if (strcmp(dir, "left") == 0) movingLeft = (st == 1);
    else if (strcmp(dir, "right") == 0) movingRight = (st == 1);
    httpd_resp_send(req, "OK", 2);
  } else {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_send(req, "Missing parameters", -1);
  }
  return ESP_OK;
}

static esp_err_t handle_position(httpd_req_t *req) {
  char json[64];
  snprintf(json, sizeof(json), "{\"h\":%d,\"v\":%d}", horizontalAngle, verticalAngle);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json, strlen(json));
  return ESP_OK;
}

static esp_err_t handle_reset(httpd_req_t *req) {
  preferences.begin("turret", false);
  preferences.clear();
  preferences.end();
  horizontalAngle = HORIZONTAL_CENTER;
  verticalAngle = VERTICAL_CENTER;
  horizontalServo.write(horizontalAngle);
  verticalServo.write(verticalAngle);
  positionsSaved = true;
  positionChanged = false;
  Serial.println("Reset to center");
  httpd_resp_send(req, "Reset complete", -1);
  return ESP_OK;
}

static esp_err_t handle_shoot(httpd_req_t *req) {
  char state[16];
  if (getQueryParam(req, "state", state, sizeof(state))) {
    httpd_resp_set_type(req, "application/json");
    if (strcmp(state, "start") == 0) {
      digitalWrite(RELAY_PIN_SPINNER, LOW);
      spinnerActive = true;
      delay(250);
      digitalWrite(RELAY_PIN_TRIGGER, LOW);
      triggerActive = true;
      Serial.println("FIRING...");
      httpd_resp_send(req, "{\"status\":\"firing\"}", -1);
    } else if (strcmp(state, "stop") == 0) {
      digitalWrite(RELAY_PIN_TRIGGER, HIGH);
      digitalWrite(RELAY_PIN_SPINNER, HIGH);
      triggerActive = false;
      spinnerActive = false;
      Serial.println("STOPPED");
      httpd_resp_send(req, "{\"status\":\"stopped\"}", -1);
    }
  } else {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_send(req, "Missing state parameter", -1);
  }
  return ESP_OK;
}

static esp_err_t handle_led(httpd_req_t *req) {
  char value[16];
  httpd_resp_set_type(req, "application/json");

  if (getQueryParam(req, "brightness", value, sizeof(value))) {
    ledBrightness = atoi(value);
    if (ledBrightness < 0) ledBrightness = 0;
    if (ledBrightness > 255) ledBrightness = 255;
    ledcWrite(LED_FLASH_PIN, ledBrightness);
    Serial.printf("LED brightness: %d\n", ledBrightness);
    char json[32];
    snprintf(json, sizeof(json), "{\"brightness\":%d}", ledBrightness);
    httpd_resp_send(req, json, strlen(json));
  } else if (getQueryParam(req, "state", value, sizeof(value))) {
    if (strcmp(value, "on") == 0) {
      ledBrightness = 64;
      ledcWrite(LED_FLASH_PIN, ledBrightness);
      Serial.println("LED ON (25%)");
      httpd_resp_send(req, "{\"led\":true,\"brightness\":64}", -1);
    } else if (strcmp(value, "off") == 0) {
      ledBrightness = 0;
      ledcWrite(LED_FLASH_PIN, 0);
      Serial.println("LED OFF");
      httpd_resp_send(req, "{\"led\":false,\"brightness\":0}", -1);
    }
  } else {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_send(req, "Missing parameter", -1);
  }
  return ESP_OK;
}

static esp_err_t handle_admin(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, adminPage, strlen(adminPage));
  return ESP_OK;
}

static esp_err_t handle_stats(httpd_req_t *req) {
  sensor_t *s = esp_camera_sensor_get();

  int frameWidth = 0, frameHeight = 0;
  switch(s->status.framesize) {
    case 4: frameWidth = 320; frameHeight = 240; break;
    case 5: frameWidth = 400; frameHeight = 296; break;
    case 6: frameWidth = 480; frameHeight = 320; break;
    case 8: frameWidth = 640; frameHeight = 480; break;
    case 9: frameWidth = 800; frameHeight = 600; break;
    case 10: frameWidth = 1024; frameHeight = 768; break;
    case 12: frameWidth = 1280; frameHeight = 1024; break;
    case 13: frameWidth = 1600; frameHeight = 1200; break;
    default: frameWidth = 640; frameHeight = 480;
  }

  float tempC = temperatureRead();
  char json[384];
  snprintf(json, sizeof(json),
    "{\"rssi\":%d,\"fps\":%.1f,\"framesize\":%d,\"quality\":%d,"
    "\"frameWidth\":%d,\"frameHeight\":%d,\"freeHeap\":%u,\"freePsram\":%u,"
    "\"psramFound\":%s,\"tempC\":%.1f,\"captureMs\":%lu,\"sendMs\":%lu,\"avgFrameKB\":%.1f}",
    WiFi.RSSI(), currentFPS, s->status.framesize, s->status.quality,
    frameWidth, frameHeight, ESP.getFreeHeap(), ESP.getFreePsram(),
    psramFound() ? "true" : "false", tempC, avgCaptureTime, avgSendTime, avgFrameSize / 1024.0);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json, strlen(json));
  return ESP_OK;
}

static esp_err_t handle_config(httpd_req_t *req) {
  sensor_t *s = esp_camera_sensor_get();
  bool changed = false;
  char value[16];

  if (getQueryParam(req, "framesize", value, sizeof(value))) {
    int framesize = atoi(value);
    s->set_framesize(s, (framesize_t)framesize);
    Serial.printf("Set framesize to %d\n", framesize);
    changed = true;
  }

  if (getQueryParam(req, "quality", value, sizeof(value))) {
    int quality = atoi(value);
    s->set_quality(s, quality);
    Serial.printf("Set quality to %d\n", quality);
    changed = true;
  }

  if (changed) {
    preferences.begin("camera", false);
    preferences.putInt("framesize", s->status.framesize);
    preferences.putInt("quality", s->status.quality);
    preferences.end();
    Serial.println("Camera settings saved to flash");
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"success\":true}", -1);
  return ESP_OK;
}

// ===========================
// Start control server (port 80) - runs on Core 0
// ===========================
void startControlServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.core_id = 0;  // Run on Core 0 (WiFi core) - separate from main loop on Core 1

  httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = handle_root };
  httpd_uri_t uri_move = { .uri = "/move", .method = HTTP_GET, .handler = handle_move };
  httpd_uri_t uri_position = { .uri = "/position", .method = HTTP_GET, .handler = handle_position };
  httpd_uri_t uri_reset = { .uri = "/reset", .method = HTTP_GET, .handler = handle_reset };
  httpd_uri_t uri_shoot = { .uri = "/shoot", .method = HTTP_GET, .handler = handle_shoot };
  httpd_uri_t uri_led = { .uri = "/led", .method = HTTP_GET, .handler = handle_led };
  httpd_uri_t uri_admin = { .uri = "/admin", .method = HTTP_GET, .handler = handle_admin };
  httpd_uri_t uri_stats = { .uri = "/stats", .method = HTTP_GET, .handler = handle_stats };
  httpd_uri_t uri_config = { .uri = "/config", .method = HTTP_GET, .handler = handle_config };

  if (httpd_start(&control_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(control_httpd, &uri_root);
    httpd_register_uri_handler(control_httpd, &uri_move);
    httpd_register_uri_handler(control_httpd, &uri_position);
    httpd_register_uri_handler(control_httpd, &uri_reset);
    httpd_register_uri_handler(control_httpd, &uri_shoot);
    httpd_register_uri_handler(control_httpd, &uri_led);
    httpd_register_uri_handler(control_httpd, &uri_admin);
    httpd_register_uri_handler(control_httpd, &uri_stats);
    httpd_register_uri_handler(control_httpd, &uri_config);
    Serial.println("Control server started on port 80 (Core 0)");
  }
}

// ===========================
// Setup
// ===========================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\nToy Gun Turret - Step 6: Camera Streaming");

  // Initialize camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;  // 640x480
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;

  // Check for PSRAM
  if (psramFound()) {
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.fb_count = 2;  // Double buffer for smoother streaming
    Serial.println("PSRAM found, using for frame buffer");
  } else {
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.frame_size = FRAMESIZE_QVGA;  // Smaller if no PSRAM
    config.jpeg_quality = 20;  // More compression for faster streaming
    config.fb_count = 1;  // Single buffer to save memory
    config.grab_mode = CAMERA_GRAB_LATEST;  // Always get latest frame
    Serial.println("No PSRAM, using DRAM with single buffer");
  }

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
  } else {
    Serial.println("Camera initialized successfully");

    // Load saved camera settings
    preferences.begin("camera", true);
    int savedFramesize = preferences.getInt("framesize", 8);  // Default VGA
    int savedQuality = preferences.getInt("quality", 12);     // Default 12
    preferences.end();

    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, (framesize_t)savedFramesize);
    s->set_quality(s, savedQuality);
    Serial.printf("Loaded camera settings - framesize: %d, quality: %d\n", savedFramesize, savedQuality);
  }

  // Load saved positions
  loadPositions();

  // Attach servos
  horizontalServo.attach(SERVO_PIN_HORIZONTAL);
  verticalServo.attach(SERVO_PIN_VERTICAL);
  horizontalServo.write(horizontalAngle);
  verticalServo.write(verticalAngle);

  // Initialize relay pins
  pinMode(RELAY_PIN_SPINNER, OUTPUT);
  pinMode(RELAY_PIN_TRIGGER, OUTPUT);
  digitalWrite(RELAY_PIN_SPINNER, HIGH);
  digitalWrite(RELAY_PIN_TRIGGER, HIGH);

  // Initialize flash LED with PWM (reduces power draw vs full ON)
  ledcAttach(LED_FLASH_PIN, LED_PWM_FREQ, LED_PWM_RESOLUTION);
  ledcWrite(LED_FLASH_PIN, 0);

  Serial.println("Servos, relays, and LED initialized");

  // Configure static IP
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Static IP configuration failed");
  }

  WiFi.setHostname(hostname);
  WiFi.setSleep(false);  // Disable WiFi power saving - reduces latency
  Serial.printf("Connecting to WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());

    if (MDNS.begin(hostname)) {
      Serial.printf("mDNS: http://%s.local\n", hostname);
    }

    Serial.println("\nAccess points:");
    Serial.printf("  Control UI: http://%s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  Admin: http://%s/admin\n", WiFi.localIP().toString().c_str());
    Serial.printf("  Stream: http://%s:81/stream\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi connection failed!");
  }

  // Start control server (non-blocking, runs on Core 0)
  startControlServer();

  // Start stream server
  startStreamServer();

  Serial.println("\nReady!");
}

// ===========================
// Loop - servo movement only (HTTP servers are non-blocking)
// ===========================
void loop() {
  unsigned long currentTime = millis();

  if (currentTime - lastMoveTime >= MOVE_DELAY) {
    lastMoveTime = currentTime;
    bool moved = false;

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

    if (moved) {
      lastActivityTime = currentTime;
      positionChanged = true;
      positionsSaved = false;
    }
  }

  // Auto-save positions
  if (positionChanged && !positionsSaved && (currentTime - lastActivityTime >= SAVE_DELAY)) {
    savePositions();
    positionChanged = false;
  }
}
