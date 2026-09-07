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

## The build that works

**`old_hardware`**, from the ESP32 Marauder release (v1.15.1 at time of writing).

⚠️ Do **not** use the `flipper` build. That one is compiled for the ESP32-**S2**
(the official Flipper WiFi Devboard is an S2), chip_id `0x0002`. This WROOM is a
plain ESP32 (chip_id `0x0000`); the S2 image just boot-loops. If esptool says
*"not an ESP32 image"*, that is chip mismatch — pick a different build, do NOT
pass `--force`.

Quick check: the two bytes at offset 12 of any Marauder `.bin` are the chip id.
`0000` = plain ESP32 (what we need), `0200` = S2, `0900` = S3, `0500`/`1700` = C-series.

```bash
xxd -s 12 -l 2 <file>.bin      # want: 0000
```

Other plain-ESP32 builds exist (`kit`, `v6`, `marauder_v7`, `mini`, `cyd_*`,
`m5stickc_*`) but they target boards with specific screens/hardware.
`old_hardware` is the generic bare-devkit build.

## Re-flashing from the Mac (copy-paste)

esptool ships with the Arduino ESP32 core:
`~/Library/Arduino15/packages/esp32/tools/esptool_py/5.1.0/esptool`

1. Download the release assets (the app bin + the installer-assets zip that
   carries the matched bootloader/partition-table/ota-data):

```bash
BASE=https://github.com/justcallmekoko/ESP32Marauder/releases/download/v1.15.1
curl -sL -o assets.zip "$BASE/marauder-installer-assets.zip"
unzip -o assets.zip '*old_hardware*'
P=esp32_marauder_installer_v1_15_1_20260824_old_hardware
```

2. Confirm the chip id is `0000`, then erase and flash the four regions:

```bash
ESPTOOL=~/Library/Arduino15/packages/esp32/tools/esptool_py/5.1.0/esptool
PORT=/dev/cu.usbserial-0001      # check with: ls /dev/cu.* | grep usbserial

xxd -s 12 -l 2 "$P.bin"          # must print 0000

"$ESPTOOL" --chip esp32 --port "$PORT" --baud 460800 erase-flash

"$ESPTOOL" --chip esp32 --port "$PORT" --baud 460800 write-flash -z \
  0x1000  "$P.bootloader.bin" \
  0x8000  "$P.partition-table.bin" \
  0xe000  "$P.ota-data.bin" \
  0x10000 "$P.bin"
```

Standard ESP32 offsets: bootloader `0x1000`, partition table `0x8000`,
ota-data/boot_app0 `0xe000`, app `0x10000`.

## Verify without the Flipper

Marauder exposes a serial CLI at 115200. From the Mac:

```bash
arduino-cli monitor -p /dev/cu.usbserial-0001 -c baudrate=115200
```

On boot it prints a big `====` banner. Type `channel` and it should echo
`#channel` and a number. That is a working Marauder.

## Easier alternatives (no command line)

- **Web flasher:** `flash.pingequa.com` in Chrome/Edge, plug the WROOM in with a
  data USB cable, let it auto-detect. Handles offsets and build choice for you.
- **From the Flipper itself:** **Apps → GPIO → [ESP] Flasher → Manual Flash**,
  board type **"Other WROOM"**, with the Marauder bin on the SD card.
