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

## How it connects

The Flipper cannot act as a BLE central — official firmware exports no scanning
or GATT client API at all. So the roles are inverted from the obvious ones:

- **Flipper** = peripheral, running its built-in Serial profile
- **Turret** = central. It scans, finds the Flipper, connects, pairs, and
  subscribes to the Flipper's TX characteristic.

Commands arrive as ASCII on that characteristic; telemetry is written back to
the Flipper's RX characteristic. Full UUIDs, the app-side API calls, and the
command set are in [PROTOCOL.md](PROTOCOL.md).

Pairing is bonded and happens once: the Flipper shows a six-digit code, you type
it into the turret's serial monitor, and the bond is stored in NVS.

## Deadman watchdog

This launches projectiles and BLE links drop without warning, so:

- **Any disconnect** stops firing and all movement immediately.
- Movement and firing **auto-cancel after 1500 ms of radio silence**.

A client holding a button must resend that command — or `K` — about every
500 ms. A bare `F1` with nothing following it stops on its own after 1.5 s.

## Testing without a Flipper

Do this first; it separates turret bugs from Flipper bugs. The firmware accepts
the same commands over USB serial.

1. Open the Serial Monitor at **115200**, line ending Newline.
2. Type `V` — expect `VER 1 LizardGun3000`.
3. Type `P` — expect `POS 90 90`.
4. Type `R1`, wait a second, type `R0`. The turret pans right and stops.
5. Type `R1` and then nothing. After 1.5 s you should see the watchdog fire.

That covers the servos, relays, parser and watchdog with no BLE involved.
Keep darts out of the magazine until movement is confirmed.

## Flipper Zero app

Your son's side. It runs on **stock firmware** — no custom or experimental
firmware needed — using the Flipper's built-in Serial profile as a byte pipe:

```c
furi_hal_bt_start_app(ble_profile_serial, ...)
ble_profile_serial_set_rpc_active(profile, false)   // required, see PROTOCOL.md
ble_profile_serial_set_event_callback(profile, buf_size, callback, ctx)
ble_profile_serial_tx(profile, (uint8_t*)"R1", 2)
```

All four are exported to apps in the official `api_symbols.csv`. The HID profile
is not (`ble_profile_hid` is marked `-`), which is why this uses the serial
profile rather than pretending to be a keyboard.

**The Video Game Module cannot help here.** It is built on a Raspberry Pi
RP2040, which has no radio at all — no BLE, no WiFi. It is a DVI video output
plus an ICM-42688-P 6-axis IMU, connected over the GPIO header.

## Fallback if BLE pairing proves painful

Infrared. A TSOP38238 on the WROOM is one GPIO plus power, and the Flipper's
stock Infrared app can send NEC codes with no custom app at all — then a proper
FAP with a D-pad once it works. Line of sight only, and weak in direct sunlight,
but it is the cheapest thing that can possibly work.

## Security

The Flipper link is bonded and encrypted once paired. The pairing code is
entered once over USB serial; after that the bond lives in NVS.

Note that the turret connects to the **first** device it sees advertising the
Flipper Serial service or a name starting with `Flipper`. On a workbench with
several Flippers around, pin it to one by setting `FLIPPER_NAME_PREFIX` to the
full device name.
