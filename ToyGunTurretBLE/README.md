# Turret control firmware (ESP32-WROOM-32E)

Motion and firing, driven over Bluetooth LE. Runs on the WROOM board that sits
alongside the ESP32-CAM; the CAM keeps running `../ToyGunCamWiFi/` unchanged for
the camera stream and its web UI.

Splitting the two jobs across two chips means the camera never competes with
control traffic for the radio, and the WROOM has GPIO to spare for whatever
comes next.

## Wiring

The pin numbers are unchanged from `ToyGunCamWiFi.ino`, so the existing 10-pin
perfboard header plugs straight into the WROOM.

⚠️ **Move that header to the WROOM entirely.** If both boards stay wired to the
same servo signal line, two push-pull outputs fight over it — one drives high
while the other drives low, and you lose GPIO pins on one or both.

The two boards share the 5V rail and a common ground, and nothing else.

## Build

| Setting | Value |
|---|---|
| Library | **NimBLE-Arduino** by h2zero, **2.x** (Library Manager) |
| Board | ESP32 Dev Module |
| Core | ESP32 Arduino core 3.x |

NimBLE-Arduino 1.x will not compile this — the callback signatures changed in
2.0 (`onConnect`/`onDisconnect` gained a `NimBLEConnInfo&`, `onWrite` too).

## GATT interface

Advertised as **`LizardGun3000`**, exposing the Nordic UART service:

| Role | UUID |
|---|---|
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX — write / write-no-response | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX — notify | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

Nordic UART rather than a custom UUID so any generic client already speaks it.

### Commands (ASCII, written to RX)

| Command | Effect |
|---|---|
| `U1` / `U0` | Tilt up — press / release |
| `D1` / `D0` | Tilt down |
| `L1` / `L0` | Pan left |
| `R1` / `R0` | Pan right |
| `F1` / `F0` | Fire — spinner, 250 ms spin-up, then trigger / stop both |
| `C` | Center servos and clear the saved position |
| `P` | Request a position notification |
| `B<0-255>` | Brightness on the spare GPIO 4 PWM output, e.g. `B64` |
| `K` | Keepalive — refreshes the watchdog, changes nothing |

Several commands may share one write, separated by spaces or newlines.

### Notifications (from TX)

```
POS <h> <v>      position, during movement and on demand
FIRE <0|1>
LED <0-255>
ERR <char>       unrecognized command
OK watchdog      the deadman timer just cut the outputs
```

## Deadman watchdog

This launches projectiles and BLE links drop without warning, so:

- **Any disconnect** stops firing and all movement immediately.
- Movement and firing **auto-cancel after 1500 ms of radio silence**.

A client holding a button must resend that command — or `K` — about every
500 ms. A bare `F1` with nothing following it stops on its own after 1.5 s.

## Testing without a Flipper

Do this first; it separates turret bugs from Flipper bugs.

1. Install **nRF Connect** (or LightBlue) on a phone.
2. Scan, connect to `LizardGun3000`.
3. Enable notifications on the TX characteristic (`...0003`).
4. Write `P` to RX (`...0002`) as text — you should get `POS 90 90` back.
5. Write `R1`, wait, write `R0`. The turret should pan right and stop.

Keep darts out of the magazine until movement is confirmed.

## Flipper Zero

The turret is a **BLE peripheral**, so the Flipper has to act as **central** —
scan, connect, discover the Nordic UART service, and write to the RX
characteristic. That is the open question in this design: the Flipper's
official firmware is built around being a *peripheral* (BLE Remote / HID,
Flipper Serial), and GATT-client support has historically lived in third-party
firmware. Confirm your firmware branch can act as a central before writing an
app against this.

**The Video Game Module cannot be used for this.** The VGM is built on a
Raspberry Pi RP2040, which has no radio at all — no BLE, no WiFi. It is a DVI
video output plus an ICM-42688-P 6-axis IMU, and it talks to the Flipper over
the GPIO header. Its own 14-pin breakout only helps if you are willing to run a
wire to the turret.

The Flipper accessory with WiFi is the separate **WiFi Devboard** (ESP32-S2).
With FlipperHTTP flashed to that, a Flipper app can drive the existing HTTP
endpoints in `ToyGunCamWiFi.ino` (`/move`, `/shoot`, `/led`, `/position`) with
no changes to that firmware and no BLE involved.

## Security

The link is open — no pairing, no passkey. Anyone in BLE range who knows the
protocol can drive the turret. Fine on a workbench; worth adding
`NimBLEDevice::setSecurityAuth()` and a static passkey before leaving it
powered up unattended.
