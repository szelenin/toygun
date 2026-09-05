# Turret BLE protocol — v1

**The contract between the Flipper app and the turret firmware.** Both sides
code against this document. If it needs to change, bump `PROTOCOL_VERSION` in
`ToyGunTurretBLE.ino` and update this file in the same commit.

| Side | Owner |
|---|---|
| Flipper Zero app (BLE central) | son |
| ESP32-WROOM-32E firmware (BLE peripheral) | dad |

## Connection

| | |
|---|---|
| Advertised name | `LizardGun3000` |
| Service UUID | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX — central writes here | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX — central subscribes here | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| MTU | 64 |
| Pairing | none — open connection |

This is the Nordic UART service, so nRF Connect and LightBlue can drive the
turret directly. Use that to test either side in isolation.

The turret requests a 7.5–15 ms connection interval on connect. Prefer
**write-without-response** on RX; nothing in this protocol needs an ack.

## Handshake

On connect, the app **must**:

1. Subscribe to notifications on TX.
2. Write `V`.
3. Expect `VER 1 LizardGun3000` back.

If the version is not one the app knows, disconnect and say so. Do not send
movement or fire commands to an unknown protocol version.

## Commands — written to RX, ASCII

| Command | Effect |
|---|---|
| `U1` / `U0` | Tilt up — press / release |
| `D1` / `D0` | Tilt down — press / release |
| `L1` / `L0` | Pan left — press / release |
| `R1` / `R0` | Pan right — press / release |
| `F1` / `F0` | Fire start / stop |
| `C` | Center servos, clear the saved position |
| `P` | Request a position notification |
| `B<0-255>` | PWM duty on the spare GPIO 4 output, e.g. `B64` |
| `K` | Keepalive — refreshes the watchdog, changes nothing |
| `V` | Handshake — replies `VER <protocol> <name>` |

Several commands may share one write, separated by spaces or newlines:
`R1 F1` is legal. Unknown commands return `ERR <char>` and change nothing.

## Notifications — from TX, ASCII

| Message | Meaning |
|---|---|
| `POS <h> <v>` | Current angles. Sent at most every 200 ms while moving, and on `P`. |
| `FIRE <0\|1>` | Firing state changed |
| `LED <0-255>` | Spare PWM output changed |
| `VER <n> <name>` | Handshake reply |
| `ERR <char>` | Unrecognized command |
| `OK watchdog` | The deadman timer just cut all outputs |

Angle ranges: horizontal **0–180**, vertical **75–100**. Vertical is reversed —
`U` decreases the angle. The firmware clamps; the app does not need to.

## The deadman watchdog — read this

The turret launches projectiles and BLE links drop without warning, so the
firmware kills all outputs when it stops hearing from the app.

- **Any disconnect** stops firing and movement immediately.
- **1500 ms with no write of any kind** stops firing and movement, and sends
  `OK watchdog`.

**The app must therefore send something at least every 500 ms while any button
is held** — either the held command again (`R1`, `F1`) or a bare `K`. A single
`F1` with nothing following it fires for 1.5 seconds and stops on its own.

This is deliberate and is not going to be relaxed. Build the keepalive into the
app's main loop, not into the button handler.

## Firing behaviour

`F1` turns the spinner relay on, blocks 250 ms while the flywheels spin up, then
turns the trigger relay on. So `F1` takes about a quarter second to return, and
no other command is processed during it. `F0` cuts both relays immediately.

## Open question

The Flipper must act as a BLE **central** for any of this to work. Official
firmware is built around being a peripheral, and GATT-client support has lived
in third-party branches. **Confirm the firmware branch can scan, connect and
write a characteristic before building on this document.** If it cannot, the
fallback is infrared: a TSOP38238 on the WROOM and saved NEC codes in the
Flipper's stock Infrared app, which needs no custom Flipper app at all.

## Testing without the other half

**Flipper app, no turret:** run any Nordic UART peripheral emulator on a phone,
or flash this sketch to a spare ESP32 with no servos attached — commands still
notify correctly with nothing wired.

**Turret, no Flipper app:** nRF Connect. Connect, enable notifications on
`...0003`, write `V` to `...0002` as text, expect `VER 1 LizardGun3000`. Then
`R1`, wait, `R0`. Keep darts out of the magazine until movement is confirmed.
