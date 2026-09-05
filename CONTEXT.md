# Toy Gun Turret — Project Context

**Last updated:** 2026-09-05

Current status and what's next. Hardware details live in the
[root README](README.md), per-board details in each sketch's own README, and the
BLE command set in [PROTOCOL.md](ToyGunTurretBLE/PROTOCOL.md). This file does not
repeat them.

---

## What this is

A WiFi/BLE controlled toy gun turret built on an XSHOT Insanity Motorized Rage
Fire, with two DS3235 servos for pan/tilt and a 2-channel relay driving the
blaster's spinner and trigger motors. Kid's idea; the long-term goal is
auto-targeting from a thermal camera.

## Architecture

Two ESP32 boards on the gun, one job each. They share the 5V rail and a common
ground, and nothing else.

| Board | Sketch | Job |
|---|---|---|
| ESP32-CAM (AI Thinker) | `ToyGunCamWiFi/` | Camera stream, web UI, admin panel |
| ESP32-WROOM-32E | `ToyGunTurretBLE/` | Servos and firing relays over BLE |

The 10-pin perfboard header carries servos and relays and uses the same GPIO
numbers on both boards, so it plugs into either one.

⚠️ **It connects to exactly one board at a time.** Two push-pull outputs on the
same servo signal line will destroy GPIO pins.

Wired to the CAM, `ToyGunCamWiFi` alone is a complete, working turret — that is
the fallback whenever the BLE side is mid-debug.

## Hardware configuration

| GPIO | Function |
|---|---|
| 12 | Horizontal servo (pan), 0–180° |
| 13 | Vertical servo (tilt), 75–100°, direction reversed |
| 14 | Relay 1 — trigger motor |
| 15 | Relay 2 — spinner motor (flywheels) |

Relays are **active LOW**: HIGH = off, LOW = on, both driven HIGH at boot.

Power: 12V 6A wall adapter into two DROK Mini buck converters — one at 6V for
the servos, one at 5.3V through a 1N5819 into the common 5V rail for the boards
and relays. All grounds common. Servos never draw from an ESP32.

GPIO 12 is a strapping pin on both boards; if a board won't boot with a servo
attached, that's why.

## Network

SSID `Edgar`, DHCP, reachable at **http://lizardgun3000.local**. Credentials are
in `ToyGunCamWiFi.ino`.

## Status

**Working:**

- Servo control, both axes, press-and-hold, with position persistence in flash
- Firing sequence — spinner on, 250 ms spin-up, trigger on; both cut on release
- Camera streaming on port 81, web UI, admin panel with live stats and
  resolution/quality control saved to flash
- Everything above runs today on the ESP32-CAM

**Written but never compiled:**

- `ToyGunTurretBLE` — NimBLE peripheral firmware for the WROOM. NimBLE-Arduino
  is not installed yet, so this has not been through a compiler even once.
  Expect a few errors on first build.

**In progress:**

- Flipper Zero app, BLE central side

## Who's doing what

| Side | Owner |
|---|---|
| Flipper Zero app | son |
| WROOM firmware, wiring, servos, trigger | dad |

The interface between them is frozen in
[PROTOCOL.md](ToyGunTurretBLE/PROTOCOL.md) — v1, with a `V` handshake command so
a version mismatch fails cleanly instead of behaving oddly. Either side can be
tested alone against nRF Connect.

## Open items

1. **Confirm the Flipper can act as a BLE central.** Official firmware is built
   around being a peripheral; GATT-client support has lived in third-party
   branches. Everything on the BLE path depends on this. Fallback if it can't:
   infrared — a TSOP38238 on the WROOM and saved NEC codes in the Flipper's
   stock Infrared app, which needs no custom Flipper app at all.
2. **Build and flash `ToyGunTurretBLE`** — install NimBLE-Arduino 2.x first.
3. **Move the 10-pin header** from the CAM to the WROOM, and disconnect it from
   the CAM completely.
4. **Set camera resolution to VGA.** It is currently on SXGA, which caps the
   stream near 7 fps; VGA gets 20–30. The admin panel's choice persists in flash,
   so this survives reflashing.
5. **`ToyGunCamWiFi` has no deadman watchdog.** If the browser goes away
   mid-shot the relays stay latched and it keeps firing. The BLE firmware cuts
   everything after 1500 ms of silence; the WiFi one should too.
6. *Cosmetic:* the CAM's web UI keeps D-pad and SHOOT buttons that do nothing
   once the header moves to the WROOM.

## Resolved

- **Not all darts firing** — fixed. Note that the firmware still uses a 250 ms
  spin-up followed by a continuously held trigger; no pulsing was added.
- **Control requests stalling the camera stream** — fixed by moving the control
  server off blocking `WebServer` onto `esp_http_server` and giving the two
  servers separate control ports.
