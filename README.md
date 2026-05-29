# Neumann Volume Control for ESP32-S3-ETH

A lightweight, high-performance network volume controller and REST API server running on the Waveshare ESP32-S3-ETH board. It provides a beautiful glassmorphic Web UI and REST API to discover and control the volume of Neumann digital DSP studio monitors (e.g., KH 80, KH 120 II, KH 150, KH 750) over a wired link-local Ethernet connection using the Sennheiser Sound Control (SSC) TCP protocol on port 45.

---

## Hardware Configuration
* **Target Board**: Waveshare ESP32-S3-ETH (ESP32-S3 with 8MB PSRAM and 16MB Flash).
* **Onboard Ethernet**: W5500 connected via SPI.
* **Pin Mapping (SPI W5500)**:
  * **SCK**: GPIO 13
  * **MISO**: GPIO 12
  * **MOSI**: GPIO 11
  * **CS**: GPIO 14
  * **INT (IRQ)**: GPIO 10
  * **RST**: GPIO 9
  * **PHY Address**: `1`
* **OLED Display (I2C SSD1306/SSD1315)**:
  * **SDA**: GPIO 17 (Pin 34)
  * **SCL**: GPIO 18 (Pin 31)
* **Rotary Encoder (EC11)**:
  * **TR_A**: GPIO 1 (Pin 25)
  * **TR_B**: GPIO 2 (Pin 26)
  * **PUSH**: GPIO 3 (Pin 27)
  * *Note*: Direction is reversed in software (TR_B and TR_A swapped) to ensure clockwise rotation increases volume.

---

## Key Features

1. **Dual-Interface Networking**:
   * **Wi-Fi**: Connects to your local Wi-Fi network to host the REST API and Web UI (on port 5000) for control from laptops, phones, or tablets.
   * **Ethernet**: Configured with a static link-local IPv4 address (`169.254.1.1`) to communicate directly with Neumann monitors connected to the same switch/router via IPv6 link-local addresses.
2. **Native mDNS PTR Speaker Discovery**:
   * Utilizes native ESP-IDF mDNS functions directly (`mdns_query_ptr`) with a low **800ms** query timeout instead of Arduino's blocking 3-second wrappers.
   * Direct extraction of IP addresses from resolved PTR record headers, bypassing secondary hostname query timeouts completely.
3. **Optimistic Direct TCP Polling**:
   * Digital monitors can take up to 12 seconds to boot their network stacks. The controller maintains a history of known speakers and performs active TCP port 45 connection polling to discover booting monitors instantly, bypassing mDNS registration delays.
4. **Robust Volume Synchronization**:
   * When a speaker is powered on or reconnected, the background task detects it, waits **500ms** for the monitor's internal control stack to stabilize, and automatically syncs it to the current system volume level. A retry loop is implemented if the initial sync fails.
5. **Fluid Web Interface (Glassmorphism)**:
   * Responsive slider and preset buttons featuring a sleek dark-mode design.
   * **Visual Overlay States**: Smoothly transitions between `connecting` (indigo spinner during scan), `offline` (clean gray panel if monitors are off), and `error` (red indicator if the controller itself goes down).
6. **Interface Interaction Lockout & Request Queuing**:
   * **Interaction Lockout**: Blocks background status fetches from resetting the slider position during drags or for **1.5 seconds** after any adjustment.
   * **POST Serialization**: Limits outgoing HTTP POST commands to exactly one in-flight request at a time, queueing the latest target level and discarding intermediate stale values to prevent socket buffer congestion.

---

## API Documentation

### 1. `GET /`
Serves the responsive, single-page Glassmorphic Web UI.

### 2. `GET /api/status`
Returns the network connectivity status for Wi-Fi and Ethernet, active speaker count, and system volume.
* **Response `200 OK`**:
  ```json
  {
    "wifi": {
      "connected": true,
      "ip": "192.168.10.202",
      "ip6": "fe80::3e0f:2ff:fed7:778c"
    },
    "eth": {
      "connected": true,
      "link_up": true,
      "ip": "169.254.1.1",
      "ip6": "fe80::2a36:38ff:fe61:661c",
      "speed": 100,
      "full_duplex": true
    },
    "mdns_hostname": "neumann-vol.local",
    "speaker_count": 2,
    "current_volume": 60.0
  }
  ```

### 3. `GET /api/speakers`
Returns a list of all active discovered speakers.
* **Response `200 OK`**:
  ```json
  {
    "speakers": [
      {
        "hostname": "KH120-28363861661C",
        "ip": "fe80::2a36:38ff:fe61:661c",
        "port": 45
      }
    ]
  }
  ```

### 4. `GET /api/level`
Returns the current cached volume level in dB (served instantly from RAM to avoid blocking the WebServer).
* **Response `200 OK`**:
  ```json
  {
    "level": 60.0,
    "unit": "dB"
  }
  ```
* **Response `503 Service Unavailable`**: Returned if no speakers are online.

### 5. `POST /api/level`
Sets the volume level on all active speakers. Input levels exceeding boundaries are automatically clamped.
* **Request**:
  * Headers: `Content-Type: application/json`
  * Body: `{"level": 55.0}`
* **Response `200 OK`**:
  ```json
  {
    "level": 55.0,
    "unit": "dB",
    "clamped": false,
    "range": {
      "min": 0.0,
      "max": 80.0
    }
  }
  ```
* **Response `503 Service Unavailable`**: Returned if speakers are disconnected or the command fails to send.

---

## Developer Environment & Setup

### Requirements:
1. **ESP32 Arduino Core Version**: **`3.0.x` or higher** (Mandatory for core W5500 SPI Ethernet stack integration).
2. **Arduino Libraries**:
   * **ArduinoJson** (v7.4.3 or higher).

### Compilation and Upload Commands:
Create a file named `credentials.h` in the sketch directory:
```cpp
#define WIFI_SSID "YourWiFiSSID"
#define WIFI_PASSWORD "YourWiFiPassword"
```

Compile and flash using `arduino-cli`:
```bash
# Compile sketch
arduino-cli compile --fqbn esp32:esp32:esp32s3 .

# Flash sketch (replace cu.usbmodem* with your port)
arduino-cli upload -p /dev/cu.usbmodem21101 --fqbn esp32:esp32:esp32s3 .
```

---

## Technical & Architecture Notes

### Arduino Preprocessor / Raw String Literal Workaround
* **The Issue**: The standard Arduino preprocessor scans `.ino` files to auto-generate C++ prototypes. If you embed HTML/JavaScript inside a C++ raw string literal (`R"rawliteral(...)rawliteral"`) in a `.ino` file, and that JS contains parenthesis adjacent to double-quotes (such as inline click handlers: `onclick="foo(30)"`), the preprocessor incorrectly interprets `)"` as the premature closing delimiter of the C++ string. It then tries to compile the remaining JS as C++ code, failing with confusing syntax errors (e.g., `error: 'asyncfunction' does not name a type`).
* **The Solution**: 
  1. All HTML/CSS/JavaScript content is declared externally in `html.h` and defined inside a separate `html.cpp` file. The Arduino preprocessor does not scan `.cpp`/`.h` files, which completely avoids the bug.
  2. All inline JS click handlers containing parenthesis have been replaced by standard `data-preset` HTML attributes, and event listeners are attached dynamically in the JS script block at the bottom of the Web UI page.

