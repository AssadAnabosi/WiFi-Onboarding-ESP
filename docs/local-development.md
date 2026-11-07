# Local development

Building, flashing and testing on your own machine.

> Outline only. This page is filled in before the v0.1.0 release.

## Setup

Arduino IDE 2 or `arduino-cli`, ESP32 core and library versions (same as `esp32/sketch.yaml`), `pip install -r utils/requirements.txt`.

## Edit loop

Edit the UI, run `build_assets.py`, compile, upload over USB; "Erase All Flash" for a clean start.

## Flash a release over USB

`esptool.py write_flash 0x0 <file>.factory.bin` (wipes everything).

## Dev keys

`keygen.py --dev`, `OtaPublicKey.local.h`, signing with `sign.py`, uploading from Settings.

## Testing pulls locally

Dev manifest URL and `python -m http.server` (plain HTTP, dev builds only).

## Troubleshooting

Serial monitor, upload errors, "signature invalid" / "untrusted key", firmware too large.
