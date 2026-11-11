# Architecture

How the firmware is put together: an Arduino sketch in `esp32/` that keeps its settings in NVS, serves an embedded web interface, and updates itself into a second firmware slot.

## Code layout

The sketch root holds only what forks and tools touch; the code lives in `esp32/src/`, one folder per module (the Arduino tools compile everything under `src/`).

| Path | Role |
| --- | --- |
| `esp32.ino` | Creates the modules, hands each the others it needs, and runs `setup()`/`loop()` |
| `ProjectConfig.h` | Everything a fork changes ([make-it-yours.md](make-it-yours.md)) |
| `build_opt.h`, `partitions.csv`, `sketch.yaml`, `targets/` | The build target ([below](#build-targets)) |
| `WebAssets.h`, `Version.h`, `OtaPublicKey.h` | Generated; never edited by hand ([below](#generated-files)) |
| `.clang-format` | C++ formatting, used by Arduino IDE's Auto Format and most editors |
| `src/storage/ConfigStorage` | Settings (`DeviceSettings`, the saved network) and system records in NVS |
| `src/wifi/WiFiManager` | Saved network, access point, mDNS, scans, background connection attempts |
| `src/web/WebServerManager` | Embedded pages and response helpers; routes by area in `SystemRoutes.cpp`, `WifiRoutes.cpp` and `UpdateRoutes.cpp` ([api.md](api.md)) |
| `src/web/RequestGuard` | Cross-site protection for every POST |
| `src/ota/UpdateManager` | The update state machine: uploads, update checks, downloads, rollback requests |
| `src/ota/FirmwareSlots` | Slot records, USB-flash and rollback detection, the confirmation window |
| `src/ota/UpdateFile` | Update file layout and the info-block rules |
| `src/ota/UpdateCheck`, `src/ota/HttpGet` | Reading the release manifest; HTTPS downloads |
| `src/ota/OtaVerifier`, `src/ota/OtaKeys.h` | Tries each trusted public key when the core checks a signature |
| `src/system/` | `BuildInfo.h` (version, build date, commit, dev-build flag), `Log.h` (tagged serial lines), `Restart` (restart from `loop()` after the response), `CoreSerial` (optional debug helper) |

How the modules depend on each other (arrows point at what a module uses):

```mermaid
flowchart LR
    ino["esp32.ino"] --> web["WebServerManager"]
    web --> wifi["WiFiManager"]
    web --> updates["UpdateManager"]
    web --> slots["FirmwareSlots"]
    updates --> slots
    updates --> file["UpdateFile, UpdateCheck, HttpGet"]
    wifi --> storage["ConfigStorage"]
    slots --> storage
    web --> storage
```

- Modules are created once in `esp32.ino` and receive the modules they use as constructor references; there are no globals to reach for.
- `setup()` starts `FirmwareSlots` first (so the rollback deadline of new firmware is armed before anything can hang), then logs the settings and starts `WiFiManager`, `UpdateManager` and `WebServerManager`, and calls `firmwareSlots.setupDone()` last. `loop()` runs `FirmwareSlots`, `UpdateManager` and `Restart`.
- `WiFiManager`, `FirmwareSlots` and `UpdateFile` know nothing about HTTP; the web layer turns their results into API responses.
- Files in `src/` include root files by relative path (`#include "../../ProjectConfig.h"`).

## Flash layout

`esp32-4mb` uses `esp32/targets/esp32-4mb.partitions.csv`:

| Partition | Offset | Size | Holds |
| --- | --- | --- | --- |
| `nvs` | 0x9000 | 20 KB | Settings and system records |
| `otadata` | 0xE000 | 8 KB | Which app slot boots, and whether it is confirmed |
| `app0` | 0x10000 | 0x1F0000 (~1.9 MB) | Firmware slot A |
| `app1` | 0x200000 | 0x1F0000 (~1.9 MB) | Firmware slot B |
| `coredump` | 0x3F0000 | 64 KB | Crash information for debugging |

`esp32-8mb` uses `esp32/targets/esp32-8mb.partitions.csv`: the same partitions at the same offsets up to `app0`, with each app slot doubled. `esp32-8mb-psram` uses an identical copy; it differs from `esp32-8mb` only in turning PSRAM on (`PSRAM=enabled` in its `sketch.yaml` profile).

| Partition | Offset | Size | Holds |
| --- | --- | --- | --- |
| `nvs` | 0x9000 | 20 KB | Settings and system records |
| `otadata` | 0xE000 | 8 KB | Which app slot boots, and whether it is confirmed |
| `app0` | 0x10000 | 0x3F0000 (~3.9 MB) | Firmware slot A |
| `app1` | 0x400000 | 0x3F0000 (~3.9 MB) | Firmware slot B |
| `coredump` | 0x7F0000 | 64 KB | Crash information for debugging |

The bootloader sits at 0x1000 and the partition table at 0x8000. An update writes only the slot that isn't running, so settings survive every update and rollback.

The partition table and bootloader are **frozen from v0.1.0 on**: updates never change them, so a new layout would need a USB flash on every device. `otadata` must stay at 0xE000 and `app0` at 0x10000, which the core's factory-image recipe assumes. The SHA-256 of the table (padded to 0xC00 bytes) is the *layout hash* in every update file; a file for another layout is refused.

## Settings storage

Settings live in NVS through `Preferences`, in two namespaces:

| Namespace | Holds | Factory reset |
| --- | --- | --- |
| `cfg` | `wifi_ssid`, `wifi_password`, `ap_ssid`, `ap_password`, `ap_channel`, `ap_hidden`, `ap_status`, `hostname` | Cleared |
| `sys` | `home_target`, `last_switch`, the pending install (`pend_slot`, `pend_reason`), and a record per slot (`s0_`/`s1_` + `valid`, `version`, `target`, `sha`) | Kept |

- All names are listed once at the top of `src/storage/ConfigStorage.cpp`. The settings the Settings page edits travel together as one `DeviceSettings` struct.
- Every read takes a default from `ProjectConfig.h`, so a new setting needs no migration.
- An older firmware reads the same names after a rollback, so names are never renamed, reused or given another type (see [CONTRIBUTING.md](../CONTRIBUTING.md#setting-names)). Names are at most 15 characters.
- `WiFi.persistent(false)` stops the Wi-Fi driver keeping its own copy of the credentials. The whole NVS partition is never erased.

**Factory reset** clears `cfg`, calls `esp_wifi_restore()` for anything the Wi-Fi driver kept, and reboots with the defaults. The firmware and the `sys` records stay.

## Build targets

A build target is a configuration the firmware is built for, named `<chip>-<flash>[-<config>]`:

- `<chip>`: an Espressif chip with Wi-Fi: `esp32`, `esp32s2`, `esp32s3`, `esp32c2`, `esp32c3`, `esp32c5`, `esp32c6`, `esp32c61`.
- `<flash>`: `2mb`, `4mb`, `8mb`, `16mb` or `32mb`.
- `<config>` (optional): features, e.g. `relay` or `psram`.
- Lowercase letters, digits and single `-` only; `utils/targets.py check` enforces this in CI.

Each target is made of:

| Piece | File |
| --- | --- |
| Build profile (board, partition scheme, pinned core and libraries) | A profile named after the target in `esp32/sketch.yaml` |
| Partition table | `esp32/targets/<target>.partitions.csv` |
| Feature switches | `esp32/targets/<target>.h` |
| Build flags (`-DUPDATE_SIGN`, `OTA_TARGET_ID`, `OTA_TARGET_HEADER`) | `esp32/build_opt.h`, committed for the default target |

To add a target, add those three files; CI builds every profile without workflow changes. Then add its `Build <target>` check to the `main` ruleset's required checks (see [releasing.md](releasing.md#github-setup)); GitHub doesn't add it by itself. Before each build, `python utils/targets.py prepare <target>` copies that target's partition table to `esp32/partitions.csv` and writes its `build_opt.h`, because the Arduino tools only read those from the sketch folder. Both are committed for the default target, and `targets.py check` fails if either no longer matches it.

The target ID is compiled in. The device never guesses it from the hardware, since two configurations can share the same chip.

## Update states

`UpdateManager` runs every update path (manual upload, pulled download, rollback) through one state machine behind one lock, so only one update runs at a time. A pulled update and a manual upload enter at different states, then share the same checks, reboot and confirmation; every failure ends on firmware that works:

```mermaid
flowchart TD
    check["Update check: checking"] -->|"newer version for this target"| available["Update check: available"]
    available -->|"user clicks Install"| downloading
    upload(["Manual upload of a .ota.bin"]) --> receiving
    downloading --> installing
    receiving --> installing
    installing -->|"info block and signature OK"| rebooting
    rebooting --> pending["New firmware boots unconfirmed<br/>Finishing update, N s"]
    pending -->|"setup done, then 60 s up"| confirmed(["Confirmed: idle"])
    downloading -->|"transfer or checksum fails"| failed["failed: old firmware keeps running"]
    receiving -->|"transfer fails"| failed
    installing -->|"file rule or signature fails"| failed
    pending -->|"reset, crash, hang or 3-minute deadline"| rolledback["Rolled back: previous version boots"]

    classDef bad stroke:#d33,stroke-width:2px
    classDef good stroke:#3c5,stroke-width:2px
    class failed,rolledback bad
    class confirmed good
```

The update check runs 30 s after boot, every 24 h and on request (see [ota-updates.md](ota-updates.md#update-checks)). The states in lowercase are the `state` values of `GET /api/update/status`:

| State | Meaning |
| --- | --- |
| `idle` | Nothing running |
| `receiving` / `downloading` | Writing an uploaded or downloaded file into the inactive slot |
| `installing` | Checking the info block, then the signature (`Update.end()`) |
| `rebooting` | Done; the device restarts from `loop()` after the response is sent (`Restart`) |
| `failed` | The last attempt failed; `error` and `message` say why. The running firmware is unchanged |

Update checks have their own state (`never`, `checking`, `up_to_date`, `available`, `no_update`, `needs_manual`, `failed`). Checks and downloads run in background tasks, so the web server stays responsive. The device checks 30 s after boot, then every 24 h plus up to 1 h, and on request. Wi-Fi scan and connect are refused while an update is writing.

An upload that receives no data for 30 s is cancelled with `Update.abort()`. As soon as writing starts, the inactive slot's record is marked invalid.

## Rollback

The core enables the bootloader's one-shot rollback. New firmware boots as **pending**, and if the device resets before the firmware confirms itself, the bootloader marks it invalid and boots the other slot.

- `verifyRollbackLater()` is overridden to return `true`. Otherwise the core would confirm every new firmware before `setup()` even runs.
- **Health check** (`FirmwareSlots`): once `setup()` has finished (`firmwareSlots.setupDone()`) and the firmware has run 60 s more, it calls `esp_ota_mark_app_valid_cancel_rollback()`. Reaching the home Wi-Fi network is not required.
- **Deadline:** an `esp_timer` rolls back and reboots if the firmware isn't confirmed 3 minutes after boot. The loop watchdog is on while unconfirmed, so a hang also resets and rolls back.
- While unconfirmed, reboot, settings save, factory reset, updates and manual rollback answer `409 "Finishing update, please wait N s"`.

**Slot records** (`sys`): for each slot, whether it holds known-good firmware, its version, target and SHA-256, plus why the last switch happened (`update`, `manual_rollback`, `auto_rollback` or `usb`). On every boot the running firmware's SHA-256 is compared with them:

- An install was pending for the other slot, but this slot is running: the bootloader rolled back. That slot is marked invalid, and the switch is recorded as `auto_rollback`.
- The running firmware matches no record, and no install is pending: it was **flashed over USB**. The records are reset, and its target becomes the **home target**.

**Manual rollback** (`POST /api/update/rollback`) is offered only when the firmware is confirmed, the last switch was `update` and the other slot's record is valid. It calls `Update.rollBack()`, records a pending `manual_rollback` and reboots. The older firmware then goes through the same health check.

**Home target:** the configuration the device was USB-flashed with. Updates never change it; when the running target differs, `/api/status` reports `target_mismatch` and the UI warns.

## Generated files

Committed, never edited by hand; CI fails when they are out of date.

| File | Made by | From | Contains |
| --- | --- | --- | --- |
| `esp32/WebAssets.h` | `utils/build_assets.py` | `web-interface/` | Every page, script and style, minified and gzipped, with its URL, MIME type and content hash (the ETag). Pages link their CSS/JS with `?v=<content hash>` |
| `esp32/Version.h` | `utils/build_assets.py` | `VERSION` | `FIRMWARE_VERSION` and its parts |
| `esp32/OtaPublicKey.h` | `utils/keygen.py` | Public keys | The trusted release keys |
| `esp32/OtaPublicKey.local.h` (gitignored) | `utils/keygen.py --dev` | Dev key | Makes a build a dev build |

`build_assets.py` output is byte-identical on every machine:

- gzip uses the pinned `zopfli`, with no timestamp or file name;
- JavaScript is minified with `rjsmin`, HTML and CSS with `css-html-js-minify`;
- all versions are pinned in `utils/requirements.txt`;
- `{{VERSION}}` in the HTML is replaced from `VERSION`.

The commit hash is never in a committed file: release builds get it from CI's `build_opt.h`, and local builds show `dev`.

The web server serves each file with `Content-Encoding: gzip` and its content hash as `ETag`, and answers `304 Not Modified` itself when `If-None-Match` matches. Pages get `Cache-Control: no-cache`, so the browser revalidates them on every visit. Pages link their CSS/JS as `file?v=<content hash>`; a request whose `v` matches gets `Cache-Control: public, max-age=31536000, immutable`, so the browser reuses those files without asking. New content means a new hash, a new URL and a new page ETag, so a browser never runs stale JavaScript after an update. A request without `v`, or with an old one, falls back to `no-cache`.
