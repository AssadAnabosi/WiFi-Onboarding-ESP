# ESP32 WiFi Onboarding (ESP-IDF)

A native **ESP-IDF** onboarding web interface for the ESP32, designed as a
boilerplate to help you quickly get started with your IoT projects. It lets a
user connect to the device and configure which WiFi network it should join.

The entire firmware — including the web interface — is built and flashed with
the standard `idf.py` toolchain. The web assets are minified, gzipped and
**embedded directly into the application binary**, so there is no separate
filesystem image and no third-party upload tool to deal with.

## Features

- **Scan and Connect** — scan for available WiFi networks and connect the ESP
  to your chosen network (including **hidden** networks).
- **WPA2-Enterprise** — connect to enterprise (PEAP/MSCHAPv2) networks by
  entering credentials as `identity|password`.
- **Access Point Mode** — the ESP can create its own WiFi Access Point for
  direct configuration.
- **Auto-Reconnect** — credentials are persisted in **NVS**, so the device
  reconnects to the last network automatically after a reboot.
- **Responsive Web Interface** — lightweight HTML/CSS/JS, embedded in the
  firmware and served gzip-compressed.
- **mDNS** — reachable at `config.local` in addition to its IP.

## Usage

Once configured, access the device through your own WiFi network or directly via
its Access Point at `http://192.168.4.1` or `http://config.local`.

> This project is intended as a **boilerplate**: set up WiFi management quickly
> and build your own IoT application on top of it.

## Prerequisites

- **ESP-IDF v5.1 or newer**, installed and exported on your `PATH`
  (so `idf.py` is available). See the official
  [ESP-IDF Get Started guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html).
- **Python 3** (bundled with ESP-IDF) for the web asset build step.

## Build & Flash

```bash
# 1. Select the target chip (once per checkout)
idf.py set-target esp32

# 2. Minify + gzip the web interface into main/www/*.gz (embedded by CMake)
python utils/build_web.py

# 3. Build, flash and open the serial monitor
idf.py -p <PORT> flash monitor
```

Re-run `python utils/build_web.py` whenever you edit anything under
`web-interface/`, then rebuild.

- To wipe stored settings (NVS) and start fresh: `idf.py -p <PORT> erase-flash`.
- Exit the serial monitor with `Ctrl-]`.

The `espressif/mdns` dependency is fetched automatically by the IDF Component
Manager on the first build.

## Default settings

- **IP Address:** `192.168.4.1`
- **SSID:** `ESP-Access-Point`
- **Password:** `12345678`
- **Channel:** `6`
- **Hidden:** `false`
- **Hostname:** `ESP-IoT`

## REST API

All endpoints return JSON. Page navigation is served from the embedded assets.

| Method | Endpoint          | Description                                  |
| ------ | ----------------- | -------------------------------------------- |
| GET    | `/api`            | Board / health info                          |
| GET    | `/api/status`     | Current SoftAP + station status              |
| GET    | `/api/scan`       | Available WiFi networks (RSSI-sorted)        |
| GET    | `/api/settings`   | Current AP + device settings                 |
| POST   | `/api/connect`    | Connect to a network `{ssid, password}`      |
| POST   | `/api/disconnect` | Disconnect and clear saved credentials       |
| POST   | `/api/settings`   | Update settings (reboots to apply)           |
| POST   | `/api/reboot`     | Reboot the device                            |
| POST   | `/api/reset`      | Factory reset (clears NVS) and reboot        |

## File Structure

```
📁 main                  # ESP-IDF application component
│   📄 main.c            # app_main: NVS, WiFi, HTTP server, mDNS
│   📄 wifi_manager.*    # station + SoftAP, scan, WPA2-PSK/Enterprise
│   📄 config_store.*    # NVS-backed settings
│   📄 http_server.*     # esp_http_server: REST API + static serving
│   📄 idf_component.yml # managed dependencies (espressif/mdns)
│   📁 www               # generated *.gz assets, embedded into the firmware
📁 web-interface         # editable source: HTML, CSS, JS, favicon
📁 utils
│   📄 build_web.py      # minify + gzip web-interface/ -> main/www/*.gz
📄 partitions.csv        # OTA-ready partition table (ota_0 / ota_1)
📄 sdkconfig.defaults    # default project configuration
```

## Notes

- **No writable filesystem is required.** The web UI is embedded in the app
  image and configuration lives in NVS.
- The partition table is **OTA-ready** (two app slots). A future
  [`esp_https_ota`](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_https_ota.html)
  update would refresh the firmware *and* the embedded web UI together,
  atomically, with rollback.

## Dashboard Screenshots

<img width="645" height="2795" alt="config local_home(iPhone 14 Pro Max)" src="https://github.com/user-attachments/assets/45e8e4b1-ebd4-4c75-9d7c-4aaee84054d2" />

<img width="645" height="2795" alt="config local_scan(iPhone 14 Pro Max)" src="https://github.com/user-attachments/assets/125d1c18-506c-40c7-898d-b8cf1026e4db" />

<img width="645" height="3714" alt="config local_settings(iPhone 14 Pro Max)" src="https://github.com/user-attachments/assets/e2b67543-4c3f-431f-94f0-7bda48c5720d" />
