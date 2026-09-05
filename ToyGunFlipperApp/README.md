# Toy Gun Remote — Flipper Zero app

Drives the turret over Bluetooth. Arrows aim, OK fires.

This is a **starter app that already works**. The fiddly Bluetooth plumbing is
done; the fun part — making it look and feel how you want — is yours.

## Build it

You need Python and your Flipper plugged in by USB.

```bash
python3 -m pip install --upgrade ufbt
cd ToyGunFlipperApp
ufbt launch
```

`ufbt` builds the app, copies it to the Flipper, and starts it. It appears in
**Apps → GPIO → Toy Gun Remote**.

If `ufbt launch` fails the first time, run `ufbt` on its own once — it downloads
the SDK, which takes a minute.

## Try it

1. Turn the turret on. It connects to the Flipper by itself — you don't have to
   pair or search for anything.
2. Open the app. The top line should change to **Turret: connected**.
3. The bottom line shows what the turret sends back. On startup the app asks
   `V` and the turret answers `VER 1 LizardGun3000`. **Seeing that reply means
   everything works.**
4. Press an arrow. The gun moves while you hold it.

Take the darts out until you've seen it move.

## How it actually works

The turret is the one that connects to *us*. The Flipper is a peripheral — it
can't go looking for things — so this app doesn't scan or connect. It just opens
the Flipper's Serial profile and writes text into it, and the turret is already
listening.

Commands are two characters: a letter for what, and `1` or `0` for pressed or
released. `R1` means "start panning right", `R0` means "stop". The full list is
in [../ToyGunTurretBLE/PROTOCOL.md](../ToyGunTurretBLE/PROTOCOL.md).

## Two things not to break

**`ble_profile_serial_set_rpc_active(profile, false)`** — the Flipper normally
uses this Bluetooth channel to talk to its phone app. That line tells it to stop,
so our text gets through. Remove it and everything looks connected while nothing
happens at all.

**The keepalive lives in the main loop**, not in the button handler. The turret
stops moving and firing after 1500 ms of silence, on purpose, so that a dropped
connection can never leave the gun firing by itself. Something has to keep
talking while a button is held.

## When it doesn't work

The turret prints every message it receives on its USB serial console, like
`<- R1`. That splits any problem in half:

| Symptom | Meaning |
|---|---|
| Nothing appears on the turret console | The bytes aren't leaving the Flipper — check `rpc_active` |
| `<- R1` appears but nothing moves | The turret's problem, not yours |
| Worked, then stopped after a second | The keepalive isn't being sent |
| Turret never connects | Is your phone connected to the Flipper? Close the Flipper app — a Flipper that's talking to a phone goes invisible to everything else |

## Ideas once it works

- Show the turret's position on screen — it sends `POS 90 88` while moving
- Hold OK for half a second before firing, so it can't go off by accident
- Read the angle from `POS` and draw a little aiming crosshair
- Vibrate when the turret replies `OK watchdog`, so you know it cut out
