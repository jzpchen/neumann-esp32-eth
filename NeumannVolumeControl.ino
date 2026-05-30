#include <WiFi.h>
#include <ETH.h>
#include <SPI.h>
#include <ESPmDNS.h>
#include "mdns.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include "credentials.h"
#include "html.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <ESP32Encoder.h>

#define SCK_PIN 13
#define MISO_PIN 12
#define MOSI_PIN 11
#define CS_PIN 14
#define IRQ_PIN 10
#define RST_PIN 9

#define MAX_LEVEL 80.0
#define MIN_LEVEL 0.0

// Speaker cache structure
struct SpeakerInfo {
  IPAddress ip;
  uint16_t port;
  char hostname[64];
};

// Speaker lists and mutex
SpeakerInfo discoveredSpeakers[10];
int speakerCount = 0;
SpeakerInfo knownSpeakers[10];
int knownSpeakerCount = 0;
SemaphoreHandle_t speakerMutex = NULL;
float currentVolume = -1000.0; // -1000.0 means uninitialized

WebServer server(5000);

SPIClass ethSPI(HSPI);

// OLED display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// OLED and Encoder state
#define ENCODER_TR_A  1
#define ENCODER_TR_B  2
#define ENCODER_PUSH  3

ESP32Encoder encoder;
volatile bool pushPressed = false;
volatile unsigned long lastPushTime = 0;
volatile bool displayPowerState = true;
volatile bool displayNeedsUpdate = true;

Preferences prefs;
bool swapLR = false;

// Function declarations
void handleGetSpeakers();
void handleGetLevel();
void handleSetLevel();
void handleRoot();
void handleGetStatus();
void mdnsDiscoveryTask(void *pvParameters);
void onNetworkEvent(arduino_event_id_t event);
float getSpeakerLevel(const SpeakerInfo &speaker);
bool setSpeakerLevel(const SpeakerInfo &speaker, float level);
void IRAM_ATTR pushISR();
void updateOLED();
void handlePostSettings();
void turnOffRGB();

void IRAM_ATTR pushISR() {
  unsigned long now = millis();
  if (now - lastPushTime > 250) { // 250ms debounce
    pushPressed = true;
    lastPushTime = now;
  }
}

