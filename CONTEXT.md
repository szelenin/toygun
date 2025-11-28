# Toy Gun Turret - Project Context & Status

**Last Updated:** 2025-11-28
**Current Step:** Step 4c - Press-and-Hold Trigger (COMPLETE)

---

## Project Overview

Building a WiFi-controlled auto-targeting toy gun turret using:
- **Gun:** XSHOT Insanity Motorized Rage Fire (belt-fed flywheel blaster)
- **Microcontroller:** ESP32-WROOM-32E with USB-C (currently), migrating to ESP32-CAM later
- **Control:** Web interface with directional servo control + shoot button
- **Servos:** 2× DS3235 35KG servos for pan/tilt
- **Firing:** 2-channel relay module controlling spinner and trigger motors

---

## Current Status: FULLY FUNCTIONAL ✅

### Completed Steps:

**Step 1:** ✅ Basic servo control at 90 degrees
**Step 2:** ✅ Servo movement patterns
**Step 3a:** ✅ WiFi webserver with static IP and mDNS
**Step 3b+c:** ✅ Directional UI with keydown/keyup control
**Step 3d:** ✅ Position persistence (saves to flash)
**Step 4a:** ✅ 2-relay control code (GPIO 14: trigger, GPIO 15: spinner)
**Step 4b:** ✅ Shoot button with automatic sequencing
**Step 4c:** ✅ Press-and-hold trigger mode (continuous firing)

### Remaining Steps:

**Step 5:** 🔲 Migrate code to ESP32-CAM
**Step 6:** 🔲 Integrate camera streaming into UI
**Step 7:** 🔲 Final UI - video stream + controls + shoot button

---

## Hardware Configuration

### GPIO Pin Assignments:
- **GPIO 12:** Horizontal servo (0° to 180°)
- **GPIO 13:** Vertical servo (75° to 100°)
- **GPIO 14:** Relay 1 - Trigger motor
- **GPIO 15:** Relay 2 - Spinner motor (flywheels)

### Power System:
```
[12V 6A Wall Adapter]
    │
    ├──> [YRDZXG 12V→6V Buck, 10A] ──> Servos (6V)
    │
    └──> [LM2596 12V→5V Buck, 3A] ──> ESP32 + Relays (5V)
                                       (Can also power ESP32 via USB-C)

⚠️ IMPORTANT: All grounds must be connected (common ground)
```

