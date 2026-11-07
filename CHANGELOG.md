# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Project documentation in `docs/`, plus `LICENSE` (MIT), `CONTRIBUTING.md`, `SECURITY.md` and `THIRD_PARTY_LICENSES.md`.

### Changed

- The sketch is ESP32-only and lives in `esp32/` (`esp32/esp32.ino`).
- WPA2-Enterprise uses the ESP32 core 3.x `esp_eap_client` API.

### Removed

- ESP8266 support. The last ESP8266-compatible version is on the `legacy/esp8266` branch.