void updateOLED() {
  if (!displayPowerState) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  xSemaphoreTake(speakerMutex, portMAX_DELAY);
  float vol = currentVolume;
  
  // Speaker identification logic based on hostname keyword or order of appearance
  int leftIdx = -1;
  int rightIdx = -1;
  for (int i = 0; i < knownSpeakerCount; i++) {
    String host = String(knownSpeakers[i].hostname);
    host.toLowerCase();
    if (host.indexOf("left") >= 0 || host.indexOf("-l") >= 0) {
      leftIdx = i;
    } else if (host.indexOf("right") >= 0 || host.indexOf("-r") >= 0) {
      rightIdx = i;
    }
  }
  if (leftIdx == -1 && rightIdx == -1) {
    if (knownSpeakerCount >= 1) leftIdx = 0;
    if (knownSpeakerCount >= 2) rightIdx = 1;
  } else if (leftIdx != -1 && rightIdx == -1) {
    for (int i = 0; i < knownSpeakerCount; i++) {
      if (i != leftIdx) { rightIdx = i; break; }
    }
  } else if (leftIdx == -1 && rightIdx != -1) {
    for (int i = 0; i < knownSpeakerCount; i++) {
      if (i != rightIdx) { leftIdx = i; break; }
    }
  }

  bool leftOnline = false;
  bool rightOnline = false;
  if (leftIdx != -1) {
    for (int i = 0; i < speakerCount; i++) {
      if (discoveredSpeakers[i].ip == knownSpeakers[leftIdx].ip) {
        leftOnline = true;
        break;
      }
    }
  }
  if (rightIdx != -1) {
    for (int i = 0; i < speakerCount; i++) {
      if (discoveredSpeakers[i].ip == knownSpeakers[rightIdx].ip) {
        rightOnline = true;
        break;
      }
    }
  }
  xSemaphoreGive(speakerMutex);

  // Apply L/R swap setting if active
  if (swapLR) {
    bool temp = leftOnline;
    leftOnline = rightOnline;
    rightOnline = temp;
  }

  // 1. Draw Large Volume Level
  if (vol < -999.0) {
    display.setTextSize(3);
    display.setCursor(24, 8);
    display.print("--.-");
  } else {
    display.setTextSize(3);
    display.setCursor(24, 8);
    display.printf("%.1f", vol);
  }

  // Draw "dB" unit
  display.setTextSize(1);
  display.setCursor(100, 20);
  display.print("dB");

  // 2. Draw Bottom Status Bar (Speaker Icons)
  // Left Speaker Icon
  display.setTextSize(1);
  display.setCursor(12, 48);
  display.print("L");
  
  display.drawRoundRect(24, 40, 14, 22, 2, SSD1306_WHITE);
  display.drawCircle(30, 45, 2, SSD1306_WHITE); // Tweeter
  display.drawCircle(30, 54, 4, SSD1306_WHITE); // Woofer
  if (!leftOnline) {
    display.drawLine(24, 40, 37, 61, SSD1306_WHITE);
  } else {
    // Sound waves
    display.drawPixel(21, 49, SSD1306_WHITE);
    display.drawPixel(20, 50, SSD1306_WHITE);
    display.drawPixel(20, 51, SSD1306_WHITE);
    display.drawPixel(21, 52, SSD1306_WHITE);
  }

  // Right Speaker Icon
  display.drawRoundRect(90, 40, 14, 22, 2, SSD1306_WHITE);
  display.drawCircle(96, 45, 2, SSD1306_WHITE); // Tweeter
  display.drawCircle(96, 54, 4, SSD1306_WHITE); // Woofer
  if (!rightOnline) {
    display.drawLine(90, 40, 103, 61, SSD1306_WHITE);
  } else {
    // Sound waves
    display.drawPixel(106, 49, SSD1306_WHITE);
    display.drawPixel(107, 50, SSD1306_WHITE);
    display.drawPixel(107, 51, SSD1306_WHITE);
    display.drawPixel(106, 52, SSD1306_WHITE);
  }

  display.setCursor(110, 48);
  display.print("R");

  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- Neumann Volume Control Booting ---");

  // Turn off the onboard RGB LED immediately
  turnOffRGB();

  // Initialize NVS storage and load swap setting
  prefs.begin("neumann-vol", false);
  swapLR = prefs.getBool("swapLR", false);

  // Create speaker cache mutex
  speakerMutex = xSemaphoreCreateMutex();

  // Register network event callbacks
  Network.onEvent(onNetworkEvent);

  // Initialize Wi-Fi
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.enableIPv6(true);

  // Initialize W5500 SPI Ethernet
  Serial.println("Initializing SPI for W5500...");
  ethSPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);

  Serial.println("Starting Ethernet...");
  if (!ETH.begin(ETH_PHY_W5500, 1, CS_PIN, IRQ_PIN, RST_PIN, ethSPI)) {
    Serial.println("W5500 Ethernet begin failed!");
  } else {
    Serial.println("W5500 Ethernet interface initialized.");
    IPAddress local_ip(169, 254, 1, 1);
    IPAddress subnet(255, 255, 0, 0);
    IPAddress gateway(0, 0, 0, 0);
    ETH.config(local_ip, gateway, subnet);
    ETH.enableIPv6(true);
  }



  // Register WebServer routes
  server.on("/api/speakers", HTTP_GET, handleGetSpeakers);
  server.on("/api/level", HTTP_GET, handleGetLevel);
  server.on("/api/level", HTTP_POST, handleSetLevel);
  server.on("/api/status", HTTP_GET, handleGetStatus);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/", HTTP_GET, handleRoot);
  
  server.begin();
  Serial.println("REST API WebServer started on port 5000");

  // Initialize I2C and OLED
  Wire.begin(17, 18); // Pin 34 (SDA = 17), Pin 31 (SCL = 18)
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
  } else {
    display.clearDisplay();
    display.display();
    Serial.println("OLED Display initialized.");
  }

  // Initialize ESP32Encoder
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  encoder.attachFullQuad(ENCODER_TR_B, ENCODER_TR_A);
  encoder.setFilter(1023); // Hardware debounce filter
  encoder.setCount(0);

  // Initialize Push Button pin and interrupt
  pinMode(ENCODER_PUSH, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PUSH), pushISR, FALLING);

  // Start background mDNS Discovery Task
  xTaskCreatePinnedToCore(
    mdnsDiscoveryTask,
    "mDNSTask",
    4096,
    NULL,
    1,
    NULL,
    1
  );
  Serial.println("mDNS Discovery Task started.");
  updateOLED(); // Initial display refresh
}

