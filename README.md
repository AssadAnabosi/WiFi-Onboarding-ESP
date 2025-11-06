# ESP8266/ESP32 WiFi Onboarding

This project provides a simple onboarding web interface for the ESP8266/ESP32, designed as a boilerplate to help you quickly get started with your IoT projects.
Allowing the user to connect it to the ESP and configure what network to connect to.

**_from this point on we will be referring to the ESP8266/ESP32 as ESP_**

## Features

- **Scan and Connect**: Scan for available Wi-Fi networks and connect the ESP to your chosen network.
- **Access Point Mode**: Configure the ESP to create its own Wi-Fi Access Point for direct connection.
- **Auto-Reconnect**: Save Wi-Fi credentials using LittleFS, allowing the device to automatically reconnect to the last used network after reboot.
- **Responsive Web Interface**: Built with HTML, CSS, and JavaScript, the interface is lightweight, user-friendly, and works across devices.

## Usage

Once configured, you can access the ESP either through your own Wi-Fi network or directly via its Access Point (`192.168.4.1` or `config.local`), providing flexibility for a variety of IoT applications.

## Note

This project is intended as a **boilerplate** to help you quickly set up Wi-Fi management for your ESP and build upon it for your own IoT projects.

## How to start

1. **Download Arduino IDE 2 via either**

- [Microsoft Store](https://apps.microsoft.com/detail/xpddtbj80f8pc9)
- [Official Website](https://docs.arduino.cc/software/ide-v2/tutorials/getting-started/ide-v2-downloading-and-installing/)

2. **Configure the IDE**

- **Install the ESP Board**
  - ESP8266
    - Arduino IDE, go to `File` > `Preferences` and add this URL to the `Additional Boards Manager URLs`: `https://arduino.esp8266.com/stable/package_esp8266com_index.json`
    - Now go to `Tools` > `Board` > `Boards Manager`, search esp8266, and install ESP8266 Boards
  - ESP32
    - Arduino IDE, go to `Tools` > `Board` > `Boards Manager`, search for ESP32 and install esp32 by Espressif Systems version 3.X

- **Installing Libraries**
- Arduino IDE, go to `Sketch` > `Include Library` > `Manage Libraries...` > Search for the libraries and install the following.
  - Install the [ArduinoJson](https://github.com/bblanchon/ArduinoJson) library via `Sketch` > `Include Library` > `Manage Libraries...` and search for `ArduinoJson` (by Benoît Blanchon). Install the version 7.
  <!-- - **ESP8266**
    - Install the [ESPAsyncTCP](https://github.com/ESP32Async/ESPAsyncTCP) library (by ESP32Async). -->
  - **ESP32**
    - Install the [Async TCP](https://github.com/ESP32Async/AsyncTCP) library (by ESP32Async).
    - Install the [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) library via `Sketch` > `Include Library` > `Manage Libraries...` and search for `ESPAsyncWebServer` (by ESP32Async).

- Select your board at `Tools` > `Board`
- Plug in your board and select its COM port at `Tools` > `Port`
- Optional: To reset/override previous settings, select `Tools` > `Erase Flash` > `All Flash Contents` or `Tools` > `Erase All Flash Before Sketch Upload` > `Enabled`
- You will need [arduino-littlefs-upload filesystem uploader tool](https://github.com/earlephilhower/arduino-littlefs-upload/) to load sketch data into the ESP. [Tutorial](https://randomnerdtutorials.com/arduino-ide-2-install-esp32-littlefs/) (follow the instructions for Arduino IDE 2)
- Run the `webMinifier` script to minify the HTML, CSS files. This will create a `data` folder with the minified files inside the esp directory.
- Upload the `data` folder to the ESP using the filesystem uploader tool. as follows:
  - Open the `*.ino` file in your Arduino IDE.
  - Open the command palette by selecting `Ctrl` + `Shift` + `P`, then type `LittleFS Upload` and select `LittleFS to Pico/ESP8266/ESP32`.
  - Note: Make sure to close the serial monitor before uploading the files. If you don't, you will get an error message.

## Default settings

- **IP Address:** `192.168.4.1`
- **SSID:** `ESP-Access-Point`
- **Password:** `12345678`
- **Channel:** `6`
- **Hidden:** `false`

## File Structure

```
📁 web-interface        # Contains the source HTML, CSS, and JS files for the web interface
📁 esp                  # Contains the ESP sketch and the web interface files
│   📁 data
│       📁 www          # Contains the minified HTML, CSS, and the JS files for the web interface
📁 utils                # Contains utility scripts
    📄 webMinifier.py   # Script to minify HTML, CSS, files

```
