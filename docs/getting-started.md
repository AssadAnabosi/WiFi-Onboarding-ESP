# Getting started

How to get the firmware onto a board for the first time.

> Outline only. This page is filled in before the v0.1.0 release.

## Supported hardware

Classic ESP32 modules with at least 4 MB flash (`esp32-4mb`): ESP32-WROOM-32/32D/32E/32UE, ESP32-WROVER (PSRAM unused; GPIO16/17 reserved), ESP32-PICO-D4. ESP32-SOLO-1 is unsupported for now. Chips without Wi-Fi (ESP32-H2, ESP32-P4) are not and will not be supported.

## Option 1: flash a release

Download `wifi-onboarding-<version>-esp32-4mb.factory.bin` and flash it with `esptool.py write_flash 0x0 <file>`. This wipes everything, settings included.

## Option 2: build it yourself

See [local-development.md](local-development.md).

## First setup

Join `ESP-Access-Point` (password `12345678`), open `config.local` or `192.168.4.1`, connect to your Wi-Fi network, change the AP password.

## WPA2-Enterprise (not verified)

Enter `username|password` as the network password. Not yet tested on a real enterprise network.