void loop() {
  server.handleClient();

  // Handle display toggle button press
  if (pushPressed) {
    pushPressed = false;
    displayPowerState = !displayPowerState;
    if (displayPowerState) {
      display.ssd1306_command(SSD1306_DISPLAYON);
      displayNeedsUpdate = true;
      Serial.println("OLED Display: Turned ON");
    } else {
      display.ssd1306_command(SSD1306_DISPLAYOFF);
      Serial.println("OLED Display: Turned OFF (Sleep)");
    }
  }

  // Handle encoder rotation
  static int64_t lastEncoderCount = 0;
  int64_t currentEncoderCount = encoder.getCount();
  int64_t encoderDelta = currentEncoderCount - lastEncoderCount;
  if (encoderDelta != 0) {
    lastEncoderCount = currentEncoderCount;
    static int64_t encoderAccumulator = 0;
    encoderAccumulator += encoderDelta;
    
    // 4 counts per click for standard EC11 in FullQuad mode
    int64_t clicks = encoderAccumulator / 4;
    if (clicks != 0) {
      encoderAccumulator -= clicks * 4;

      xSemaphoreTake(speakerMutex, portMAX_DELAY);
      float val = currentVolume;
      int count = speakerCount;
      SpeakerInfo speakers[10];
      for (int i = 0; i < count; i++) {
        speakers[i] = discoveredSpeakers[i];
      }
      xSemaphoreGive(speakerMutex);

      if (val >= -999.0) { // Only adjust if volume is initialized
        float newVal = val + (clicks * 0.5); // Adjust by 0.5 dB steps
        if (newVal > MAX_LEVEL) newVal = MAX_LEVEL;
        if (newVal < MIN_LEVEL) newVal = MIN_LEVEL;

        if (newVal != val) {
          Serial.printf("Encoder adjusted volume: %.1f dB -> %.1f dB (delta=%lld, clicks=%lld)\n", val, newVal, encoderDelta, clicks);
          
          xSemaphoreTake(speakerMutex, portMAX_DELAY);
          currentVolume = newVal;
          xSemaphoreGive(speakerMutex);

          // Update all active speakers
          for (int i = 0; i < count; i++) {
            setSpeakerLevel(speakers[i], newVal);
          }
          
          displayNeedsUpdate = true;
        }
      }
    }
  }

  // Draw display if needed
  if (displayNeedsUpdate) {
    displayNeedsUpdate = false;
    updateOLED();
  }

  delay(2);
}

void onNetworkEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("Wi-Fi Interface: Connected.");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("Wi-Fi Interface Got IPv4: ");
      Serial.println(WiFi.localIP());
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
      Serial.print("Wi-Fi Interface Got IPv6: ");
      Serial.println(WiFi.linkLocalIPv6());
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("Ethernet Link: Connected.");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("Ethernet Interface Got IPv4: ");
      Serial.println(ETH.localIP());
      break;
    case ARDUINO_EVENT_ETH_GOT_IP6:
      Serial.print("Ethernet Interface Got IPv6 Link-Local: ");
      Serial.println(ETH.linkLocalIPv6());
      // Initialize MDNS responder if not already initialized
      if (!MDNS.begin("neumann-vol")) {
        Serial.println("Error setting up mDNS responder!");
      } else {
        Serial.println("mDNS responder started (hostname: neumann-vol.local)");
      }
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("Ethernet Link: Disconnected.");
      break;
    default:
      break;
  }
}

// Helper to query mDNS AAAA record (IPv6 address) for a hostname
IPAddress queryHostV6(const char *host, uint32_t timeout = 2000) {
  esp_ip6_addr_t addr;
  memset(&addr, 0, sizeof(addr));
  esp_err_t err = mdns_query_aaaa(host, timeout, &addr);
  if (err) {
    Serial.printf("mdns_query_aaaa failed for host %s (error: 0x%x)\n", host, err);
    return IPAddress(IPv6); // Return empty IPv6 address
  }
  return IPAddress(IPv6, (const uint8_t *)addr.addr, ETH.impl_index());
}

