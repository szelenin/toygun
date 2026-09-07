# Flipper Zero WiFi via ESP32-WROOM + Marauder

Standalone side project — nothing to do with the turret. Turns a spare
ESP32-WROOM into a WiFi board for the Flipper by running WiFi Marauder on it and
wiring it to the Flipper's GPIO header.

This used the **second** WROOM (MAC `44:1d:64:f8:2b:5c`, CP2102 USB, enumerates
as `/dev/cu.usbserial-0001`). The turret's WROOM (`f0:24:f9:e3:7e:a4`, CH340,
`/dev/cu.wchusbserial10`) was not touched.

## Wiring: WROOM → Flipper GPIO

Four wires. TX and RX cross over.

```
WROOM              Flipper Zero (top GPIO header)
TX0 (GPIO 1) ───→  Pin 14  (RX)
RX0 (GPIO 3) ←───  Pin 13  (TX)
3V3          ←───  Pin 9   (3V3)
GND          ───→  Pin 18  (GND)
```

- Power from the Flipper's 3V3 (pin 9). Do **not** also plug USB into the WROOM
  while it's on the Flipper — one power source at a time.
- If Marauder connects but commands do nothing, swap the TX/RX wires. Backwards
  crossover is the usual cause and trying it harms nothing.
- On the Flipper: **Apps → GPIO → ESP32 WiFi Marauder** (built into Momentum).
- Flipper UART pin set is switchable: **GPIO → USB-UART Bridge → Left → UART
  Pins** toggles 13/14 vs 15/16. Match the wiring above (13/14).

## Flashing (the way that actually worked)

Flash from the **Flipper itself**, over the 4 GPIO wires. This installs the
correct build for a bare WROOM and needs no working Mac USB.

1. Flipper: **Apps → GPIO → [ESP] Flasher**
2. **Quick Flash → Other ESP32-WROOM → Marauder (has evil portal)**
3. It will say *"Cannot connect to target ... make sure the device is in
   bootloader/reflash mode"*. This is expected — the Flipper can't reset a bare
   WROOM electrically over 4 wires. Put it in flash mode by hand:
   - **Hold BOOT** (aka IO0) on the WROOM
   - **Tap EN** (aka RST) once, release it
   - **Release BOOT**
4. Run the flash again. It connects and finishes in ~30 s.
5. **Tap EN once** to reset the board (single tap, no BOOT this time).
6. Open **Apps → GPIO → ESP32 WiFi Marauder**, run **Scan**, then **List → ap**.
   Real networks should appear.

⚠️ **Do NOT use the release `flipper` or `old_hardware` builds for this board.**
- `flipper` is compiled for the ESP32-**S2** (the official devboard is an S2) —
  it just boot-loops on a plain WROOM.
- `old_hardware` is a plain-ESP32 build that boots and answers commands, but it
  drives an external-antenna select pin this bare WROOM does not have, so it
  scans and finds **zero** networks. This wasted an afternoon. The ESP Flasher
  "Other ESP32-WROOM" build uses the onboard PCB antenna and works.

## If you must flash from the Mac (advanced)

esptool ships with the Arduino ESP32 core:
`~/Library/Arduino15/packages/esp32/tools/esptool_py/5.1.0/esptool`

The chip id is the two bytes at offset 12 of any `.bin`: `0000` = plain ESP32
(what this board is), `0200` = S2. Never pass `--force` to flash an image whose
chip id does not match — esptool's "not an ESP32 image" warning is real.

The catch: the release bins do not include a bare-WROOM (onboard-antenna)
headless build, which is why the Flipper "Other ESP32-WROOM" path above is the
reliable one. If flashing from the Mac, get the matching bin from the
auto-detecting web flasher below rather than guessing a release asset.

## Verify without the Flipper

Marauder exposes a serial CLI at 115200. From the Mac:

```bash
arduino-cli monitor -p /dev/cu.usbserial-0001 -c baudrate=115200
```

On boot it prints a big `====` banner. Type `channel` and it should echo
`#channel` and a number. That is a working Marauder.

## Web flasher (Mac alternative that picks the right build)

`flash.pingequa.com` in Chrome/Edge — plug the WROOM in with a **data** USB
cable and let it auto-detect. It serves the correct generic-ESP32 build and
handles the offsets, so it avoids the wrong-build trap above. If the USB link is
noisy ("invalid head of packet"), try another cable or enter the bootloader by
hand (hold BOOT, tap EN, release BOOT).

## Verified working, 2026-09-07

Flashed via the Flipper "Other ESP32-WROOM" path. `Scan` then `List -> ap`
returns real networks, including the home mesh "Edgar" on multiple nodes.
