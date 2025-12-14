# Auto-Targeting Toy Gun Turret

## Project Overview

My kid had an idea for a project. He proposed to put a targeting system on a toy automatic gun. The targeting system uses an infrared camera that looks for targets. As soon as somebody appears in the camera, the gun should move towards the target and start shooting.

## Toy Selection

We went to Walmart and after some search we found what we need. Here is the gun we chose for our project:

![XSHOT Insanity Motorized Rage Fire](images/image1.png)

## Project Implementation Idea

We decided to use 2 servo motors for moving the gun:
- **Horizontal servo**: Moves the gun left/right (horizontal rotation)
- **Vertical servo**: Moves the gun up/down (vertical tilt)

**Control System:**
- Infrared camera mounted on the aim for target detection
- Servos mounted on tripod to move the gun
- Relay to control trigger press
- ESP32 microcontroller for control

## 3D Printed Parts

To mount servos to the tripod we need to print 3 models:

### 1. Horizontal Rotation Cylinder
This cylinder will rotate the gun horizontally:

![Horizontal rotation cylinder](images/image2.png)

### 2. Bottom Servo Mount
This will be used to mount the servo to the bottom of the gun. The cylinder will rotate the gun:

![Bottom servo mount](images/image3.png)

### 3. Vertical Servo Mount
This will mount another servo to move the gun vertically:

![Vertical servo mount](images/image4.png)

## Hardware Components

### Core Components