// Background FreeRTOS task for scanning speakers
void mdnsDiscoveryTask(void *pvParameters) {
  while (true) {
    uint32_t startScan = millis();
    if (ETH.linkUp()) {
      Serial.printf("[%lu ms] Scanning network for Neumann monitors (_ssc._tcp) using native mDNS PTR...\n", millis());
      
      mdns_result_t *results = NULL;
      // Query PTR with a timeout of 800ms (more than enough for a local network link)
      esp_err_t err = mdns_query_ptr("_ssc", "_tcp", 800, 10, &results);
      
      // Temporary cache to build the new list of online speakers
      SpeakerInfo activeSpeakers[10];
      int activeCount = 0;
      
      if (err == ESP_OK && results != NULL) {
        // Count results
        int n = 0;
        mdns_result_t *temp = results;
        while (temp) {
          n++;
          temp = temp->next;
        }
        Serial.printf("[%lu ms] Native mDNS returned %d service record(s) (took %lu ms)\n", millis(), n, millis() - startScan);
        
        mdns_result_t *r = results;
        for (int i = 0; r != NULL && activeCount < 10; i++, r = r->next) {
          uint32_t startRes = millis();
          IPAddress ip;
          
          // Try to extract IP from the results' address linked list directly
          mdns_ip_addr_t *curr_addr = r->addr;
          while (curr_addr != NULL) {
            if (curr_addr->addr.type == MDNS_IP_PROTOCOL_V6) {
              ip = IPAddress(IPv6, (const uint8_t *)curr_addr->addr.u_addr.ip6.addr, ETH.impl_index());
              break; // Prioritize IPv6 link-local
            } else if (curr_addr->addr.type == MDNS_IP_PROTOCOL_V4 && ip.type() != IPv6) {
              ip = IPAddress(curr_addr->addr.u_addr.ip4.addr);
            }
            curr_addr = curr_addr->next;
          }
          
          // If IP was not in the record, resolve hostname via queries
          if ((ip.type() == IPv6 && ip == IN6ADDR_ANY) || (ip.type() == IPv4 && ip == IPAddress(0,0,0,0))) {
            if (r->hostname != NULL && strlen(r->hostname) > 0) {
              Serial.printf("[%lu ms] IP not in PTR records. Resolving hostname: %s.local...\n", millis(), r->hostname);
              IPAddress ip6 = queryHostV6(r->hostname, 500);
              if (ip6.type() == IPv6 && ip6 != IN6ADDR_ANY) {
                ip = ip6;
              } else {
                Serial.printf("[%lu ms] IPv6 query failed (took %lu ms). Trying IPv4 fallback...\n", millis(), millis() - startRes);
                IPAddress ip4 = MDNS.queryHost(r->hostname, 250);
                if (ip4 != INADDR_NONE && ip4 != IPAddress(0,0,0,0)) {
                  ip = ip4;
                }
              }
            }
          }
          
          uint16_t port = r->port;
          const char *hostName = r->hostname ? r->hostname : "";
          
          Serial.printf("[%lu ms] Resolution for %s took %lu ms. Resolved IP: %s\n", 
            millis(), hostName, millis() - startRes, ip.toString().c_str());
          
          if ((ip.type() == IPv6 && ip == IN6ADDR_ANY) || (ip.type() == IPv4 && ip == IPAddress(0,0,0,0))) {
            Serial.printf("  [%lu ms] Skipping service record %d: unresolved IP.\n", millis(), i + 1);
            continue;
          }
          
          // Verify TCP connectivity to port 45
          uint32_t startTCP = millis();
          NetworkClient client;
          Serial.printf("  [%lu ms] Verifying connectivity to speaker %s (%s:%d)...\n", millis(), hostName, ip.toString().c_str(), port);
          if (client.connect(ip, port, 400)) {
            client.stop();
            Serial.printf("  [%lu ms] Speaker %s ONLINE (TCP connect took %lu ms).\n", millis(), hostName, millis() - startTCP);
            
            activeSpeakers[activeCount].ip = ip;
            activeSpeakers[activeCount].port = port;
            strncpy(activeSpeakers[activeCount].hostname, hostName, sizeof(activeSpeakers[activeCount].hostname) - 1);
            activeSpeakers[activeCount].hostname[sizeof(activeSpeakers[activeCount].hostname) - 1] = '\0';
            activeCount++;
          } else {
            Serial.printf("  [%lu ms] Speaker %s OFFLINE (TCP connection failed after %lu ms).\n", millis(), hostName, millis() - startTCP);
          }
        }
        
        mdns_query_results_free(results);
      } else {
        Serial.printf("[%lu ms] Native mDNS query found 0 speakers or failed (took %lu ms).\n", millis(), millis() - startScan);
      }
      
      // Attempt direct TCP reconnection to any missing known speakers in our history
      if (knownSpeakerCount > 0) {
        for (int i = 0; i < knownSpeakerCount && activeCount < 10; i++) {
          // Check if knownSpeakers[i] is already in activeSpeakers (by IP)
          bool alreadyFound = false;
          for (int j = 0; j < activeCount; j++) {
            if (activeSpeakers[j].ip == knownSpeakers[i].ip) {
              alreadyFound = true;
              break;
            }
          }
          
          if (!alreadyFound) {
            uint32_t startTCP = millis();
            NetworkClient client;
            if (client.connect(knownSpeakers[i].ip, knownSpeakers[i].port, 300)) { // Fast 300ms timeout
              client.stop();
              Serial.printf("[%lu ms] Direct reconnect success for missing speaker: %s (%s, took %lu ms)\n", 
                millis(), knownSpeakers[i].hostname, knownSpeakers[i].ip.toString().c_str(), millis() - startTCP);
              activeSpeakers[activeCount] = knownSpeakers[i];
              activeCount++;
            }
          }
        }
      }
      
      // Update cache and perform synchronization/initialization logic
      xSemaphoreTake(speakerMutex, portMAX_DELAY);
      
      // Check for newly discovered/connected speakers to sync volume
      for (int i = 0; i < activeCount; i++) {
        bool wasCached = false;
        for (int j = 0; j < speakerCount; j++) {
          if (activeSpeakers[i].ip == discoveredSpeakers[j].ip) {
            wasCached = true;
            break;
          }
        }
        
        if (!wasCached) {
          Serial.printf("[%lu ms] New speaker connected: %s (%s)\n", millis(), activeSpeakers[i].hostname, activeSpeakers[i].ip.toString().c_str());
          if (currentVolume > -999.0) {
            // Give the speaker's internal stack 500ms to stabilize after physical connection is verified
            delay(500);
            Serial.printf("[%lu ms] Syncing new speaker %s to current volume level %.1f dB...\n", millis(), activeSpeakers[i].hostname, currentVolume);
            if (setSpeakerLevel(activeSpeakers[i], currentVolume)) {
              Serial.println("  Volume sync successful.");
            } else {
              Serial.println("  Volume sync failed. Retrying in 1 second...");
              delay(1000);
              if (setSpeakerLevel(activeSpeakers[i], currentVolume)) {
                Serial.println("  Volume sync successful on retry.");
              } else {
                Serial.println("  Volume sync retry failed.");
              }
            }
          }
        }
        
        // Add to persistent knownSpeakers list if not already there (by IP)
        bool isKnown = false;
        for (int j = 0; j < knownSpeakerCount; j++) {
          if (knownSpeakers[j].ip == activeSpeakers[i].ip) {
            isKnown = true;
            break;
          }
        }
        if (!isKnown && knownSpeakerCount < 10) {
          knownSpeakers[knownSpeakerCount] = activeSpeakers[i];
          knownSpeakerCount++;
          Serial.printf("[%lu ms] Added %s (%s) to known speakers history.\n", millis(), activeSpeakers[i].hostname, activeSpeakers[i].ip.toString().c_str());
        }
      }
      
      // Save active speakers to cache
      speakerCount = activeCount;
      for (int i = 0; i < activeCount; i++) {
        discoveredSpeakers[i] = activeSpeakers[i];
      }
      displayNeedsUpdate = true; // Trigger OLED update when active speakers change
      
      // If volume is uninitialized and we have speakers, fetch level to initialize
      if (currentVolume < -999.0 && speakerCount > 0) {
        Serial.printf("[%lu ms] Initializing system volume level from first discovered speaker...\n", millis());
        float val = getSpeakerLevel(discoveredSpeakers[0]);
        if (val > -999.0) {
          currentVolume = val;
          Serial.printf("[%lu ms] System volume level initialized to: %.1f dB\n", millis(), currentVolume);
          displayNeedsUpdate = true; // Trigger OLED update on volume initialization
        } else {
          Serial.printf("[%lu ms] Failed to read volume from speaker during initialization.\n", millis());
        }
      }
      
      xSemaphoreGive(speakerMutex);
      
    } else {
      Serial.printf("[%lu ms] Ethernet link down, skipping mDNS scan.\n", millis());
      xSemaphoreTake(speakerMutex, portMAX_DELAY);
      if (speakerCount != 0) {
        speakerCount = 0;
        displayNeedsUpdate = true; // Trigger OLED update when link goes down
      }
      xSemaphoreGive(speakerMutex);
    }
    
    // Wait 3 seconds before scanning again (faster scan to detect state changes quickly)
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

void handleGetStatus() {
  JsonDocument doc;
  
  // Wi-Fi Status
  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["connected"] = (WiFi.status() == WL_CONNECTED);
  wifi["ip"] = WiFi.localIP().toString();
  wifi["ip6"] = WiFi.linkLocalIPv6().toString();
  
  // Ethernet Status
  JsonObject eth = doc.createNestedObject("eth");
  eth["connected"] = ETH.connected();
  eth["link_up"] = ETH.linkUp();
  eth["ip"] = ETH.localIP().toString();
  eth["ip6"] = ETH.linkLocalIPv6().toString();
  eth["speed"] = ETH.linkSpeed();
  eth["full_duplex"] = ETH.fullDuplex();
  
  // MDNS Status
  doc["mdns_hostname"] = "neumann-vol.local";
  
  // Discovered Speakers Count
  doc["speaker_count"] = speakerCount;
  doc["current_volume"] = currentVolume;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleGetSpeakers() {
  JsonDocument doc;
  JsonArray arr = doc.createNestedArray("speakers");

  xSemaphoreTake(speakerMutex, portMAX_DELAY);
  for (int i = 0; i < speakerCount; i++) {
    JsonObject obj = arr.createNestedObject();
    if (strlen(discoveredSpeakers[i].hostname) > 0) {
      obj["hostname"] = discoveredSpeakers[i].hostname;
    } else {
      obj["hostname"] = nullptr;
    }
    obj["ip"] = discoveredSpeakers[i].ip.toString();
    obj["port"] = discoveredSpeakers[i].port;
  }
  xSemaphoreGive(speakerMutex);

  doc["swap_lr"] = swapLR;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleGetLevel() {
  xSemaphoreTake(speakerMutex, portMAX_DELAY);
  int count = speakerCount;
  float level = currentVolume;
  xSemaphoreGive(speakerMutex);

  if (count == 0 || level < -999.0) {
    JsonDocument doc;
    doc["error"] = "speakers not connected";
    if (level >= -999.0) {
      doc["level"] = level;
    }
    String response;
    serializeJson(doc, response);
    server.send(503, "application/json", response);
    return;
  }

  JsonDocument doc;
  doc["level"] = level;
  doc["unit"] = "dB";

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleSetLevel() {
  xSemaphoreTake(speakerMutex, portMAX_DELAY);
  int count = speakerCount;
  SpeakerInfo speakers[10];
  for (int i = 0; i < count; i++) {
    speakers[i] = discoveredSpeakers[i];
  }
  xSemaphoreGive(speakerMutex);

  if (count == 0) {
    server.send(503, "application/json", "{\"error\":\"speakers not connected\"}");
    return;
  }

  String body = server.arg("plain");
  JsonDocument reqDoc;
  DeserializationError error = deserializeJson(reqDoc, body);
  
  if (error || !reqDoc.containsKey("level")) {
    server.send(400, "application/json", "{\"error\":\"body must be JSON with 'level' key\"}");
    return;
  }

  float level = reqDoc["level"].as<float>();
  
  // Clamp level
  bool clamped = false;
  if (level > MAX_LEVEL) {
    level = MAX_LEVEL;
    clamped = true;
  } else if (level < MIN_LEVEL) {
    level = MIN_LEVEL;
    clamped = true;
  }

  // Update current volume
  xSemaphoreTake(speakerMutex, portMAX_DELAY);
  currentVolume = level;
  xSemaphoreGive(speakerMutex);
  displayNeedsUpdate = true; // Trigger OLED update when volume is set via REST API

  // Send level to all speakers
  bool success = true;
  for (int i = 0; i < count; i++) {
    if (!setSpeakerLevel(speakers[i], level)) {
      success = false;
    }
  }

  if (!success) {
    server.send(503, "application/json", "{\"error\":\"failed to set level\"}");
    return;
  }

  JsonDocument respDoc;
  respDoc["level"] = level;
  respDoc["unit"] = "dB";
  respDoc["clamped"] = clamped;
  
  JsonObject range = respDoc.createNestedObject("range");
  range["min"] = MIN_LEVEL;
  range["max"] = MAX_LEVEL;

  String response;
  serializeJson(respDoc, response);
  server.send(200, "application/json", response);
}

// Low-level TCP communication function to read level
float getSpeakerLevel(const SpeakerInfo &speaker) {
  NetworkClient client;
  
  Serial.printf("Connecting to speaker via TCP %s:%d...\n", speaker.ip.toString().c_str(), speaker.port);
  
  if (!client.connect(speaker.ip, speaker.port, 1000)) { // 1000ms connection timeout
    Serial.println("TCP connection to speaker failed.");
    return -1000.0;
  }
  
  // Construct request JSON without spaces
  String request = "{\"audio\":{\"out\":{\"level\":null}}}";
  
  client.println(request);
  
  // Await response
  unsigned long start = millis();
  String response = "";
  while (client.connected() && millis() - start < 1000) { // 1000ms read timeout
    while (client.available()) {
      char c = client.read();
      response += c;
    }
    // If we have a non-empty response, check if we received the closing brace
    if (response.length() > 0 && response.indexOf('}') != -1) {
      delay(10); // small delay to ensure buffer is completely flushed
      while (client.available()) {
        response += (char)client.read();
      }
      break;
    }
    delay(10);
  }
  
  client.stop();
  
  if (response.length() == 0) {
    Serial.println("Timeout waiting for level query TCP response from speaker.");
    return -1000.0;
  }
  
  Serial.printf("Speaker TCP response: %s\n", response.c_str());
  
  // Parse JSON response
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    Serial.printf("Failed to parse speaker JSON response: %s\n", err.c_str());
    return -1000.0;
  }
  
  if (doc.containsKey("audio") && 
      doc["audio"].containsKey("out") && 
      doc["audio"]["out"].containsKey("level")) {
    return doc["audio"]["out"]["level"].as<float>();
  }
  
  Serial.println("JSON response does not contain level key.");
  return -1000.0;
}

// Low-level TCP communication function to write level
bool setSpeakerLevel(const SpeakerInfo &speaker, float level) {
  NetworkClient client;
  
  Serial.printf("Connecting to speaker via TCP %s:%d to set volume...\n", speaker.ip.toString().c_str(), speaker.port);
  
  if (!client.connect(speaker.ip, speaker.port, 1000)) { // 1000ms connection timeout
    Serial.println("TCP connection to speaker failed.");
    return false;
  }
  
  // Construct command JSON
  char request[128];
  snprintf(request, sizeof(request), "{\"audio\":{\"out\":{\"level\":%.1f}}}", level);
  
  client.println(request);
  
  // Await response (ack)
  unsigned long start = millis();
  String response = "";
  while (client.connected() && millis() - start < 500) { // 500ms read timeout
    while (client.available()) {
      response += (char)client.read();
    }
    if (response.length() > 0 && response.indexOf('}') != -1) {
      break;
    }
    delay(10);
  }
  
  client.stop();
  
  if (response.length() > 0) {
    Serial.printf("Speaker set TCP response: %s\n", response.c_str());
  } else {
    Serial.println("No response received for set level command, but assuming success.");
  }
  
  return true;
}

void handleRoot() {
  server.send(200, "text/html", HTML_CONTENT);
}

void turnOffRGB() {
#ifdef RGB_BUILTIN
  neopixelWrite(RGB_BUILTIN, 0, 0, 0);
#endif
  neopixelWrite(21, 0, 0, 0);
  neopixelWrite(38, 0, 0, 0);
  neopixelWrite(48, 0, 0, 0);
  Serial.println("Onboard RGB LED set to OFF via neopixelWrite.");
}

void handlePostSettings() {
  String body = server.arg("plain");
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error || !doc.containsKey("swap_lr")) {
    server.send(400, "application/json", "{\"error\":\"body must contain 'swap_lr'\"}");
    return;
  }
  
  swapLR = doc["swap_lr"].as<bool>();
  prefs.putBool("swapLR", swapLR);
  
  displayNeedsUpdate = true;
  
  JsonDocument resp;
  resp["success"] = true;
  resp["swap_lr"] = swapLR;
  String response;
  serializeJson(resp, response);
  server.send(200, "application/json", response);
  Serial.printf("Settings updated: swap_lr = %s\n", swapLR ? "true" : "false");
}
