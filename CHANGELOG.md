# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- Firmware code reorganised into `esp32/src/` (`storage`, `wifi`, `web`, `ota`, `system`), with the two largest files split into focused modules and the modules wired together in `esp32.ino` instead of through globals. The API, settings and update behaviour are unchanged.
- New firmware arms its rollback deadline at the start of `setup()`, before connecting to Wi-Fi, so a hang while connecting also rolls back.
- Serial log lines start with a module tag (`[wifi]`, `[ota]`, ...).
- `esp32/.clang-format` sets the C++ style.

### Fixed

- A failed connection to a new network (for example a wrong password) no longer leaves the device off Wi-Fi until the next reboot: it reconnects to the saved network, or starts its access point if that fails too.

## [0.2.1] - 2026-11-08

### Fixed

- Signal bars on the Scan page are coloured on the device again. The minified stylesheet dropped the quotes in the level selectors, which made them invalid, so only the preview showed the colours.

### Changed

- The Scan list no longer repeats the signal strength in dBm next to the channel and MAC; hover the signal bars to see it.

## [0.2.0] - 2026-11-08

A new web interface with light and dark themes.

### Added

- Light and dark themes. The first visit follows the system setting (light when there is none), and the toggle in the header remembers your choice.
- A show/hide button in every password field: the access point password and the network and enterprise sign-in dialogs.
- "Dev build" and "Dev update available" badges on the Firmware card, and a dev-update note on Home, in the warning colour.
- `check.dev` in `GET /api/update/status`: `true` when the available update came from the local dev manifest rather than a GitHub release.
- `utils/ui_preview.py`: preview the web interface on a PC against a fake device with switchable states (`--mock`) or a real one, without flashing.

### Changed

- Redesigned web interface: connection status as a pill in the header, tabs, status cards with aligned rows, Scan as one list on every screen size with signal bars, Settings grouped with help under each field and a Device actions card, a drop zone for update files, native dialogs and toasts at the bottom.
- The "Finishing update" bar and the update and mismatch notes slide in and out.
- Check for updates and update uploads are also locked while an update finishes.
- When the device refuses a request (for example while an update finishes), the page shows its message and stays "Connected" instead of showing "Disconnected".
- The access point password is hidden in Settings until you show it.
- The web interface is one stylesheet and fewer scripts: 12 embedded files instead of 18, about 11% smaller.

## [0.1.0] - 2026-11-08

First release of the ESP32-only boilerplate with signed over-the-air updates.

### Added

- Signed OTA updates (ECDSA P-256): upload a `.ota.bin` from Settings → Firmware, with checks in the browser before anything is sent.
- Update checks against the project's GitHub Releases 30 s after boot, every 24 h and on request; the user confirms the install. Home shows an "Update available" badge.
- Automatic rollback when new firmware crashes, hangs or doesn't stay up, and a manual "Roll back" button for the previous version.
- A "Finishing update" countdown while new firmware confirms itself; reboot, save, reset and new updates wait for it.
- Protection against wrong files: other chip, flash layout, project or configuration, factory images and unsigned files.
- A warning when the device runs another configuration than the one it was set up with.
- Version in the footer of every page, and a Firmware card with version, build date and target.
- Protection against cross-site requests on every POST route.
- Release pipeline: signed `.ota.bin` and `.factory.bin` per target, `manifest.json` and `SHA256SUMS`, published from a protected GitHub Environment.
- Tools in `utils/`: `build_assets.py`, `keygen.py`, `sign.py`, `targets.py`, `release.py`, with tests.
- Project documentation in `docs/`, plus `LICENSE` (MIT), `CONTRIBUTING.md`, `SECURITY.md` and `THIRD_PARTY_LICENSES.md`.

### Changed

- The sketch is ESP32-only and lives in `esp32/` (`esp32/esp32.ino`).
- Settings are stored in NVS instead of `config.json` on LittleFS, and survive every update and rollback. Flashing v0.1.0 starts from empty settings.
- The web interface is embedded in the firmware, minified and gzipped, and revalidated by the browser after each update.
- Wi-Fi scan and connect run in the background; the API answers `202` and the UI polls for the result.
- WPA2-Enterprise uses the ESP32 core 3.x `esp_eap_client` API (not verified on a real network).
- `GET /api/health` replaces `GET /api`.
- Requires ESP32 Arduino core 3.3.8 or newer (pinned to 3.3.12).

### Removed

- ESP8266 support. The last ESP8266-compatible version is on the `legacy/esp8266` branch.
- LittleFS and the separate filesystem image.
