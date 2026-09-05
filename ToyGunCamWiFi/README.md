# Camera and WiFi firmware (ESP32-CAM)

Live camera stream, the web control UI, and the admin panel. Runs on the AI
Thinker ESP32-CAM.

Wired to the 10-pin header, this sketch is a complete turret on its own — it
drives the servos and relays too. Move that header to the WROOM running
[`../ToyGunTurretBLE/`](../ToyGunTurretBLE/) and this board becomes the camera
while the WROOM takes over motion and firing; the servo code here simply stops
having anything attached to it.

Shared hardware — components, power architecture, servo and relay wiring, the
perfboard pinout — is in the [root README](../README.md).

## Build

| Setting | Value |
|---|---|
| Board | AI Thinker ESP32-CAM |
| Partition scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| Core | ESP32 Arduino core 3.x |
| Libraries | ESP32Servo, plus WiFi / ESPmDNS / Preferences from the core |

## Web interface

| Path | Purpose |
|---|---|
| `http://lizardgun3000.local/` | Stream, D-pad, SHOOT, LED slider |
| `/admin` | Resolution, JPEG quality, live stats |
| `:81/stream` | MJPEG stream (the UI points at this itself) |

HTTP API, if you want to drive it from a script or another device:

```
GET /move?dir={up|down|left|right}&state={0|1}
GET /shoot?state={start|stop}
GET /led?brightness={0-255}
GET /position                      -> {"h":90,"v":90}
GET /reset
GET /stats                         -> JSON for the admin panel
GET /config?framesize=<n>&quality=<n>
```

⚠️ Unlike the BLE firmware, this one has **no deadman watchdog**. If the
browser goes away mid-shot — closed tab, dropped WiFi, phone asleep — the
relays stay latched and the gun keeps firing until something stops it.

## Frame rate

Resolution is the whole story. Stream FPS is essentially `1000 / send_time`, and
send time scales with pixels: SXGA is 4.3x the pixels of VGA and lands around
7 fps, while VGA gets 20-30. The admin panel writes your choice to flash, so
whatever you picked last is what boots — the sketch default only applies to a
board that has never been configured.

For aiming, VGA (640x480) is already larger than the video element on screen.

Note that `esp_http_server` is single-threaded and `stream_handler` never
returns, so **only one client is ever served**. A second tab does not halve the
frame rate, it gets nothing at all. If a stale connection is holding the stream,
close every tab, wait about five seconds for the send timeout, then open one.

## Software Setup

### WiFi Configuration

Before uploading code to the ESP32, you need to configure WiFi settings in the Arduino sketch:

**1. WiFi Credentials** (Lines 30-32 in ToyGunCamWiFi.ino):
```cpp
const char* ssid = "YOUR_WIFI_SSID";           // Change to your WiFi name
const char* password = "YOUR_WIFI_PASSWORD";   // Change to your WiFi password
const char* hostname = "toygun";               // Access via http://toygun.local
```

**2. Network Settings** (Lines 39-43):

To find these values on your network:

**On Mac:**
- Go to **System Settings → Network → WiFi → Details**
- Note: **Router** (gateway), **Subnet Mask**, and **DNS Server**

**On iPhone:**
- Go to **Settings → WiFi**
- Tap **(i)** button next to your WiFi network
- Note: **Router**, **Subnet Mask**, and **DNS**

**On Windows:**
- Open **Command Prompt**
- Run: `ipconfig /all`
- Note: **Default Gateway**, **Subnet Mask**, and **DNS Servers**

**Configure in code:**
```cpp
IPAddress local_IP(192, 168, 86, 42);      // Choose any IP ending in 2-254
IPAddress gateway(192, 168, 86, 1);        // Your router IP (from above)
IPAddress subnet(255, 255, 255, 0);        // Subnet mask (from above)
IPAddress primaryDNS(192, 168, 86, 1);     // DNS server (usually same as gateway)
IPAddress secondaryDNS(8, 8, 8, 8);        // Google DNS as backup
```

**3. Accessing the Turret:**

