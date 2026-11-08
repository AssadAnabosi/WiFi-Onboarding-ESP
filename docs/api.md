# API

The device serves a small REST API on port 80; the web interface uses nothing else. Request and response bodies are JSON unless noted.

## Conventions

- **Messages:** most responses are `{"success": <bool>, "message": "<text>"}`. `success` is true for any 2xx status.
- **Long operations** (Wi-Fi scan and connect, update checks and installs) start with `202 Accepted` and run in the background; poll the matching GET route for the result.
- **POST requests** must send `Content-Type: application/json` (an empty body is fine where no body is needed) or `application/octet-stream` for an upload. The `Host` header must be one of the device's own names, and the `Origin` header, if any, its own pages. Otherwise the answer is `403` (see [security.md](security.md#cross-site-request-protection)).
- **Restarts** (settings, reset, reboot, install, rollback) happen 0.5–1 s after the response is sent.
- **While new firmware is unconfirmed**, the routes that would restart the device or start another update answer `409` with "Finishing update, please wait N s".

Example:

```sh
curl http://192.168.4.1/api/status
curl -X POST -H "Content-Type: application/json" http://192.168.4.1/api/update/check
```

## Status and health

### `GET /api/health`

Liveness check, used by the UI to wait for a reboot. Returns board information:

```json
{
  "success": true,
  "message": "What are you doing here ?",
  "data": {
    "platform": "ESP32",
    "mac": "AA:BB:CC:DD:EE:FF",
    "chip_info": { "chip_model": "ESP32-D0WD-V3", "chip_revision": 301, "cores": 2, "cpu_freq_mhz": 240 },
    "memory": { "flash_size_mb": 4, "free_heap_kb": 180 },
    "software": { "sdk_version": "...", "arduino_core": "3.3.12" },
    "firmware": { "version": "0.1.0", "dev": false }
  }
}
```

### `GET /api/status`

Network and firmware state:

| Field | Meaning |
| --- | --- |
| `softAP.enabled`, `softAP.ssid`, `softAP.ip` | The device's own access point |
| `station.connected`, `station.ssid`, `station.ip` | The connection to a Wi-Fi network |
| `station.attempt.state`, `station.attempt.ssid` | Last `POST /api/connect`: `none`, `connecting`, `succeeded` or `failed` |
| `version`, `build`, `commit`, `dev` | Firmware version, build date, commit (`dev` for local builds), dev-build flag |
| `target`, `home_target`, `target_mismatch` | Running build target, the target the device was set up with, and whether they differ |
| `previous_version`, `rollback_available` | Version in the other slot, and whether manual rollback is offered |
| `project`, `chip_id`, `layout` | Project slug, chip ID and partition-table SHA-256, used by the browser to check a file before uploading it |

## Wi-Fi

### `GET /api/scan`

Returns the nearby networks, strongest first:

```json
[
  { "ssid": "Home", "hidden": false, "rssi": -52, "enc": "WPA2-PSK", "mac": "AA:BB:CC:DD:EE:FF", "channel": 6, "connected": true }
]
```

When no recent results exist (older than 15 s), it starts a scan and answers `202 "Scanning"`; poll again until the list arrives. `409` while connecting to a network or while an update is running.

### `POST /api/connect`

```json
{ "ssid": "Home", "password": "secret" }
```

Answers `202 "Connecting to Home"`, then connects in the background (about 10 s). Poll `GET /api/status` → `station.attempt.state`. On success the credentials are saved. On failure the saved network is kept and the device reconnects to it (up to about 10 s more, before the state turns `failed`); if that fails too, it starts its access point so it stays reachable. For WPA2-Enterprise, send `username|password` as the password. `400` without `ssid`; `409` while scanning, already connecting, or while an update is running.

### `POST /api/disconnect`

Disconnects from the Wi-Fi network.

## Settings

### `GET /api/settings`

```json
{ "ap_ssid": "ESP-Access-Point", "ap_password": "12345678", "ap_channel": 6, "ap_hidden": false, "ap_status": true, "hostname": "ESP-IoT" }
```

### `POST /api/settings`

Saves all six fields of `GET /api/settings`, then reboots. All fields are required; otherwise `400` with `"fields"` listing the missing ones.

### `POST /api/reboot`

