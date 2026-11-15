# Cordell Watch

Cordell Watch is a Cordell Aerial–branded passive RF-awareness toy and network tester for the Freenove FNK0104B 2.8" ESP32-S3 capacitive touchscreen board (ILI9341 240×320 display, FT6336U touch, and ES8311 audio). It is Squatch-Watch–inspired: a pocket scanner that notices nearby Wi-Fi and Bluetooth devices, flags likely cameras and Flock-style matches, and gives you a small toolkit for joining networks and testing connectivity. The UI uses a navy `#041123` and gold `#B6954D` theme.

## Hardware

Target board: Freenove FNK0104B, ESP32-S3 N16 (16 MB flash).

| Subsystem | Details |
| --- | --- |
| Display | ILI9341 240×320, HSPI — MISO 13, MOSI 11, SCLK 12, CS 10, DC 46, BL 45 |
| Touch | FT6336U, I2C — SDA 16, SCL 15, RST 18, INT 17 |
| Audio codec | ES8311 at I2C address `0x18` on the same bus |
| I2S | MCLK 4, BCLK 5, WS 7, DOUT 8, DIN 6 |
| Speaker amp enable | GPIO 1 (LOW = on) |
| MCU | ESP32-S3 N16, 16 MB flash |

## Features

- **Passive Wi-Fi scan** about every 12 seconds, plus a **passive BLE scan** (4 seconds), both running in the background with MAC-address deduplication.
- **Ping toast and SFX** when a new device appears.
- A **dismissable popup** for camera / Flock-style matches (button or tap outside).
- A **16-tile Bingo card** persisted in NVS, with achievements at 4 / 8 / 12 / 16 tiles and a first-line BINGO.
- **Wi-Fi scan → tap a network → password sheet → Connect.** The keyboard OK / Cancel / tap-outside path hides the sheet; the Connect button stays at the top so it is never covered by the keyboard. **Manual join** is available for hidden SSIDs.
- **Tools menu:** Channel map, Net info, Ping, DNS, Traceroute, BLE list, Watch ON/OFF, and Sound ON/OFF.

## Device fingerprinting

These matches are heuristic **"likely"** labels, not proof of a device's identity. See `src/watch.cpp` for the exact patterns and OUI lists.

- **SSID / name patterns:** Ring, Arlo / VMB, Wyze, Nest Cam, Reolink, eufy, Blink, Hikvision, Dahua, Amcrest, Lorex, Tapo, EZVIZ, Foscam, and generic cam / IPC names.
- **Camera OUIs:** including Hikvision, Dahua, Wyze, Ring, Reolink, Arlo, Axis, Foscam, and Amcrest.
- **Flock detection:** names containing `flock`, `Penguin`, `Pigvision`, or `FS Ext Battery`; a community OUI list; and BLE manufacturer ID `0x09C8`.
- **BLE:** Apple Find My (`0x004C` type `0x12`), Tile / SmartTag service UUIDs, and ASTM Remote ID (`0xFFFA`) which maps to the Drone tile.

## Attack Lab

> **WARNING:** The Attack Lab contains offensive features. Use them only for authorized testing on hardware and networks you own or have explicit permission to test. Capturing other people's credentials, standing up a rogue AP against a network you do not control, or interfering with third-party devices is illegal.

The Attack Lab (`src/attack.cpp`, `src/attack.h`) includes:

- **Evil Portal** — rogue AP plus captive DNS, with credential capture.
- **KARMA** — probe-response injection. Deauthentication / disassociation is rejected by the ESP32-S3 Wi-Fi driver, so KARMA is the substitute: the firmware answers probe requests for any SSID with its own BSSID and tries to pull clients onto the rogue AP.
- **Beacon flood** — random SSIDs.
- **Probe-request flood.**
- **BLE advertisement spam** — rotating random MAC.
- **Promiscuous client sniffer** — unique client MACs seen on the air.

**Hardware limitation (documented in `attack.cpp`):** the ESP32-S3 Wi-Fi driver rejects deauthentication (`0xC0`) and disassociation (`0xA0`) frames. `esp_wifi_80211_tx()` returns `ESP_ERR_INVALID_ARG` and the driver logs `unsupport frame type: 0c0`. A sweep of management subtypes on this chip showed that only beacon, probe-request, probe-response, action, and non-QoS data frames are accepted.

## Build

Build with [PlatformIO](https://platformio.org/):

```bash
pio run -e cordellwatch
```

- Platform: `pioarduino` espressif32 **55.03.39**
  (`https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip`)
- 16 MB flash config in `platformio.ini`: `board_build.flash_size=16MB`, `board_upload.flash_size=16MB`, partitions `default_16MB.csv` (2× 6.25 MB OTA app, SPIFFS, coredump).

### External libraries

This repo does not vendor the Freenove display stack (`lib/`, `vendor/`). You need the following libraries, which ship as zips under the Freenove ESP32-S3 Display package in `Libraries/FNK0104AB` and `Libraries/FNK0104N`:

- FT6336U v1.0.2
- TFT_eSPI v2.5.43
- lvgl v8.4.0
- TJpg_Decoder v1.1.0
- Freenove_WS2812_Lib v2.0.1
- ESP32-audioI2S v3.0.13

Place them where PlatformIO can find them (typically under `lib/`, matching a local Freenove FNK0104B checkout). The checked-in `platformio.ini` also points at a local Arduino-ESP32 `Network` library symlink used on the original build machine; adjust that path for your environment.

## Flash

```bash
python -m esptool --chip esp32s3 --port /dev/ttyACM1 -b 921600 write-flash --flash-size 16MB 0x0 dist/merged.bin
```

A prebuilt merged image may be included as `dist/merged.bin.base64`. Restore it before flashing:

```bash
base64 -d dist/merged.bin.base64 > merged.bin
```

Then point the `esptool` command at that `merged.bin` (or move it to `dist/merged.bin`). If the encoded image is not in the repo, build from source with `pio run -e cordellwatch` instead.

## Disclaimer

This project is for research and educational use on hardware and networks you own or are authorized to test.
