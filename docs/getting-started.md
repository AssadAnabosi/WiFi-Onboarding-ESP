# Getting started

How to get the firmware onto a board for the first time. After this first USB flash, updates go over the air (see [ota-updates.md](ota-updates.md)).

## Supported hardware

There are two build targets, both for classic ESP32 modules:

| Target | Flash | App slots |
| --- | --- | --- |
| `esp32-4mb` | 4 MB or more | 2 × ~1.9 MB |
| `esp32-8mb` | 8 MB or more | 2 × ~3.9 MB |

Pick the one that matches your board's flash size; `esptool.py flash_id` prints it. An `esp32-4mb` build also runs on an 8 MB board but leaves half the flash unused. Moving a board from one target to the other takes a USB flash, because the flash layout differs.

| Module | Notes |
| --- | --- |
| ESP32-WROOM-32, -32D, -32E, -32UE | Supported |
| ESP32-WROVER, -WROVER-E (e.g. on an ESP32-WROVER-DevKitC) | Supported. The extra RAM (PSRAM) is not used, and GPIO16/GPIO17 are wired to it, so your own hardware must not use those pins |
| ESP32-PICO-D4 | Supported |
| ESP32-SOLO-1 (single core) | Not supported for now: it needs a special core build |
| ESP32-H2, ESP32-P4 and other chips without Wi-Fi | Never supported. Wi-Fi onboarding and OTA are the whole project, so the firmware refuses to compile for them |

Other chips with Wi-Fi (ESP32-S3, -C3, -C6 and so on) can be added as new build targets; see [architecture.md](architecture.md#build-targets).

## Option 1: flash a release

1. Download `wifi-onboarding-<version>-<target>.factory.bin` for your target (e.g. `esp32-4mb`) from the [latest release](https://github.com/AssadAnabosi/WiFi-Onboarding-ESP/releases/latest). Optionally check it against `SHA256SUMS` from the same release.
2. Install [esptool](https://docs.espressif.com/projects/esptool/) (`pip install esptool`) and connect the board over USB.
3. Flash the whole image at offset 0:

   ```sh
   esptool.py --chip esp32 --port <PORT> write_flash 0x0 wifi-onboarding-<version>-<target>.factory.bin
   ```

   `<PORT>` is e.g. `COM3` on Windows or `/dev/ttyUSB0` on Linux. Newer esptool versions also accept `esptool write-flash`.

The factory image is a full 4 MB image: bootloader, partition table and firmware. **Flashing it wipes everything on the board, settings included.** It is only for USB flashing; the device's update page refuses it. Later versions are installed with the `.ota.bin` file instead.

## Option 2: build it yourself

Build with Arduino IDE 2 or `arduino-cli` and upload over USB. See [local-development.md](local-development.md).

## First setup

1. On a phone or laptop, join the Wi-Fi network **`ESP-Access-Point`**, password **`12345678`**.
2. Open **`http://config.local`** or **`http://192.168.4.1`**.
3. On **Networks**, pick your network, enter its password and connect. The device saves the credentials once the connection works.
4. On **Settings**, change the access point password. The default one is public.

On every boot the device tries the saved network three times. If that fails, or if the access point is enabled in Settings, it also runs its own access point at `192.168.4.1`. Once it is on your network, open it at the IP address your router gave it (shown on the Home page while you are connected to the access point). The hostname setting (`ESP-IoT` by default) is the name the device gives your router.

## WPA2-Enterprise (not verified)

To join a WPA2-Enterprise (PEAP/MSCHAPv2) network, enter `username|password` as the network password. This has not been tested on a real enterprise network yet.