Reboots the device.

### `POST /api/reset`

Factory reset: clears the Wi-Fi and access point settings and reboots with the defaults. The installed firmware is not changed.

## Updates

See [ota-updates.md](ota-updates.md) for the behaviour behind these routes.

### `GET /api/update/status`

```json
{
  "state": "idle",
  "source": "upload",
  "progress": 0,
  "confirming": false,
  "check": { "state": "available", "version": "0.2.0", "notes": "...", "dev": false }
}
```

| Field | Meaning |
| --- | --- |
| `state` | `idle`, `receiving`, `downloading`, `installing`, `rebooting` or `failed` |
| `source` | `upload` or `pull` |
| `progress` | Percent written |
| `error`, `message` | Why the last update failed (only when `state` is `failed`) |
| `confirming`, `confirm_in_s` | New firmware still unconfirmed, and seconds left |
| `check.state` | `never`, `checking`, `up_to_date`, `available`, `no_update`, `needs_manual` or `failed` |
| `check.version`, `check.notes` | The available update and its release notes (plain text, up to 4 KB) |
| `check.dev` | With `available`: `true` when the update came from the local dev manifest (dev builds with `keygen.py --dev-manifest-url`), `false` for a GitHub release |
| `check.error`, `check.message` | Why the last check didn't find an update |

### `POST /api/update`

Manual upload. The body is the raw `.ota.bin` file with `Content-Type: application/octet-stream` (not a form upload), so the device knows the exact size before it writes anything:

```sh
curl -X POST -H "Content-Type: application/octet-stream" \
     --data-binary @wifi-onboarding-0.2.0-esp32-4mb.ota.bin http://192.168.4.1/api/update
```

Query parameters, sent after the user confirmed a warning:

- `allow_downgrade=1`: install a version that is not newer;
- `allow_target_change=1`: install another configuration's file.

`200 "Update installed, rebooting"` on success. On failure: `{"success": false, "error": "<reason>", "message": "<text>"}` with `400` (`409` while another update runs or the firmware is unconfirmed, `415` for another content type).

### `POST /api/update/check`

Checks the release manifest now. `202`, then poll `GET /api/update/status` → `check`. `409` while a check or update is already running.

### `POST /api/update/install`

Downloads and installs the update the last check found. `202 "Downloading vX.Y.Z"`, then poll `GET /api/update/status`; the device reboots when done. `409` when no update is available, or while another update runs or the firmware is unconfirmed.

### `POST /api/update/rollback`

Boots the previous version, only when `rollback_available` in `/api/status` is true. `200 "Rolling back to vX.Y.Z, rebooting"`, otherwise `409`.

## Errors

| Status | When |
| --- | --- |
| `400` | Missing fields, or an update file that failed its checks |
| `403` | Cross-site protection: wrong content type, host or origin |
| `404` | Unknown route |
| `409` | Busy (scan, connect, update running) or finishing an update |
| `411` | Upload without a body |
| `415` | Upload with a content type other than `application/octet-stream` |
| `500` | The device couldn't start the operation (e.g. out of memory) |

Update error reasons (`error`, `check.error`):

| Reason | Meaning |
| --- | --- |
| `invalid_signature` | Unsigned, or signed by an untrusted key |
| `invalid_file` | Not an update file, or a damaged one |
| `factory_image` | A `.factory.bin` was sent |
| `file_too_large` | Doesn't fit the firmware slot |
| `wrong_target` | Another chip or flash layout, another configuration without `allow_target_change`, or no update for this device's target |
| `wrong_project` | Another project's file or manifest |
| `not_newer` | Not newer than the installed version, without `allow_downgrade` |
| `no_internet` | Not connected to a Wi-Fi network |
| `not_enough_memory` | Less than 60 KB free for a secure download |
| `network_failure` | Server unreachable, HTTP error, invalid manifest, or the connection dropped |
| `verification_failed` | The download doesn't match the manifest, or the core's final check failed |
| `upload_cancelled` | The upload stopped or no data arrived for 30 s |
| `update_running` | Another update is in progress |
| `finishing_update` | The new firmware isn't confirmed yet |
| `manifest_too_new` | The manifest needs a newer firmware: install by hand |
| `write_failed` | Writing to flash failed |
