# ESP32 WiFi Onboarding

A boilerplate for ESP32 IoT projects: a small web interface for connecting the device to Wi-Fi, plus signed over-the-air (OTA) updates that never lose your settings.

> ESP8266 support has been removed. The last ESP8266-compatible version lives on the [`legacy/esp8266`](https://github.com/AssadAnabosi/WiFi-Onboarding-ESP/tree/legacy/esp8266) branch.

## Features

- **Scan and connect**: find nearby Wi-Fi networks and connect the ESP32 to one, hidden networks included.
- **Access point mode**: the ESP32 runs its own access point for direct setup at `192.168.4.1` or `config.local`.
- **Auto-reconnect**: Wi-Fi credentials and settings are saved and survive reboots, power loss and updates.
- **Signed OTA updates**: upload a signed firmware file from the Settings page, or install a new release from GitHub after the device finds one.
- **Automatic rollback**: a failed update returns to the previous firmware; a manual rollback button is also available.
- **WPA2-Enterprise** (not verified): enter `username|password` as the password to join PEAP/MSCHAPv2 networks.
- **Responsive web interface**: plain HTML, CSS and JavaScript, embedded in the firmware.

## Quick start

1. Download the `.factory.bin` file for your board from the [latest release](https://github.com/AssadAnabosi/WiFi-Onboarding-ESP/releases/latest).
2. Flash it over USB (this wipes everything on the board):

   ```sh
   esptool.py write_flash 0x0 wifi-onboarding-<version>-esp32-4mb.factory.bin
   ```

3. Join the `ESP-Access-Point` Wi-Fi network (password `12345678`).
4. Open `http://config.local` or `http://192.168.4.1` and connect the device to your network.

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
- [Architecture](docs/architecture.md): flash layout, settings storage and rollback
- [API](docs/api.md): REST endpoints
- [Roadmap](docs/roadmap.md): future work

## Dashboard screenshots

<img width="645" height="2795" alt="config local_home(iPhone 14 Pro Max)" src="https://github.com/user-attachments/assets/45e8e4b1-ebd4-4c75-9d7c-4aaee84054d2" />

<img width="645" height="2795" alt="config local_scan(iPhone 14 Pro Max)" src="https://github.com/user-attachments/assets/125d1c18-506c-40c7-898d-b8cf1026e4db" />

<img width="645" height="3714" alt="config local_settings(iPhone 14 Pro Max)" src="https://github.com/user-attachments/assets/e2b67543-4c3f-431f-94f0-7bda48c5720d" />

## License

[MIT](LICENSE). Third-party components are listed in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
