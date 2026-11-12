# ESP32 WiFi Onboarding

A boilerplate for ESP32 IoT projects: a small web interface for connecting the device to Wi-Fi, plus signed over-the-air (OTA) updates that never lose your settings.

> ESP8266 support has been removed. The last ESP8266-compatible version lives on the [`legacy/esp8266`](https://github.com/AssadAnabosi/WiFi-Onboarding-ESP/tree/legacy/esp8266) branch.

## Features

- **Scan and connect**: find nearby Wi-Fi networks and connect the ESP32 to one, hidden networks included.
- **Access point mode**: the ESP32 runs its own access point for direct setup at `192.168.4.1` or `config.local`.
- **Browser install**: first flash from a web page ([ESP Web Tools](https://esphome.github.io/esp-web-tools/)), then send the Wi-Fi network over USB ([Improv](https://www.improv-wifi.com/serial/)).
- **Auto-reconnect**: Wi-Fi credentials and settings are saved and survive reboots, power loss and updates.
- **Signed OTA updates**: upload a signed firmware file from the Settings page, or install a new release from GitHub after the device finds one.
- **Automatic rollback**: a failed update returns to the previous firmware; a manual rollback button is also available.
- **WPA2-Enterprise** (not verified): enter `username|password` as the password to join PEAP/MSCHAPv2 networks.
- **Responsive web interface**: light and dark themes, plain HTML, CSS and JavaScript, embedded in the firmware. Preview it on your PC against a fake or real device with `utils/ui_preview.py`.

## Quick start

1. Open the [browser installer](https://assadanabosi.github.io/WiFi-Onboarding-ESP/) in Chrome or Edge, connect the board over USB and click **Connect**, then **Install** (this wipes everything on the board). Afterwards it can send your Wi-Fi network to the device over USB, and you're done.

   Other browsers: download the `.factory.bin` file for your board from the [latest release](https://github.com/AssadAnabosi/WiFi-Onboarding-ESP/releases/latest) and flash it with `esptool.py write_flash 0x0 wifi-onboarding-<version>-esp32-4mb.factory.bin` (see [getting-started.md](docs/getting-started.md)).

2. Otherwise, join the `ESP-Access-Point` Wi-Fi network (password `12345678`).
3. Open `http://config.local` or `http://192.168.4.1` and connect the device to your network.

To build it yourself instead, see [docs/local-development.md](docs/local-development.md).

## Default settings

| Setting | Value |
| --- | --- |
| IP address | `192.168.4.1` |
| AP SSID | `ESP-Access-Point` |
| AP password | `12345678` |
| AP channel | `6` |
| AP hidden | `false` |

Change the default AP password after setup: it is public.

## Documentation

- [Getting started](docs/getting-started.md): supported boards and first flash
- [OTA updates](docs/ota-updates.md): uploading, update checks and failure behaviour
- [Make it yours](docs/make-it-yours.md): checklist for forks
- [Local development](docs/local-development.md): building, flashing and testing
- [Releasing](docs/releasing.md): versioning and the release pipeline
- [Security](docs/security.md): signing, trusted keys and known risks
- [Architecture](docs/architecture.md): code layout, flash layout, settings storage and rollback
- [API](docs/api.md): REST endpoints
- [Roadmap](docs/roadmap.md): future work

## License

[MIT](LICENSE). Third-party components are listed in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
