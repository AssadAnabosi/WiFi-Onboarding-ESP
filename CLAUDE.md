# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

Arduino-framework boilerplate for Wi-Fi onboarding on ESP32/ESP8266: the device boots, tries saved STA credentials, falls back to a SoftAP (`192.168.4.1`, mDNS `config.local`), and serves a small web UI for scanning/connecting to networks and editing AP settings. Intended as a starting point for other IoT projects.

## Build / flash workflow

There is no CLI build, lint, or test suite — everything goes through Arduino IDE 2.

1. Board packages: esp32 by Espressif **3.x**, or esp8266 community core.
2. Libraries: ArduinoJson **v7**; ESP32 also needs ESPAsyncWebServer + AsyncTCP (both by ESP32Async).
3. Generate the filesystem image: `python utils/webMinifier.py` (interactive — prompts for target `1`=esp8266, `2`=esp, and to confirm clearing the output dir; auto-offers to `pip install css-html-js-minify`). It copies `web-interface/` into `esp/data/www/` or `esp8266/data/www/` and minifies HTML/CSS in place (JS and `.ico` are copied unminified).
4. Upload `data/` with the arduino-littlefs-upload plugin (command palette → "LittleFS to Pico/ESP8266/ESP32"; serial monitor must be closed), then compile/upload the `.ino`.

`*/data/` is gitignored — always edit `web-interface/`, never the generated copies. Any web UI change requires re-running the minifier and re-uploading LittleFS.

## Architecture

Two parallel sketch directories with the same class layout:

- `esp/` — the maintained, "generic" sketch. Uses `#ifdef ESP32` / `ESP8266` throughout but is only tested on ESP32. Uses **ESPAsyncWebServer** with `AsyncCallbackJsonWebHandler` for JSON POST bodies (`AsyncJson.h` must be included after `ESPAsyncWebServer.h`).
- `esp8266/` — older ESP8266-only variant using the synchronous `ESP8266WebServer` (`std::bind` handlers). It has diverged from `esp/` (e.g. no WPA2-Enterprise); changes to shared behavior usually need to be ported by hand to both.

Classes (in each sketch dir):

- `ConfigStorage` — static methods over a single `/config.json` on LittleFS (keys: `wifi_ssid`, `wifi_password`, `ap_ssid`, `ap_password`, `ap_channel`, `ap_hidden`, `ap_status`, `hostname`). Every getter re-opens and re-parses the file; `begin()` creates it with defaults if missing; `factoryReset()` deletes and recreates it. Adding a setting means updating defaults in `begin()`, a getter, `saveSettings`, the GET/POST `/api/settings` handlers, and `settings.js`.
- `WiFiManager` — `autoConnect()` (3 attempts with saved creds, then SoftAP if `ap_status` is true **or** STA failed) and `connectToNetwork()` (blocking ~10s wait; saves credentials on success). WPA2-Enterprise is encoded in the password field as `username|password` (ESP32 only) — the stored config format is unchanged.
- `WebServerManager` — routes pages (`/`, `/home`, `/scan`, `/settings` → `/www/*.html` on LittleFS), static `/css/`, `/js/`, and the JSON API: `GET /api` (board info; also matches `/api/health` used by the frontend's `checkConnection()`), `GET /api/status`, `GET /api/scan`, `POST /api/connect`, `POST /api/disconnect`, `GET|POST /api/settings` (POST restarts the device), `POST /api/reboot`, `POST /api/reset`. Responses use `{success, message}` via `sendMessage`/`sendJson`.
- `esp/CoreSerial` — optional debug helper that prefixes Serial lines with the core ID (`#define Serial SerialCore`).

Frontend (`web-interface/`): plain HTML/CSS/vanilla JS, no build step or framework. `js/index.js` is shared by every page and provides `apiCall()` (XHR wrapper that drives the status banner and toasts) and `checkConnection()`, which each page calls on load and which then invokes the page's own `load()` (and `draw()` on errors) if defined. Use `esc()` when injecting API strings into `innerHTML`.

## Other notes

- Default AP: SSID `ESP-Access-Point`, password `12345678`, channel 6, hostname `ESP-IoT`.
- Untracked `main/`, `managed_components/`, `build/`, `sdkconfig*`, `dependencies.lock` in the working tree are ESP-IDF build leftovers (see remote `esp-idf` branch), not part of the Arduino sketches on this branch.

## Working conventions

- Commit messages follow the Conventional Commits spec: https://www.conventionalcommits.org/en/v1.0.0/#specification (`type(scope): description`, optional body/footers, `!` or `BREAKING CHANGE:` for breaking changes).
- All commits must be authored as `Assad Anabosi <assad.anabosi@icloud.com>`.
- Do not add AI co-author trailers (e.g. `Co-Authored-By: Claude ...`) or any other AI attribution to commit messages.
- Whenever a phase or item of a plan/spec is finished, mark it as completed in that plan/spec file.
- Do not assume. If something is debatable, requires a choice the docs/specs don't specify, or conflicts with the docs, stop and ask the user — present the options along with your recommendation — before proceeding.
- Context management: when nearing the end of the context window, write a handoff for the next session based on the current step in the spec file (what's done, what's in progress, what's next). Never auto-compact unless the current step is fully done.