After uploading, the Serial Monitor will show:
```
✓ WiFi connected!
IP address: 192.168.86.42
mDNS responder started: http://toygun.local

Access the turret at:
  http://192.168.86.42
  http://toygun.local
```

Use either:
- **Static IP:** http://192.168.86.42
- **mDNS hostname:** http://toygun.local (works on most devices)

## ESP32-CAM Wiring Diagram

### Programming with FT232RL FTDI Adapter

Use the FT232RL FTDI USB-C to TTL adapter to program the ESP32-CAM.

**Components:**
- FT232RL FTDI USB-C to TTL Adapter: [Amazon B0CQVB6JFV](https://www.amazon.com/dp/B0CQVB6JFV)
- 1N5819 Schottky Diode (for power protection)

### Pin Mapping (ESP32-CAM)

| Function | GPIO | ESP32-CAM Pin Label |
|----------|------|---------------------|
| Horizontal Servo | 12 | IO12 |
| Vertical Servo | 13 | IO13 |
| Trigger Relay | 14 | IO14 |
| Spinner Relay | 15 | IO15 |
| 5V Power In | - | 5V |
| Ground | - | GND |

### Programming Wiring (FT232RL to ESP32-CAM)

```
FT232RL FTDI Adapter              ESP32-CAM
(set jumper to 3.3V!)
──────────────────                ─────────
    GND ─────────────────────────→ GND
    TX  ─────────────────────────→ U0R (GPIO 3)
    RX  ─────────────────────────→ U0T (GPIO 1)
    VCC ─────────────────────────→ 5V (common 5V rail, after diode)

                                           ┌────○────┐
                                  IO0 ─────┤  SWITCH ├───→ GND
                                           └─────────┘

DROK Mini (5.3V) ──►|── 1N5819 ──→ 5V (common 5V rail)
```

**Switch operation:**
- **Switch CLOSED** = Programming mode (IO0 grounded)
- **Switch OPEN** = Normal run mode (IO0 floating)

**Important:**
- Set FTDI voltage jumper to **3.3V** (ESP32 uses 3.3V logic)
- FTDI VCC connects to the common 5V rail (after the diode)
- The diode allows safe power from either source (DROK OR FTDI)
- Close switch before powering on to enter programming mode
- Open switch after programming and reset to run normally

### Complete Wiring Diagram (Normal Operation)

```
                         12V Power Supply
                               │
               ┌───────────────┴───────────────┐
               │                               │
        ┌──────┴──────┐                 ┌──────┴──────┐
        │ DROK Mini   │                 │ DROK Mini   │
        │  @ 6V       │                 │  @ 5.3V     │
        │  (Servos)   │                 │  (ESP32)    │
        └──────┬──────┘                 └──────┬──────┘
               │                               │
            6V │ GND                    5.3V+  │ GND
               │  │                        │   │
               │  │                   1N5819   │
               │  │                     ►|     │
               │  │                        │   │
               │  │              ┌─────────┴───┼──── COMMON 5V RAIL
               │  │              │             │
               │  │         FTDI VCC          │
               │  │              │             │
               │  └──────────────┼─────────────┴──── COMMON GND
               │                 │                        │
    ┌──────────┴──────────┐    ┌─┴────────────────────────┴───────────┐
    │       SERVOS        │    │              ESP32-CAM               │
    │                     │    │                                      │
    │  ┌────────────────┐ │    │  5V ←── Common 5V Rail               │
    │  │ Horizontal     │ │    │  GND ←── Common GND                  │
    │  │ VCC ← 6V DROK  │ │    │                                      │
    │  │ GND ← Common   │ │    │  IO12 ──→ Horizontal Servo Signal    │
    │  │ SIG ───────────────────→ IO12                                │
    │  └────────────────┘ │    │  IO13 ──→ Vertical Servo Signal      │
    │                     │    │  IO14 ──→ Relay IN1 (Trigger)        │
    │  ┌────────────────┐ │    │  IO15 ──→ Relay IN2 (Spinner)        │
    │  │ Vertical       │ │    │                                      │
    │  │ VCC ← 6V DROK  │ │    │  For programming:                    │
    │  │ GND ← Common   │ │    │  U0R ←── FTDI TX                     │
    │  │ SIG ───────────────────→ IO13                                │
    │  └────────────────┘ │    │  U0T ──→ FTDI RX                     │
    │                     │    │  IO0 ──┤○ SWITCH ○├── GND            │
    └─────────────────────┘    └──────────────────────────────────────┘

    ┌─────────────────────┐
    │    2-CH RELAY       │
    │                     │
    │  VCC ←── Common 5V  │
    │  GND ←── Common GND │
    │  IN1 ←── IO14       │
    │  IN2 ←── IO15       │
    └─────────────────────┘
```

### Power Protection with Diode

The 1N5819 Schottky diode allows safe dual power sources (DROK converter AND FTDI):

```
DROK 5.3V ───►|─────┬─────────→ ESP32-CAM 5V
            1N5819  │
                    ├─────────→ Relay VCC
                    │
FTDI VCC ───────────┘
                 (Common 5V Rail)
```

**How it works:**
- **DROK ON:** 5.3V through diode = 5.0V powers everything, diode blocks backflow
- **DROK OFF:** FTDI 5V powers ESP32 for bench programming, diode blocks backflow to DROK

**Setup:**
1. Adjust DROK potentiometer to output **5.3V** (compensates for 0.3V diode drop)
2. Connect diode: anode (no stripe) to DROK OUT+, cathode (stripe) to 5V rail
3. Connect FTDI VCC directly to the common 5V rail (after the diode)

### Programming Steps

1. **Connect FTDI to ESP32-CAM:**
   - FTDI GND → ESP32-CAM GND
   - FTDI TX → ESP32-CAM U0R
   - FTDI RX → ESP32-CAM U0T
   - FTDI VCC → Common 5V Rail (powers ESP32 when DROK is off)

2. **Enter programming mode:**
   - Close the IO0 switch (IO0 → GND)
   - Connect FTDI to USB (powers ESP32 via VCC)

3. **Upload:**
   - Arduino IDE: **Tools → Board → "AI Thinker ESP32-CAM"**
   - Select correct COM port
   - Click Upload

4. **Run normally:**
   - Open the IO0 switch
   - Reset or power cycle ESP32-CAM

### Alternative GPIO Mapping (If Boot Issues)

If you experience boot problems with GPIO 12, use this alternative:

| Function | Original | Alternative |
|----------|----------|-------------|
| Horizontal Servo | GPIO 12 | GPIO 2 (onboard LED will blink) |
| Vertical Servo | GPIO 13 | GPIO 13 (no change) |
| Trigger Relay | GPIO 14 | GPIO 14 (no change) |
| Spinner Relay | GPIO 15 | GPIO 4 (flash LED pin) |

## Programming with a WROOM as the USB-serial adapter

An alternative to the FTDI adapter, using the ESP32-WROOM-32E you already have.
To program an ESP32-CAM using ESP32-WROOM-32E as a USB-to-Serial adapter:

```
ESP32-WROOM-32E              ESP32-CAM              Power Source (DROK Mini)
───────────────              ─────────              ────────────────────────
EN ──────→ GND
TXD0 (GPIO 1) ────────────→  U0R
RXD0 (GPIO 3) ────────────→  U0T
GND ──────────────────────→  GND  ←────────────────  GND
                             5V   ←────────────────  5V OUT
                             IO0 ──→ GND (on CAM)
```

**Connection Summary:**

| Wire | From | To | Purpose |
|------|------|-----|---------|
| 1 | WROOM EN | WROOM GND | Disables ESP32 chip on WROOM |
| 2 | WROOM TXD0 (GPIO 1) | CAM U0R | Serial data TX → RX |
| 3 | WROOM RXD0 (GPIO 3) | CAM U0T | Serial data RX → TX |
| 4 | WROOM GND | CAM GND | Common ground |
| 5 | DROK 5V OUT | CAM 5V | Power to ESP32-CAM |
| 6 | DROK GND | CAM GND | Power ground (shared) |
| 7 | CAM IO0 | CAM GND | Enables programming/flash mode |

**After uploading:** Disconnect IO0 from GND and reset/power-cycle the ESP32-CAM to run normally.