### Relay Configuration:
- **Active-LOW relays:** HIGH = OFF, LOW = ON
- **Wiring (user's actual connection):**
  - GPIO 14 → Trigger motor
  - GPIO 15 → Spinner motor

### Network Configuration:
- **SSID:** Edgar
- **Password:** Password!23
- **Static IP:** 192.168.86.42
- **Hostname:** lizardgun3000.local
- **Gateway/DNS:** 192.168.86.1

---

## Current Code Features

### Web Interface:
- **Directional Controls:** Press and hold ▲▼◀▶ buttons to move servos
- **Shoot Button:** Press and hold to fire continuously, release to stop instantly
- **Reset Button:** Clear saved positions and return to center (90°)
- **Real-time Position Display:** Updates every 50ms

### Firing Sequence:
```
Press SHOOT:
1. Spinner ON (flywheels activate)
2. Wait 250ms (spin-up time)
3. Trigger ON (feed darts continuously)
4. Keep firing while button held

Release SHOOT:
5. Trigger OFF (immediate)
6. Spinner OFF (immediate)
```

### Position Persistence:
- Servo positions saved to ESP32 flash memory
- Auto-save 3 seconds after last movement
- Survives power cycles
- Prevents flash wear with delayed save

---

## Known Issues & Current Problem

### 🔴 ACTIVE ISSUE: Not All Bullets Firing

**Symptoms:**
- Some darts fire, others don't
- Inconsistent firing when holding trigger

**Suspected Causes:**
1. **Trigger motor needs pulsing (MOST LIKELY)**
   - Belt-fed blasters need pusher to cycle (push → return → push)
   - Current code keeps trigger ON continuously
   - Pusher can't return to grab next dart

2. **Insufficient spin-up time**
   - Current: 250ms
   - May need: 400-500ms for full flywheel speed

3. **Physical gun issues**
   - Belt feeding mechanism
   - Dart quality/alignment
   - Flywheel alignment

**Proposed Solutions:**

**Option 1: Implement Trigger Pulsing (RECOMMENDED)**
- Modify `handleShoot()` to pulse trigger: ON 150ms → OFF 100ms (repeat)
- Keep spinner running continuously while button held
- Allows pusher mechanism to cycle properly

**Option 2: Increase Spin-up Delay**
- Change `delay(250)` to `delay(500)` in handleShoot() line 551

**Option 3: Check Physical Issues**
- Inspect belt feeding
- Check dart quality
- Verify both flywheels spinning

**Next Action:** User to decide which solution to implement first

---

## File Structure

```
/Users/szelenin/projects/toygun/
├── README.md                           # Full documentation
├── CONTEXT.md                          # This file (current status)
├── ToyGunControl/
│   └── ToyGunControl.ino              # Main Arduino code (Step 4c)
└── images/
    ├── image1.png - image4.png        # 3D model renders
    ├── IMG_6874.jpg - IMG_6880.jpg    # Assembly photos
    └── (relay annotation images removed - were incorrect)
```

---

## Git Repository

**Location:** github.com:szelenin/toygun.git
**Branch:** main
**Last Commit:** Step 4c - Press-and-hold trigger mode

### Recent Commits:
- `4950b81` - Step 4b: Add shoot button with automatic firing sequence
- `cbb98ce` - Step 4a: Add 2-relay control for firing mechanism
- `e1ce247` - Remove incorrect relay wiring diagrams
- Earlier: Steps 1-3d (servo control, WiFi, UI, persistence)

---

## Key Technical Details

### Servo Movement:
- **Speed:** 1° per 40ms (25°/second)
- **Vertical range:** 75° to 100° (restricted lower limit)
- **Horizontal range:** 0° to 180° (full rotation)
- **Direction:** Vertical servo reversed (up decreases angle, down increases)

### Relay Control:
- **Active-LOW logic:** Must set HIGH to turn OFF, LOW to turn ON
- **Initial state:** Both relays OFF (HIGH) on startup
- **Manual control:** `/relay?spinner=on&trigger=off` (still available)

### Web API Endpoints:
- `GET /` - Main web interface
- `GET /move?dir={up|down|left|right}&state={0|1}` - Servo control
- `GET /position` - Returns JSON: `{"h":90,"v":90}`
- `GET /reset` - Clear saved positions, return to 90°
- `GET /relay?spinner={on|off}&trigger={on|off}` - Manual relay control
- `GET /shoot?state={start|stop}` - Press-and-hold firing

### Serial Debug Output:
```
Toy Gun Turret - Step 4c: Press-and-Hold Trigger

Relay pins initialized (GPIO 14: Trigger, GPIO 15: Spinner)

✓ WiFi connected!
IP address: 192.168.86.42
mDNS responder started: http://lizardgun3000.local

🎯 TRIGGER PRESSED - Starting fire
  ⚙️  Spinner: ON (flywheels spinning up)
  🔫 Trigger: ON (feeding darts)
  🔥 FIRING CONTINUOUSLY...

🛑 TRIGGER RELEASED - Stopping fire
  🔫 Trigger: OFF
  ⚙️  Spinner: OFF
✅ FIRING STOPPED
```

---

## Future Hardware Upgrades

### Step 5: ESP32-CAM Migration
**Ordered:** ESP32-CAM module + ESP32-CAM-MB programmer (Amazon B0948ZFTQZ)

**Changes needed:**
- Port code from ESP32-WROOM-32E to ESP32-CAM
- Verify GPIO compatibility
- Test camera functionality
- May need different power requirements

### Permanent Build Considerations

**Recommended approach:** Hybrid build
1. ESP32 screw terminal breakout board (~$10)
2. Mount buck converters on small perfboard
3. Screw terminals for all motor/servo connections
4. 3D printed enclosure

**Alternatives:**
- Full perfboard build (more work, cheaper)
- Custom PCB (most professional, ~$25)
- Keep breadboard in enclosure (easiest)

**Wiring recommendations:**
- 18 AWG for power (servos, motors)
- 22 AWG for signals (GPIO)
- Different wire colors per voltage
- Label everything

---

## Development Environment

**Arduino IDE Settings:**
- **Board:** ESP32 Dev Module
- **Upload Speed:** 115200
- **Flash Frequency:** 80MHz
- **Flash Size:** 4MB

**Libraries Used:**
- ESP32Servo
- WiFi (ESP32 core)
- WebServer (ESP32 core)
- ESPmDNS (ESP32 core)
- Preferences (ESP32 core)

**Required CH341 driver:** Installed for USB-C communication

---

## Important Notes

1. **Relays are active-LOW** - Always use HIGH for OFF, LOW for ON
2. **Common ground is critical** - USB GND, buck converter GND, and 12V PSU GND must be connected
3. **External power required for servos** - Never power servos from ESP32 (too much current)
4. **Position persistence uses flash** - Limited write cycles (~100k), hence 3-second save delay
5. **Hostname changed** - User changed from "toygun" to "lizardgun3000"
6. **Spinner must start before trigger** - Flywheels need to spin up first (250ms delay)
7. **Vertical servo direction reversed** - Up button decreases angle, down button increases

---

## Testing & Validation

### ✅ Tested & Working:
- WiFi connection and static IP
- mDNS hostname (lizardgun3000.local)
- Servo control (all 4 directions)
- Position persistence across reboots
- Relay control (manual via /relay endpoint)
- Press-and-hold trigger firing
- Instant stop on button release
- Visual feedback in UI
- Serial debug logging

### ⚠️ Partially Working:
- **Firing mechanism:** Works but misses some darts (needs trigger pulsing fix)

### 🔲 Not Yet Tested:
- ESP32-CAM compatibility
- Camera streaming
- Long-term reliability
- Permanent build durability

---

## Next Session Action Items

1. **Fix firing issue:**
   - [ ] Decide: Try longer spin-up (500ms) OR implement trigger pulsing
   - [ ] Test with modifications
   - [ ] Adjust timing based on results

2. **When ESP32-CAM arrives:**
   - [ ] Start Step 5: Port code to ESP32-CAM
   - [ ] Verify GPIO pin compatibility
   - [ ] Test basic servo/relay functionality
   - [ ] Add camera initialization

3. **Future enhancements:**
   - [ ] Add camera streaming (Step 6)
   - [ ] Integrate video into web UI (Step 7)
   - [ ] Plan permanent build (perfboard/enclosure)
   - [ ] Add IR thermal camera for auto-targeting

---

## Code Snippet Reference

### Current handleShoot() Function (Line 534):
```cpp
void handleShoot() {
  if (server.hasArg("state")) {
    String state = server.arg("state");

    if (state == "start") {
      // Activate spinner
      digitalWrite(RELAY_PIN_SPINNER, LOW);  // LOW = ON
      spinnerActive = true;

      delay(250);  // Spin-up time ← MAY NEED TO INCREASE

      // Activate trigger
      digitalWrite(RELAY_PIN_TRIGGER, LOW);
      triggerActive = true;

    } else if (state == "stop") {
      // Immediate stop
      digitalWrite(RELAY_PIN_TRIGGER, HIGH);  // HIGH = OFF
      digitalWrite(RELAY_PIN_SPINNER, HIGH);
      triggerActive = false;
      spinnerActive = false;
    }
  }
}
```

### For Trigger Pulsing (NOT YET IMPLEMENTED):
Would need to modify to pulse trigger ON/OFF while button held:
- Keep spinner running continuously
- Pulse trigger: 150ms ON → 100ms OFF → repeat
- Stop when user releases button

---

## Useful Commands

**Upload code:**
```bash
# Open Arduino IDE, select ESP32 Dev Module, click Upload
```

**Test relay manually:**
```bash
curl "http://192.168.86.42/relay?spinner=on"
curl "http://192.168.86.42/relay?trigger=on"
curl "http://192.168.86.42/relay?spinner=off&trigger=off"
```

**Check ESP32 serial output:**
```bash
# Arduino IDE → Tools → Serial Monitor (115200 baud)
```

**Access web interface:**
```bash
open http://192.168.86.42
# or
open http://lizardgun3000.local
```

**Git commands:**
```bash
git status
git add .
git commit -m "Your message"
git push
```

---

## Questions to Answer Next Session

1. Which solution to try first for firing issue?
2. Should we implement trigger pulsing or just increase delay?
3. How long to wait before migrating to ESP32-CAM?
4. What enclosure design for permanent build?
5. Need help with trigger pulsing implementation?

---

**End of Context Document**
*Resume from here in next session*
