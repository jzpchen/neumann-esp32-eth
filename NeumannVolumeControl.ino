#include <WiFi.h>
#include <ETH.h>
#include <SPI.h>
#include <ESPmDNS.h>
#include "mdns.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include "credentials.h"
#include "html.h"

#define SCK_PIN 13
#define MISO_PIN 12
#define MOSI_PIN 11
#define CS_PIN 14
#define IRQ_PIN 10
#define RST_PIN 9

#define UDP_PORT 45
#define LOCAL_UDP_PORT 4545

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
SemaphoreHandle_t speakerMutex = NULL;
float currentVolume = -1000.0; // -1000.0 means uninitialized

WebServer server(5000);
WiFiUDP udp;

SPIClass ethSPI(HSPI);

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

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- Neumann Volume Control Booting ---");

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

  // Start UDP listener using IPv6 wildcard to ensure the socket supports IPv6
  udp.begin(IPAddress(IPv6), LOCAL_UDP_PORT);
  Serial.print("UDP listening on local port ");
  Serial.println(LOCAL_UDP_PORT);

  // Register WebServer routes
  server.on("/api/speakers", HTTP_GET, handleGetSpeakers);
  server.on("/api/level", HTTP_GET, handleGetLevel);
  server.on("/api/level", HTTP_POST, handleSetLevel);
  server.on("/api/status", HTTP_GET, handleGetStatus);
  server.on("/", HTTP_GET, handleRoot);
  
  server.begin();
  Serial.println("REST API WebServer started on port 5000");

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
}

