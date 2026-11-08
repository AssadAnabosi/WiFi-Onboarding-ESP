# Local development

Building, flashing and testing on your own machine.

## Setup

You need:

- **Arduino IDE 2** or **`arduino-cli`** (CI uses arduino-cli 1.5.1).
- **Python 3** with the tools: `pip install -r utils/requirements.txt`.
- An ESP32 board with at least 4 MB flash (see [getting-started.md](getting-started.md#supported-hardware)).

The exact ESP32 core and library versions are pinned in `esp32/sketch.yaml`:

| Component | Version |
| --- | --- |
| ESP32 Arduino core (`esp32:esp32`) | 3.3.12 (3.3.8 is the minimum: signed updates need it) |
| ArduinoJson | 7.4.3 |
| Async TCP (ESP32Async) | 3.5.0 |
| ESP Async WebServer (ESP32Async) | 3.12.1 |

**arduino-cli** reads `sketch.yaml` and installs exactly these versions on its own:

```sh
arduino-cli compile --profile esp32-4mb --build-path build esp32
arduino-cli upload --profile esp32-4mb --build-path build -p <PORT> esp32
```

**Arduino IDE** ignores `sketch.yaml`, so install the same versions by hand (Boards Manager and Library Manager), select an ESP32 board and the **"No FS 4MB (2MB APP x2)"** partition scheme. Both the IDE and arduino-cli pick up `esp32/partitions.csv` and `esp32/build_opt.h` from the sketch folder automatically. `build_opt.h` turns on signed updates (`-DUPDATE_SIGN`) and sets the build target; don't delete it.

## Edit loop

1. Firmware: edit the files in `esp32/`.
2. Web interface: edit `web-interface/`, then regenerate the embedded copy:

   ```sh
   python utils/build_assets.py
   ```

   This minifies and gzips the pages into `esp32/WebAssets.h` and writes `esp32/Version.h` from the `VERSION` file. Commit the source change and the regenerated files together; CI fails if they drift.
3. Compile and upload over USB (above). Uploading over USB keeps the settings stored on the device.
4. Watch the serial monitor at 115200 baud.

For a completely clean start, erase the flash: Arduino IDE's **Tools → Erase All Flash Before Sketch Upload**, or `esptool.py erase_flash`. This wipes the settings.

Run the Python tests before pushing:

```sh
python -m unittest discover -s utils/tests
```

CI runs the same tests, the generated-files check, `python utils/targets.py check` and a build of every target.

## Flash a release over USB

```sh
esptool.py --chip esp32 --port <PORT> write_flash 0x0 wifi-onboarding-<version>-esp32-4mb.factory.bin
```

This wipes everything, settings included. See [getting-started.md](getting-started.md).

## Dev keys

A local build can't be installed over the air on a device running a release, because it isn't signed by the release key. For OTA testing, make a dev key once:

```sh
python utils/keygen.py --dev
```

This writes a key pair to `keys/ota-dev.pem` and its public key to `esp32/OtaPublicKey.local.h`. Both are gitignored and CI never sees them. Any build that finds `OtaPublicKey.local.h` is a **dev build**:

- it trusts the dev key **and** the release keys, so it can be updated with your own builds and still return to official releases without USB;
- it shows "(dev)" after the version in the footer and in `/api/status`.

Flash a dev build over USB once. After that, sign each new build and upload it from Settings → Firmware:

```sh
python utils/sign.py --key keys/ota-dev.pem --target esp32-4mb \
    --firmware build/esp32.ino.bin --partitions build/esp32.ino.partitions.bin \
    --out wifi-onboarding-dev-esp32-4mb.ota.bin
python utils/sign.py --verify wifi-onboarding-dev-esp32-4mb.ota.bin --pubkey esp32/OtaPublicKey.local.h
```

The version in the file comes from `VERSION`. To install a build with the same version as the running one, confirm the "already installed" prompt.

Delete `esp32/OtaPublicKey.local.h` to make release-like builds again.

## Testing pulls locally

A dev build can check a manifest on your own machine instead of GitHub, over plain HTTP:

1. Point dev builds at your computer, then rebuild and flash the dev build:

   ```sh
   python utils/keygen.py --dev-manifest-url http://<your-PC-IP>:8000/manifest.json
   ```

2. Build a newer version (bump `VERSION`, run `build_assets.py`, compile) and sign it with the dev key.
3. In a folder with that `.ota.bin`, write a `manifest.json`:

   ```json
   {
     "schema": 1,
     "project": "wifi-onboarding",
     "version": "0.1.1",
     "notes": "What changed",
     "targets": {
       "esp32-4mb": {
         "url": "http://<your-PC-IP>:8000/wifi-onboarding-0.1.1-esp32-4mb.ota.bin",
         "size": 1324416,
         "sha256": "<sha256 of the .ota.bin>"
       }
     }
   }
   ```

   `size` is the file size in bytes; get the SHA-256 with `sha256sum` (or `certutil -hashfile <file> SHA256` on Windows).
4. Serve the folder: `python -m http.server 8000`, and allow it through your firewall.
5. Click **Check for updates** in Settings → Firmware.

Plain HTTP works in dev builds only; release builds fetch over HTTPS. `python utils/keygen.py --dev-manifest-url ""` goes back to the GitHub release manifest.

## Troubleshooting

| Problem | What to check |
| --- | --- |
| No output on the serial monitor | Baud rate 115200; the right port; some boards need the BOOT button held while uploading |
| Upload over USB fails | Close any serial monitor holding the port; try a lower upload speed or another cable |
| "This file isn't signed for this project." | The file is unsigned or signed by a key the running firmware doesn't trust. A release build trusts only release keys: flash a dev build over USB first |
| "This file is for another project" / "for … and cannot run on this device" | Wrong `PROJECT_SLUG`, chip or partition table in the file |
| "The file is too large for this device" | The firmware no longer fits its slot (`python utils/targets.py size esp32-4mb build/esp32.ino.bin`) |
| The update rolls back after a minute or two | The new firmware didn't stay up for 60 s after setup; read the serial log of that boot |
| A pull check reports "No internet" | The device is in access-point-only mode; connect it to a network |
