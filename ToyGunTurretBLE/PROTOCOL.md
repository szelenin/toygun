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

| Role | UUID | Properties |
|---|---|---|
| Service | `8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000` | — |
| **TX** — Flipper writes, turret subscribes | `19ed82ae-…-228e61fe0000` | read, **indicate** |
| **RX** — turret writes, Flipper reads | `19ed82ae-…-228e62fe0000` | read, write, write-no-response |
| Flow control | `19ed82ae-…-228e63fe0000` | read, notify |
| RPC status | `19ed82ae-…-228e64fe0000` | read, write, notify |

Every row above was read off the real device, not just the firmware source — the
full GATT dump is at the end of this file. Negotiated **ATT MTU was 255**, so
payload size is not a constraint for a protocol whose longest message is about
twenty bytes.

The turret finds the Flipper **by MAC address** (`FLIPPER_ADDRESS` in the
sketch). Pairing is bonded: the Flipper shows a six-digit code once, it gets
typed into the turret's serial monitor, and the bond is stored in NVS so every
later connection is silent.

### Verified working, 2026-09-05

```
80:e1:26:f3:3a:6b  Ulb4fy   rssi -54   <= FLIPPER
Connecting to 80:e1:26:f3:3a:6b...
Pairing OK, link encrypted
TX props: read=1 notify=0 indicate=1 | RX props: write=1 wnr=1
Link up. Waiting for commands.
-> VER 1 LizardGun3000
```

### Four things that will waste your afternoon

1. **A Flipper connected to a phone does not advertise at all.** If the turret
   cannot see it, close the Flipper mobile app first. This looks exactly like
   "Bluetooth is off".
2. **Do not match on name or service UUID.** A Flipper's BLE name is whatever
   its owner set - ours is `Ulb4fy`, with no "Flipper" in it - and the
   advertisement carries service `0x3081`, *not* the Serial service UUID. Match
   the address. Run once with `VERBOSE_SCAN 1` to read it off.
3. **TX indicates, it does not notify.** `serial_service.c` declares it
   `CHAR_PROP_READ | CHAR_PROP_INDICATE`. NimBLE's `subscribe()` takes
   *notifications* as its first argument, so indications need `subscribe(false,
   cb)`. Subscribing with `true` fails silently-ish and looks like a pairing
   problem.
4. **Do not request MITM.** Every characteristic is `ATTR_PERMISSION_AUTHEN_*`,
   so `setSecurityAuth(bond, mitm=true, sc)` looks correct - but the WB55 then
   rejects the pairing with HCI `0x05`, Authentication Failure (NimBLE reason
   517). With `mitm=false` the Flipper still prompts for its passkey, pairing
   completes, and the characteristics are readable.

After changing any security parameter, clear the bond on **both** sides or the
peer rejects the new settings with that same 517: set `CLEAR_BONDS_ON_BOOT 1`
in the sketch, and on the Flipper use Settings > Bluetooth > Forget All Paired
Devices.

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

**This is the one link in the chain still untested.** Everything up to and
including subscribing to TX is proven working on hardware; what has never been
exercised is an app actually pushing bytes down the pipe. When you first try it,
the turret prints every inbound chunk as `<- ...` on its serial console, so you
can see immediately whether the bytes are arriving.

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

## Appendix: GATT dump from the real device

Read off `Ulb4fy` on 2026-09-05 by the turret itself, immediately after pairing.
`r`=read `w`=write `wnr`=write-no-response `n`=notify `i`=indicate.

```
=== GATT DUMP  mtu=255 ===
SERVICE 0x1801                                   (Generic Attribute)
  CHAR 0x2a05  r=0 w=0 wnr=0 n=0 i=1
SERVICE 0x1800                                   (Generic Access)
  CHAR 0x2a00  r=1 w=0 wnr=0 n=0 i=0             device name
  CHAR 0x2a01  r=1 w=0 wnr=0 n=0 i=0
  CHAR 0x2a04  r=1 w=0 wnr=0 n=0 i=0
SERVICE 0x180a                                   (Device Information)
  CHAR 0x2a29  r=1 ...                           manufacturer
  CHAR 0x2a25  r=1 ...                           serial number
  CHAR 0x2a26  r=1 ...                           firmware revision
  CHAR 0x2a28  r=1 ...                           software revision
  CHAR 03f6666d-ae5e-47c8-8e1a-5d873eb5a933  r=1 ...
SERVICE 0x180f                                   (Battery)
  CHAR 0x2a19  r=1 w=0 wnr=0 n=1 i=0             battery level
  CHAR 0x2a1a  r=1 w=0 wnr=0 n=1 i=0
SERVICE 8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000     (Flipper Serial)
  CHAR 19ed82ae-ed21-4c9d-4145-228e62fe0000  r=1 w=1 wnr=1 n=0 i=0   RX
  CHAR 19ed82ae-ed21-4c9d-4145-228e61fe0000  r=1 w=0 wnr=0 n=0 i=1   TX
  CHAR 19ed82ae-ed21-4c9d-4145-228e63fe0000  r=1 w=0 wnr=0 n=1 i=0   flow control
  CHAR 19ed82ae-ed21-4c9d-4145-228e64fe0000  r=1 w=1 wnr=0 n=1 i=0   RPC status
=== END GATT DUMP ===
```

Note `i=1` on TX and `n=0`: it indicates and never notifies. That single column
is what the earlier subscribe failure came down to.

There is no HID service in the list, which matches `ble_profile_hid` not being
exported to apps. The Battery service is standard and unrelated, though the
turret could read the Flipper's charge level from `0x2a19` if that were ever
useful.

## What is verified, and what is not

| Claim | Status |
|---|---|
| Service and characteristic UUIDs | **Verified on device** — GATT dump above |
| TX indicates, does not notify | **Verified on device** |
| RX accepts write and write-no-response | **Verified on device** |
| Scanning, pairing, bonding, subscribing | **Verified on device**, reconnects in 3.6 s |
| Turret sends `VER 1 LizardGun3000` on link up | **Verified on device** |
| Flipper advertises `0x3081`, not the Serial UUID | **Verified** — seen in scan output |
| A phone-connected Flipper stops advertising | **Verified** — it appeared only once disconnected |
| Commands, replies, watchdog, angle limits | **Verified over USB serial**, not yet over BLE |
| `ble_profile_serial_*` exported to apps | From the official `api_symbols.csv`, not run |
| `rpc_active(false)` required | **Unverified** — no app has pushed bytes yet |