void loop() {
  server.handleClient();
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
    if (ETH.linkUp()) {
      Serial.println("Scanning network for Neumann monitors (_ssc._tcp)...");
      int n = MDNS.queryService("ssc", "tcp");
      
      // Temporary cache to build the new list of online speakers
      SpeakerInfo activeSpeakers[10];
      int activeCount = 0;
      
      if (n > 0) {
        Serial.printf("mDNS discovered %d service record(s). Verifying connectivity...\n", n);
        
        for (int i = 0; i < n && activeCount < 10; i++) {
          IPAddress ip;
          IPAddress ipv4 = MDNS.address(i);
          if (ipv4 != INADDR_NONE && ipv4 != IPAddress(0,0,0,0)) {
            ip = ipv4;
          } else {
            IPAddress addr = MDNS.addressV6(i);
            if (addr.type() == IPv6 && addr != IN6ADDR_ANY) {
              uint8_t ipBytes[16];
              for (int b = 0; b < 16; b++) ipBytes[b] = addr[b];
              ip = IPAddress(IPv6, ipBytes, ETH.impl_index());
            } else {
              String host = MDNS.hostname(i);
              if (host.length() > 0) {
                IPAddress ip4 = MDNS.queryHost(host.c_str(), 1000);
                if (ip4 != INADDR_NONE && ip4 != IPAddress(0,0,0,0)) {
                  ip = ip4;
                } else {
                  IPAddress ip6 = queryHostV6(host.c_str(), 1500);
                  if (ip6.type() == IPv6 && ip6 != IN6ADDR_ANY) {
                    ip = ip6;
                  }
                }
              }
            }
          }
          
          uint16_t port = MDNS.port(i);
          String hostStr = MDNS.hostname(i);
          
          if ((ip.type() == IPv6 && ip == IN6ADDR_ANY) || (ip.type() == IPv4 && ip == IPAddress(0,0,0,0))) {
            Serial.printf("  Skipping service record %d: unresolved IP.\n", i + 1);
            continue;
          }
          
          // Verify TCP connectivity to port 45
          NetworkClient client;
          Serial.printf("  Verifying connectivity to speaker %s (%s:%d)...\n", hostStr.c_str(), ip.toString().c_str(), port);
          if (client.connect(ip, port, 500)) {
            client.stop();
            Serial.printf("  Speaker %s is ONLINE.\n", hostStr.c_str());
            
            activeSpeakers[activeCount].ip = ip;
            activeSpeakers[activeCount].port = port;
            strncpy(activeSpeakers[activeCount].hostname, hostStr.c_str(), sizeof(activeSpeakers[activeCount].hostname) - 1);
            activeSpeakers[activeCount].hostname[sizeof(activeSpeakers[activeCount].hostname) - 1] = '\0';
            activeCount++;
          } else {
            Serial.printf("  Speaker %s is OFFLINE (TCP connection failed).\n", hostStr.c_str());
          }
        }
      } else {
        Serial.println("No speakers found during mDNS scan.");
      }
      
      // Update cache and perform synchronization/initialization logic
      xSemaphoreTake(speakerMutex, portMAX_DELAY);
      
      // Check for newly discovered/connected speakers to sync volume
      for (int i = 0; i < activeCount; i++) {
        bool wasCached = false;
        for (int j = 0; j < speakerCount; j++) {
          if (strcmp(activeSpeakers[i].hostname, discoveredSpeakers[j].hostname) == 0) {
            wasCached = true;
            break;
          }
        }
        
        if (!wasCached) {
          Serial.printf("New speaker connected: %s\n", activeSpeakers[i].hostname);
          if (currentVolume > -999.0) {
            Serial.printf("Syncing new speaker %s to current volume level %.1f dB...\n", activeSpeakers[i].hostname, currentVolume);
            setSpeakerLevel(activeSpeakers[i], currentVolume);
          }
        }
      }
      
      // Save active speakers to cache
      speakerCount = activeCount;
      for (int i = 0; i < activeCount; i++) {
        discoveredSpeakers[i] = activeSpeakers[i];
      }
      
      // If volume is uninitialized and we have speakers, fetch level to initialize
      if (currentVolume < -999.0 && speakerCount > 0) {
        Serial.println("Initializing system volume level from first discovered speaker...");
        float val = getSpeakerLevel(discoveredSpeakers[0]);
        if (val > -999.0) {
          currentVolume = val;
          Serial.printf("System volume level initialized to: %.1f dB\n", currentVolume);
        } else {
          Serial.println("Failed to read volume from speaker during initialization.");
        }
      }
      
      xSemaphoreGive(speakerMutex);
      
    } else {
      Serial.println("Ethernet link down, skipping mDNS scan.");
      xSemaphoreTake(speakerMutex, portMAX_DELAY);
      speakerCount = 0;
      xSemaphoreGive(speakerMutex);
    }
    
    // Wait 5 seconds before scanning again (faster scan to detect state changes quickly)
    vTaskDelay(pdMS_TO_TICKS(5000));
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

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleGetLevel() {
  xSemaphoreTake(speakerMutex, portMAX_DELAY);
  int count = speakerCount;
  SpeakerInfo firstSpeaker;
  if (count > 0) {
    firstSpeaker = discoveredSpeakers[0];
  }
  xSemaphoreGive(speakerMutex);

  if (count == 0) {
    server.send(503, "application/json", "{\"error\":\"speakers not connected\"}");
    return;
  }

  float level = getSpeakerLevel(firstSpeaker);
  if (level < -999.0) { // Sentinel for error
    server.send(503, "application/json", "{\"error\":\"failed to read level\"}");
    return;
  }

  // Update currentVolume on successful query
  xSemaphoreTake(speakerMutex, portMAX_DELAY);
  currentVolume = level;
  xSemaphoreGive(speakerMutex);

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

  float requestedLevel = reqDoc["level"].as<float>();
  float level = requestedLevel;
  
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
  
  if (!client.connect(speaker.ip, speaker.port, 1000)) {
    Serial.println("TCP connection to speaker failed.");
    return -1000.0;
  }
  
  // Construct request JSON without spaces
  String request = "{\"audio\":{\"out\":{\"level\":null}}}";
  
  client.println(request);
  
  // Await response
  unsigned long start = millis();
  String response = "";
  while (client.connected() && millis() - start < 1000) {
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
  
  if (!client.connect(speaker.ip, speaker.port, 1000)) {
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
  while (client.connected() && millis() - start < 500) {
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
