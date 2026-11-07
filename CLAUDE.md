# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

Arduino-framework boilerplate for Wi-Fi onboarding on ESP32: the device boots, tries saved STA credentials, falls back to a SoftAP (`192.168.4.1`, mDNS `config.local`), and serves a small web UI for scanning/connecting to networks and editing AP settings. Intended as a starting point for other IoT projects.

The `OTA` branch is implementing signed OTA updates per the spec doc https://claude.ai/artifact/SSL3ZAkp8mneuKfF5pAVp6 ("Implementation plan" lists the 20 steps; read it with the Claude Docs tools). The spec decides structure, naming and tooling going forward — e.g. LittleFS → NVS, web UI embedded in firmware — so check it before changing anything it covers.

## Build / flash workflow

There is no lint step. Python tests: `python -m unittest discover -s utils/tests` (needs `pip install -r utils/requirements.txt`).

CI (`.github/workflows/checks.yml`, every push/PR) runs those tests, reruns `build_assets.py` and fails on drift, validates target names (`python utils/targets.py check`), then compiles every `sketch.yaml` profile in its own job (`utils/targets.py prepare <target>` puts that target's partitions file and `build_opt.h` in place first) and checks the `.bin` against the target's app slot (`utils/targets.py size`, warns above 90%). Run the same commands locally before pushing.

- **Compile (CLI)**: `arduino-cli compile --profile esp32-4mb esp32` — the profile in `esp32/sketch.yaml` pins the ESP32 core (3.3.12; signing needs ≥ 3.3.8) and exact library versions, and arduino-cli installs them into its own internal cache. On this machine arduino-cli ships with the IDE at `%LOCALAPPDATA%/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe`. Add `--build-path <dir>` to keep build output out of the repo.
- **Arduino IDE** ignores `sketch.yaml`: install the same core/library versions by hand and pick the "No FS 4MB (2MB APP x2)" partition scheme (matches `esp32/partitions.csv`).
- Both the IDE and arduino-cli automatically pick up `esp32/partitions.csv` and `esp32/build_opt.h` from the sketch folder. `build_opt.h` is a gcc response file, so string macros need escaped quotes (`-DFOO=\"bar\"`); its flags apply to the core and libraries too (e.g. `-DUPDATE_SIGN` enables `Update.installSignature`).
- **Web UI**: edit `web-interface/`, then run `python utils/build_assets.py` and commit the source change together with the regenerated `esp32/WebAssets.h` and `esp32/Version.h` (never edit those by hand; CI checks for drift). Output must stay byte-identical across machines: gzip uses the pinned `zopfli` (stdlib gzip differs between zlib and zlib-ng builds), JS uses `rjsmin` (css-html-js-minify breaks our JS), HTML/CSS use css-html-js-minify. `{{VERSION}}` in HTML is replaced from the root `VERSION` file; "(dev)" is appended at runtime by `index.js` from `/api/health`.

- **Signing**: `utils/sign.py` turns `build/esp32.ino.bin` + `esp32.ino.partitions.bin` into `[firmware][128-byte info block][DER ECDSA P-256 signature zero-padded to 512]` (the core's `Update.installSignature` layout; info block = magic `WOTA`, format, firmware length, version, chip ID, SHA-256 of the 0xC00-padded partition table, target ID, project slug). `--verify FILE` checks one. `utils/keygen.py` makes keys (private keys go to gitignored `keys/`) and maintains the trusted release list in generated `esp32/OtaPublicKey.h` (`--add-public`, `--remove`, `--list`, `--check`); `keygen.py --dev` writes gitignored `esp32/OtaPublicKey.local.h`, whose presence makes a build a dev build (trusts dev + release keys, shows "(dev)"). `OtaKeys.h` merges both lists; `TrustedKeyVerifier` (`OtaVerifier.h`) tries each key.

- **Updates & rollback** (`UpdateManager`): one lock/state machine for every update path. `verifyRollbackLater()` returns true so new firmware boots *pending*; it confirms itself (`esp_ota_mark_app_valid_cancel_rollback`) once `setup()` has finished (`updateManager.setupDone()`) and it has run 60 s more; an `esp_timer` rolls it back at 3 min from boot, and the loop watchdog is on while unconfirmed. While unconfirmed, reboot, settings save, factory reset and new updates answer 409 "Finishing update, please wait N s". Slot records, home target, last-switch reason and the pending install live in the NVS `sys` namespace (`ConfigStorage`); on boot the running firmware's SHA-256 is compared with them to detect a USB flash or a rollback. Tested on hardware: rollback on reset and at the deadline both work.

## Architecture

The sketch lives in `esp32/` (`esp32.ino`). Uses **ESPAsyncWebServer** with `AsyncCallbackJsonWebHandler` for JSON POST bodies (`AsyncJson.h` must be included after `ESPAsyncWebServer.h`).

Classes:

- `ConfigStorage` — static methods over NVS via `Preferences`: namespace `cfg` (user settings: `wifi_ssid`, `wifi_password`, `ap_ssid`, `ap_password`, `ap_channel`, `ap_hidden`, `ap_status`, `hostname`; cleared by factory reset together with `esp_wifi_restore()`) and `sys` (system records, kept). All setting names live in one list at the top of `ConfigStorage.cpp`; every read takes a default (from `ProjectConfig.h`), so new settings need no migration. Setting-name rule (CONTRIBUTING.md): never rename, reuse or change a name's type; max 15 chars; retired names stay listed as reserved. Never log passwords. Adding a setting means: a key in that list, getter/setter, the GET/POST `/api/settings` handlers, and `settings.js`.
- `WiFiManager` — `autoConnect()` (3 attempts with saved creds, then SoftAP if `ap_status` is true **or** STA failed) and `connectToNetwork()` (blocking ~10s wait; saves credentials on success). WPA2-Enterprise is encoded in the password field as `username|password` (configured via the core 3.x `esp_eap_client` API) — the stored config format is unchanged.
- `WebServerManager` — serves the embedded UI from `WebAssets.h` (gzip, `ETag` = content hash, `Cache-Control: no-cache`, its own `304` handling) and the JSON API: `GET /api/health`, `GET /api/status`, `GET /api/scan` (202 while a background scan runs), `POST /api/connect` (202; result polled via `/api/status` → `station.attempt`), `POST /api/disconnect`, `GET|POST /api/settings`, `POST /api/reboot`, `POST /api/reset`. Restarts are scheduled and run from `loop()` after the response. A global middleware protects every POST (content type JSON/octet-stream, Host must be the device's own name, Origin must be its own pages). Responses use `{success, message}` via `sendMessage`/`sendJson` (any 2xx = success). Note `server.on("/a")` also matches `/a/...`, so register longer routes first.
- `esp32/CoreSerial` — optional debug helper that prefixes Serial lines with the core ID (`#define Serial SerialCore`).

Frontend (`web-interface/`): plain HTML/CSS/vanilla JS, no build step or framework. `js/index.js` is shared by every page and provides `apiCall()` (XHR wrapper that drives the status banner and toasts) and `checkConnection()`, which each page calls on load and which then invokes the page's own `load()` (and `draw()` on errors) if defined. Use `esc()` when injecting API strings into `innerHTML`.

## Other notes

- Defaults (AP SSID/password/channel, hostname, mDNS setup name), the GitHub owner/repo and `PROJECT_SLUG` live in `esp32/ProjectConfig.h`; the build target (`OTA_TARGET_ID`, `OTA_TARGET_HEADER` → `esp32/targets/<target>.h`) comes from `build_opt.h`. Single source for the version: root `VERSION`.
- Untracked `main/`, `managed_components/`, `build/`, `sdkconfig*`, `dependencies.lock` in the working tree are ESP-IDF build leftovers, not part of the Arduino sketches on this branch.

## Working conventions

- Commit messages follow the Conventional Commits spec: https://www.conventionalcommits.org/en/v1.0.0/#specification (`type(scope): description`, optional body/footers, `!` or `BREAKING CHANGE:` for breaking changes).
- All commits must be authored as `Assad Anabosi <assad.anabosi@icloud.com>`.
- Do not add AI co-author trailers (e.g. `Co-Authored-By: Claude ...`) or any other AI attribution to commit messages.
- Whenever a phase or item of a plan/spec is finished, mark it as completed in that plan/spec file.
- Do not assume. If something is debatable, requires a choice the docs/specs don't specify, or conflicts with the docs, stop and ask the user — present the options along with your recommendation — before proceeding.
- Context management: when nearing the end of the context window, write a handoff for the next session based on the current step in the spec file (what's done, what's in progress, what's next). Never auto-compact unless the current step is fully done.
