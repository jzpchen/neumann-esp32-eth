# Agent Handover & Build Notes (Neumann Volume Control)

This file contains the exact compiler environment, core configurations, and library versions that resulted in a successful compilation and flash on the developer's Mac. 

---

## 1. Hardware Details (Target Board)
* **Board Type**: Waveshare ESP32-S3-ETH
* **Microcontroller**: ESP32-S3 (QFN56) with 8MB Embedded PSRAM and 16MB Flash.
* **Onboard Ethernet Controller**: W5500 connected via SPI.
  * **SCK**: GPIO 13
  * **MISO**: GPIO 12
  * **MOSI**: GPIO 11
  * **CS**: GPIO 14
  * **INT (IRQ)**: GPIO 10
  * **RST**: GPIO 9
  * **PHY Address**: `1` (or `0`)

---

## 2. Compilation Environment & FQBN
We compiled and uploaded the firmware successfully using `arduino-cli`.
* **FQBN**: `esp32:esp32:esp32s3` (using generic ESP32S3 Dev Module profile).
* **ESP32 Arduino Core Version**: **`3.3.7`** (Must be v3.0 or higher!).
  * *Important*: Core v3.x is **mandatory** because the `ETH.h` library has been rewritten to integrate SPI Ethernet controllers (like the W5500) directly into the ESP32's native `lwIP` stack. Older cores (v2.x) do not support the `ETH_PHY_W5500` constant in `ETH.begin(...)` or require external libraries that conflict with the native network stack.
* **Required Libraries**:
  * **ArduinoJson** (Version **`7.4.3`** installed via `arduino-cli lib install "ArduinoJson"`).
  * Built-in Core libraries used: `WiFi`, `Network`, `Ethernet` (specifically `ETH.h`), `SPI`, `ESPmDNS`, `WebServer`, `WiFiUdp`.

### Compilation Commands
```bash
# Compile the sketch
arduino-cli compile --fqbn esp32:esp32:esp32s3 .

# Upload to board (replace <port> with your serial port, e.g., /dev/cu.usbmodem*)
arduino-cli upload -p <port> --fqbn esp32:esp32:esp32s3 .
```

---

## 3. Critical Code Architecture Details

### Arduino Preprocessor / Raw String Literal Bug
* **The Issue**: The standard Arduino preprocessor scans `.ino` files to auto-generate C++ prototypes. If you embed HTML/JavaScript inside a C++ raw string literal (`R"rawliteral(...)rawliteral"`) in a `.ino` file, and that JS contains any parenthesis next to double-quotes (such as inline click handlers: `onclick="foo(30)"`), the preprocessor interprets `)"` as the premature closing delimiter of the C++ string. It then tries to compile the remaining JS as C++, failing with errors like `error: 'asyncfunction' does not name a type` or `error: 'function' does not name a type`.
* **The Solution**: 
  1. All HTML/CSS/JavaScript content is declared externally in `html.h` and defined inside a separate `html.cpp` file. The Arduino preprocessor does not scan `.cpp`/`.h` files, which completely resolves the bug.
  2. All inline JS click handlers containing parenthesis have been replaced by standard `data-preset` HTML attributes, and event listeners are attached dynamically in the JS script block at the bottom of the Web UI page.

### Dual-Interface Network Configuration
* **Wi-Fi**: Connects to the home local network. Read credentials from `credentials.h` (configured for SSID `rose`). Runs the WebServer on port `5000`.
* **Ethernet**: Configured with a static IPv4 link-local IP `169.254.1.1` (no gateway) to communicate directly with the Neumann monitors connected to the same switch.
* **mDNS Speaker Discovery**: A background FreeRTOS task runs native ESP-IDF mDNS PTR queries (`mdns_query_ptr("_ssc", "_tcp", ...)`) to search for Sennheiser/Neumann devices. Discovered IP addresses and hostnames are verified and cached behind a mutex.
* **SSC TCP Client**: Queries (`{"audio":{"out":{"level":null}}}`) and commands (`{"audio":{"out":{"level":60.0}}}`) are transmitted directly to the speakers' IP addresses on TCP port `45` using `NetworkClient`. Responses are received, validated, and parsed using `ArduinoJson`.
* **Optimistic Direct Polling**: If mDNS returns 0 devices, the controller uses a fast direct TCP handshake poll on port 45 to the static link-local IPv6 addresses of known speakers to minimize power-on detection latency.
