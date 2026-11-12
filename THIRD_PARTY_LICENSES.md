# Third-Party Licenses

The firmware built from this repository includes the following third-party software. Exact versions are pinned in [`esp32/sketch.yaml`](esp32/sketch.yaml).

| Component | Author | License | Source |
| --- | --- | --- | --- |
| Arduino core for ESP32 (includes ESP-IDF) | Espressif Systems | LGPL-2.1 (core), Apache-2.0 (ESP-IDF) | https://github.com/espressif/arduino-esp32 |
| ArduinoJson | Benoît Blanchon | MIT | https://github.com/bblanchon/ArduinoJson |
| ESPAsyncWebServer | ESP32Async | LGPL-3.0 | https://github.com/ESP32Async/ESPAsyncWebServer |
| AsyncTCP | ESP32Async | LGPL-3.0 | https://github.com/ESP32Async/AsyncTCP |
| Lucide icons (Wi-Fi, Wi-Fi off, Wi-Fi cog, radio tower, radar, house, settings, CPU, eye, eye off; in the web interface. Chevron down; in the browser installer) | Lucide Contributors | ISC | https://github.com/lucide-icons/lucide |

The browser installer page (`installer/`, published to GitHub Pages) also serves:

| Component | Author | License | Source |
| --- | --- | --- | --- |
| ESP Web Tools (bundles esptool-js, Lit, Material Web, pako and improv-wifi-serial-sdk) | ESPHome / Open Home Foundation and contributors | Apache-2.0 (bundled parts under their own licenses) | https://github.com/esphome/esp-web-tools |

The full license texts are available in each component's source repository.

The LGPL components are linked into the firmware. The complete source of this project is published in this repository under the MIT license, so the firmware can be rebuilt and relinked against modified versions of those components.