| Component | Description | Link |
|-----------|-------------|------|
| **Toy Gun** | XSHOT Insanity Motorized Rage Fire | [Walmart](https://www.walmart.com/ip/ZURU-X-Shot-Insanity-Motorized-Rage-Fire-72-Darts-for-Ages-8-up/1938663272) |
| **IR Camera** | AMG8833 IR 8×8 Thermal Imager Array Temperature Sensor Module | [Link](https://www.blogger.com/blog/post/edit/24004232/1311660424994822752#) |
| **Servo Motors** | Dsservo Waterproof Servo DS3235 35KG (×2) | [Link](https://www.blogger.com/blog/post/edit/24004232/1311660424994822752#) |
| **Relay Module** | DC 5V 12V 24V Relay Module with Optocoupler | [Link](https://www.blogger.com/blog/post/edit/24004232/1311660424994822752#) |

### Electronics & Power

| Component | Description | Amazon Link |
|-----------|-------------|-------------|
| **ESP32 DevKit** | 2×ESP32-WROOM-32E Module USB-C 4MB | [Amazon B0D6BH4K9B](https://www.amazon.com/dp/B0D6BH4K9B) |
| **ESP32-CAM** | 2×ESP32-CAM with OV2640 Camera + MB Programmer Board | [Amazon B0948ZFTQZ](https://www.amazon.com/dp/B0948ZFTQZ) |
| **6V Buck Converter** | YRDZXG 12V to 6V 10A 60W Waterproof (for servos) | [Amazon B0CSPTCP5L](https://www.amazon.com/dp/B0CSPTCP5L) |
| **5V Buck Converter** | LM2596 DC-DC Step Down Module (for ESP32) | [Amazon B0D7ZT7KPH](https://www.amazon.com/dp/B0D7ZT7KPH) |
| **12V Power Supply** | 12V 6A 72W Wall Adapter with 5.5mm×2.1mm barrel jack | [Amazon B08ZC7J3BZ](https://www.amazon.com/dp/B08ZC7J3BZ) |
| **Soldering Iron** | YIHUA 926 III 60W Digital Soldering Station Kit | [Amazon B082F1WKP9](https://www.amazon.com/dp/B082F1WKP9) |
| **Jumper Wires** | Breadboard jumper wire kit | Required for prototyping |

### Optional Battery Setup (For Portable Operation)

| Component | Description |
|-----------|-------------|
| **Battery** | 2S LiPo 7.4V 2200mAh (×2 recommended) |
| **Charger** | LiPo Balance Charger |
| **Safety Bag** | LiPo fireproof storage bag |

## Power System Architecture

```
Wall Power Option (Stationary):
================================
[12V 6A Wall Adapter]
    │
    ├──> [YRDZXG 12V→6V Buck] ──> Servos (6V, up to 10A)
    │
    └──> [LM2596 12V→5V Buck] ──> ESP32 + Relay + Camera (5V)

Battery Option (Portable):
===========================
[2S LiPo 7.4V]
    │
    ├──> [6V 10A BEC] ──> Servos
    │
    └──> [5V Buck Converter] ──> ESP32 + Relay + Camera
```

**Important:** All grounds must be connected to a common ground!

## Software Setup

### WiFi Configuration

Before uploading code to the ESP32, you need to configure WiFi settings in the Arduino sketch:

**1. WiFi Credentials** (Lines 30-32 in ToyGunControl.ino):
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

## Wiring Connections

### Servo Connections
- **Servo 1 (Horizontal):**
  - Red (Power) → 6V from buck converter
  - Brown (GND) → Common ground
  - Orange (Signal) → ESP32 GPIO 12

- **Servo 2 (Vertical):**
  - Red (Power) → 6V from buck converter
  - Brown (GND) → Common ground
  - Orange (Signal) → ESP32 GPIO 13

### Relay Connections (2 relays for firing mechanism)
- **Relay 1 (Spinner Motor):**
  - VCC → 5V from LM2596
  - GND → Common ground
  - Signal → ESP32 GPIO 14

- **Relay 2 (Trigger):**
  - VCC → 5V from LM2596
  - GND → Common ground
  - Signal → ESP32 GPIO 15

### ESP32 Power
- Vin → 5V from LM2596
- GND → Common ground

## ESP32-CAM Wiring Diagram

When using ESP32-CAM instead of ESP32-WROOM-32E, use this wiring configuration.

**Important Notes:**
- GPIO 12 must be LOW at boot (servo signal is LOW when idle, so this works)
- Not using SD card slot (pins repurposed for servos/relays)
- Camera uses internal pins (no conflict with our GPIOs)

### Pin Mapping (ESP32-CAM)

| Function | GPIO | ESP32-CAM Pin Label |
|----------|------|---------------------|
| Horizontal Servo | 12 | IO12 |
| Vertical Servo | 13 | IO13 |
| Trigger Relay | 14 | IO14 |
| Spinner Relay | 15 | IO15 |
| 5V Power In | - | 5V |
| Ground | - | GND |

### ESP32-CAM Wiring Diagram

```mermaid
flowchart LR
    subgraph Power
        LM[LM2596 5V Out]
        CG[Common Ground]
    end

    subgraph ESP32-CAM
        V5[5V]
        GND[GND]
        IO12[IO12]
        IO13[IO13]
        IO14[IO14]
        IO15[IO15]
    end

    subgraph Servos
        HS[Horizontal Servo Signal]
        VS[Vertical Servo Signal]
    end

    subgraph Relays
        R1[Trigger Relay IN1]
        R2[Spinner Relay IN2]
    end

    LM --> V5
    CG --> GND
    IO12 --> HS
    IO13 --> VS
    IO14 --> R1
    IO15 --> R2
```

### Alternative GPIO Mapping (If Boot Issues)

If you experience boot problems with GPIO 12, use this alternative:

| Function | Original | Alternative |
|----------|----------|-------------|
| Horizontal Servo | GPIO 12 | GPIO 2 (onboard LED will blink) |
| Vertical Servo | GPIO 13 | GPIO 13 (no change) |
| Trigger Relay | GPIO 14 | GPIO 14 (no change) |
| Spinner Relay | GPIO 15 | GPIO 4 (flash LED pin) |

## Assembly Photos

### Electronics and Breadboard Setup

![Breadboard with ESP32 and Buck Converters](images/IMG_6874.jpg)
*Breadboard setup showing ESP32-WROOM-32E, LM2596 (5V) and YRDZXG (6V) buck converters, with servo connections*

![Close-up of Control Board](images/IMG_6875.jpg)
*Close-up view of the control electronics - ESP32 and power regulation circuit*

### Servo Mounting System

![Vertical Servo Mount - Side View](images/IMG_6878.jpg)
*3D printed vertical servo mount attached to tripod with DS3235 servo*

![Horizontal Rotation Mechanism](images/IMG_6879.jpg)
*Horizontal servo controlling left/right rotation with 3D printed mounting bracket*

![Servo Mount Detail](images/IMG_6880.jpg)
*Detail of dual-servo mounting system showing vertical tilt mechanism*

### Fully Assembled Turret

![Complete Assembly on Tripod](images/IMG_6877.jpg)
*Fully assembled toy gun turret on tripod with WiFi-controlled servo system*

## Assembly Steps

1. **3D Print Parts**
   - Print horizontal rotation cylinder
   - Print bottom servo mount
   - Print vertical servo mount

2. **Mount Servos**
   - Attach servos to 3D printed parts
   - Mount assembly to tripod
   - Attach gun to servo assembly

3. **Wire Electronics**
   - Connect power supplies (buck converters)
   - Wire servos to power and ESP32
   - Connect relay to gun trigger mechanism
   - Mount IR camera

4. **Program ESP32**
   - Upload control code
   - Configure IR camera targeting
   - Test servo movements
   - Calibrate targeting system

## Resources

- [How to connect the gun to the relay switch](https://www.blogger.com/blog/post/edit/24004232/1311660424994822752#) (YouTube)

## Power Specifications

| Component | Voltage | Current Draw |
|-----------|---------|--------------|
| DS3235 Servo (×2) | 6V | Up to 2.1A each (4.2A total) |
| ESP32-WROOM-32E | 5V | 250-500mA |
| Relay Module | 5V | 70mA |
| IR Camera | 5V | 100-200mA |
| **Total Peak** | - | ~5A |

## Safety Notes

⚠️ **Important Safety Information:**

1. **Never power servos from ESP32** - They draw too much current and will damage the board
2. **Always use common ground** - All components must share the same ground connection
3. **LiPo battery safety** - Store in fireproof bag, never over-discharge below 3.2V per cell
4. **Voltage checks** - Use multimeter to verify voltages before connecting components
5. **Toy gun safety** - Only use in controlled environment, never aim at people without eye protection

lizard rush was here

## Hardware Reference Documentation

### ESP32-WROOM-32E Pinout & Schematic

Reference documentation for the ESP32-WROOM-32E development board.

#### Programming Guide & Resources

![ESP32-WROOM Programming Guide](images/ESP32-WROOM-guide.jpeg)
*Technical support links, driver download info, and important soldering notes*

#### Board Schematic

![ESP32-WROOM-32E Schematic](images/ESP32-WROOM-schematic.jpeg)
*Full schematic showing USB-to-TTL (CH340K), power regulation, GPIO headers, and ESP32 module connections*

#### Key Pins Reference (ESP32-WROOM-32E)

| Pin Label | GPIO | Function | Notes |
|-----------|------|----------|-------|
| **TXD0** | GPIO 1 | Serial TX | Used for programming/debug |
| **RXD0** | GPIO 3 | Serial RX | Used for programming/debug |
| **EN** | - | Enable/Reset | LOW = chip disabled, HIGH = chip enabled |
| **IO0** | GPIO 0 | Boot Mode | LOW at boot = programming mode |
| **3V3** | - | 3.3V Output | Regulated power output |
| **VIN/5V** | - | 5V Input | Direct from USB |
| **GND** | - | Ground | Common ground |

#### Using ESP32-WROOM-32E as USB-Serial Programmer

To program an ESP32-CAM using ESP32-WROOM-32E as a USB-to-Serial adapter:

```
ESP32-WROOM-32E              ESP32-CAM              Power Source (LM2596)
───────────────              ─────────              ─────────────────────
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
| 5 | LM2596 5V OUT | CAM 5V | Power to ESP32-CAM |
| 6 | LM2596 GND | CAM GND | Power ground (shared) |
| 7 | CAM IO0 | CAM GND | Enables programming/flash mode |

**After uploading:** Disconnect IO0 from GND and reset/power-cycle the ESP32-CAM to run normally.
