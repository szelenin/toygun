# Flipper Zero — example and connection code

**This is a reference, not the app you have to use.** If you are writing your
own remote, write your own remote. This folder exists so there is something
working to look at when you want to know how a particular piece is done.

Two separate things live here:

| File | What it is |
|---|---|
| **`turret_link.h` / `turret_link.c`** | The connection code. Copy these two files into your own app and you can talk to the turret without touching BLE. |
| `toygun_remote.c` | A small example app that uses them. Read it, run it, ignore it — whichever is useful. |

## Just show me how to connect

Three calls:

```c
#include "turret_link.h"

TurretLink* link = turret_link_alloc();       // opens the link

turret_link_hold(link, 'R', pressed);         // pan right while a button is held
turret_link_tick(link);                       // <-- from your main loop, always

turret_link_free(link);                       // stops the gun, hands BLE back
```

`turret_link_tick()` is the one that isn't optional. The turret stops moving and
firing after **1500 ms of silence** — on purpose, so that a dropped connection
can never leave the gun firing by itself. Something has to keep talking while a
button is held, and `tick()` is that something. Call it every time round your
loop.

Letters are `U` `D` `L` `R` for the directions and `F` for fire. The full
protocol — every command, every reply, the angle limits — is in
**[../ToyGunTurretBLE/PROTOCOL.md](../ToyGunTurretBLE/PROTOCOL.md)**.

## Which way round the connection goes

This surprises people, so it is worth being clear:

**The turret connects to the Flipper.** Not the other way round.

The Flipper can only ever be a *peripheral* — its firmware has no API for
scanning or connecting outward, we checked. So the ESP32 does the reaching: it
scans, finds the Flipper by address, connects, and pairs. Your app doesn't
search for anything. It opens the Flipper's Serial profile, writes text into it,
and the turret is already listening.

Which means: **you never write connection code.** `turret_link_alloc()` just
claims the serial channel and waits.

## Build

```bash
python3 -m pip install --upgrade ufbt
cd ToyGunFlipperApp
ufbt launch          # Flipper plugged in by USB
```

Appears in **Apps → GPIO**. Verified building against SDK 1.4.3, API 87.1.

If `ufbt launch` fails the first time, run plain `ufbt` once — it downloads the
SDK, which takes a minute.

## Knowing it works

The example sends `V` on startup and the turret answers `VER 1 LizardGun3000`,
shown on the bottom line. That one line proves the entire chain: profile
started, RPC off, bytes out, turret listening, reply back.

Take the darts out until you've seen the gun move.

## The two traps

Both are already handled in `turret_link.c` — this is so you recognise them if
you write your own version.

**`ble_profile_serial_set_rpc_active(profile, false)`** — the Flipper normally
uses this same channel to talk to its phone app. Without that line, the phone-app
layer eats every byte before it reaches the radio. Everything looks connected and
absolutely nothing happens.

**Keepalive in the main loop, not the button handler.** Sending it only when a
button changes works perfectly on a desk and fails the moment one packet goes
missing.

## When it doesn't work

The turret prints everything it receives on its USB serial console, like `<- R1`.
That splits any problem in half immediately:

| What you see | Where the problem is |
|---|---|
| Nothing on the turret console | Bytes aren't leaving the Flipper — check `rpc_active` |
| `<- R1` appears but nothing moves | Turret's problem, not yours |
| Works, then stops after a second | Keepalive isn't being sent |
| Turret never connects at all | Is your phone connected to the Flipper? Close the Flipper phone app — a Flipper talking to a phone stops advertising and goes invisible |

## Ideas

- Show the turret's position — it sends `POS 95 88` while moving
- Draw a crosshair that moves with the reported angle
- Require holding OK for half a second before firing
- Vibrate when the turret sends `OK watchdog`, so you know it cut out
- Aim by tilting — but that needs the Video Game Module, which is a whole
  different project
