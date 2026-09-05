# Turret BLE protocol — v1

**The contract between the Flipper app and the turret firmware.** Both sides
code against this document. If it needs to change, bump `PROTOCOL_VERSION` in
`ToyGunTurretBLE.ino` and update this file in the same commit.

| Side | Owner |
|---|---|
| Flipper Zero app (BLE central) | son |
| ESP32-WROOM-32E firmware (BLE peripheral) | dad |

## Roles — read this first

The Flipper **cannot** be a BLE central. Checked against the official
`targets/f7/api_symbols.csv`: there is no exported symbol for scanning,
connecting outward, or GATT client operations. It can only advertise and be
connected to.

So the roles are inverted from what you might expect:

| | Role |
|---|---|
| **Flipper** | peripheral + GATT server, running its built-in **Serial** profile |
| **Turret (WROOM)** | **central** + GATT client — it scans, connects and subscribes |

```
   FLIPPER ZERO                                WROOM ON THE GUN
   peripheral                                  central

   advertising  ← ← ← ← ← ← ← ← ← ← ← ← ← ←   1. scanning
                ← ← ← ← ← ← ← ← ← ← ← ← ← ←   2. connects, pairs
   TX char      → → →  commands  → → → → → →   3. subscribed
   RX char      ← ← ←  telemetry ← ← ← ← ← ←
```

The turret does the reaching out. The Flipper just needs Bluetooth on and the
app running.

## Connection

The turret talks over the Flipper's own Serial service. UUIDs are taken from
`targets/f7/ble_glue/services/serial_service_uuid.inc` and byte-reversed into
normal order (the firmware stores 128-bit UUIDs least-significant byte first).

| Role | UUID |
|---|---|
| Service | `8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000` |
| **TX** — Flipper writes, turret subscribes | `19ed82ae-ed21-4c9d-4145-228e61fe0000` |
| **RX** — turret writes, Flipper reads | `19ed82ae-ed21-4c9d-4145-228e62fe0000` |
| Flow control | `19ed82ae-ed21-4c9d-4145-228e63fe0000` |
| RPC status | `19ed82ae-ed21-4c9d-4145-228e64fe0000` |

The turret finds the Flipper by advertised service UUID, falling back to a name
starting with `Flipper`. Pairing is bonded: the Flipper shows a six-digit code
once, it gets typed into the turret's serial monitor, and the bond is then
stored in NVS so later connections are silent.

## Flipper app requirements

These are the API calls on your side. All are exported to apps (`+` in
`api_symbols.csv`):

```c
furi_hal_bt_start_app(ble_profile_serial, ...)   // or furi_hal_bt_change_app
ble_profile_serial_set_rpc_active(profile, false)
ble_profile_serial_set_event_callback(profile, buf_size, callback, ctx)
ble_profile_serial_tx(profile, (uint8_t*)"R1", 2)
```

⚠️ **`ble_profile_serial_set_rpc_active(profile, false)` is not optional.**
Leave RPC active and the RPC layer consumes the bytes before they reach the
wire — the link looks connected and nothing happens.

Note `ble_profile_hid` and every `ble_profile_hid_*` function are marked `-` in
the symbol table: not exported, not usable from an app. The serial profile is
the one available byte pipe, which is why the protocol is ASCII over it.

## Handshake

The turret sends `VER 1 LizardGun3000` unprompted as soon as the link comes up,
and also whenever it receives `V`.

The app should:

1. Wait for that `VER` line, or send `V` and wait for the reply.
2. Refuse to send movement or fire commands until it has seen a version it
   knows.

If the version is not one the app knows, disconnect and say so. Do not send
movement or fire commands to an unknown protocol version.

## Commands — Flipper app sends these, ASCII

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

## Replies — the turret sends these back, ASCII

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

## Resolved: how the Flipper connects

This was an open question and is now settled. Official firmware exports no BLE
central capability, so the earlier plan — turret as peripheral, Flipper as
central — was impossible. Inverting the roles and using the Flipper's own Serial
profile works on **stock firmware**, with no custom or experimental firmware and
no extra hardware.

Rejected alternatives, for the record:

- **Moon Firmware** — a hard fork that swaps in the BLE Full radio stack and does
  give the Flipper central mode. Rejected: experimental, and it reflashes the
  radio coprocessor.
- **Sub-GHz + CC1101** — works, but arbitrary packet TX from an app runs into
  documented preamble and FIFO trouble, and needs matching radio configs on both
  sides.
- **Infrared** — simplest of all, still the fallback if BLE pairing turns out to
  be painful. Line of sight only, and weak in direct sunlight.

## Testing without the other half

**Turret, no Flipper app:** the firmware reads the same commands from the USB
serial monitor. Open it at 115200, type `V`, expect `VER 1 LizardGun3000`. Then
`R1`, wait, `R0`. This exercises every line of the servo, relay, watchdog and
parser code with no BLE involved at all.

**Turret, no Flipper app, over BLE:** with Bluetooth on but no app running, the
turret should still find the Flipper, pair, and discover the Serial service. It
will report `No Flipper Serial service` or sit with the link up and no traffic —
either way, discovery and pairing are proven.

**Flipper app, no turret:** any BLE terminal on a phone can connect to the
Flipper and receive what the app sends, confirming `rpc_active(false)` and
`ble_profile_serial_tx` work before the turret is involved.

Keep darts out of the magazine until movement is confirmed.
