#include <WiFi.h>
#include <ETH.h>
#include <SPI.h>
#include <ESPmDNS.h>
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
};

// Speaker lists and mutex
SpeakerInfo discoveredSpeakers[10];
int speakerCount = 0;
SemaphoreHandle_t speakerMutex = NULL;

WebServer server(5000);
WiFiUDP udp;

SPIClass ethSPI(HSPI);

// Function declarations
void handleGetSpeakers();
void handleGetLevel();
void handleSetLevel();
void handleRoot();
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
  // Set W5500 interface configuration (static IP for IPv4 link-local)
  IPAddress local_ip(169, 254, 1, 1);
  IPAddress subnet(255, 255, 0, 0);
  IPAddress gateway(0, 0, 0, 0);
  
  // Set Ethernet static IP
  ETH.config(local_ip, gateway, subnet);
  
  if (!ETH.begin(ETH_PHY_W5500, 1, CS_PIN, IRQ_PIN, RST_PIN, ethSPI)) {
    Serial.println("W5500 Ethernet begin failed!");
  } else {
    Serial.println("W5500 Ethernet interface initialized.");
    ETH.enableIPv6(true);
  }

  // Start UDP listener
  udp.begin(LOCAL_UDP_PORT);
  Serial.print("UDP listening on local port ");
  Serial.println(LOCAL_UDP_PORT);

  // Register WebServer routes
  server.on("/api/speakers", HTTP_GET, handleGetSpeakers);
  server.on("/api/level", HTTP_GET, handleGetLevel);
  server.on("/api/level", HTTP_POST, handleSetLevel);
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

// Background FreeRTOS task for scanning speakers
void mdnsDiscoveryTask(void *pvParameters) {
  while (true) {
    if (ETH.linkUp()) {
      Serial.println("Scanning network for Neumann monitors (_ssc._tcp)...");
      int n = MDNS.queryService("ssc", "tcp");
      if (n > 0) {
        Serial.printf("mDNS discovered %d speaker(s):\n", n);
        
        xSemaphoreTake(speakerMutex, portMAX_DELAY);
        speakerCount = 0;
        for (int i = 0; i < n && i < 10; i++) {
          IPAddress addr = MDNS.addressV6(i);
          if (addr.type() == IPv6) {
            discoveredSpeakers[speakerCount].ip = addr;
          } else {
            discoveredSpeakers[speakerCount].ip = MDNS.address(i);
          }
          discoveredSpeakers[speakerCount].port = MDNS.port(i);
          speakerCount++;
          
          Serial.printf("  Speaker %d: %s:%d\n", i+1, discoveredSpeakers[i-1+1].ip.toString().c_str(), discoveredSpeakers[i-1+1].port);
        }
        xSemaphoreGive(speakerMutex);
      } else {
        Serial.println("No speakers found during mDNS scan.");
        xSemaphoreTake(speakerMutex, portMAX_DELAY);
        speakerCount = 0;
        xSemaphoreGive(speakerMutex);
      }
    } else {
      Serial.println("Ethernet link down, skipping mDNS scan.");
      xSemaphoreTake(speakerMutex, portMAX_DELAY);
      speakerCount = 0;
      xSemaphoreGive(speakerMutex);
    }
    
    // Wait 15 seconds before scanning again
    vTaskDelay(pdMS_TO_TICKS(15000));
  }
}

void handleGetSpeakers() {
  JsonDocument doc;
  JsonArray arr = doc.createNestedArray("speakers");

  xSemaphoreTake(speakerMutex, portMAX_DELAY);
  for (int i = 0; i < speakerCount; i++) {
    JsonObject obj = arr.createNestedObject();
    obj["hostname"] = nullptr;
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
    server.send(500, "application/json", "{\"error\":\"failed to read level\"}");
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

  // Send level to all speakers
  bool success = true;
  for (int i = 0; i < count; i++) {
    if (!setSpeakerLevel(speakers[i], level)) {
      success = false;
    }
  }

  if (!success) {
    server.send(500, "application/json", "{\"error\":\"failed to set level\"}");
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

// Low-level UDP communication function to read level
float getSpeakerLevel(const SpeakerInfo &speaker) {
  // Clear any incoming packets first
  udp.clear();

  // Construct request JSON
  String request = "{\"audio\": {\"out\": {\"level\": null}}}";
  
  Serial.printf("Querying volume from speaker %s:%d...\n", speaker.ip.toString().c_str(), speaker.port);
  
  udp.beginPacket(speaker.ip, speaker.port);
  udp.write((const uint8_t*)request.c_str(), request.length());
  if (udp.endPacket() == 0) {
    Serial.println("UDP begin/end packet failed.");
    return -1000.0;
  }

  // Await response
  unsigned long start = millis();
  char buffer[512];
  int len = 0;
  while (millis() - start < 400) { // 400ms timeout
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      len = udp.read(buffer, sizeof(buffer) - 1);
      if (len > 0) {
        buffer[len] = '\0';
        break;
      }
    }
    delay(10);
  }

  if (len == 0) {
    Serial.println("Timeout waiting for level query response from speaker.");
    return -1000.0;
  }

  Serial.printf("Speaker query response: %s\n", buffer);

  // Parse JSON response
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, buffer);
  if (err) {
    Serial.println("Failed to parse speaker JSON response.");
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

// Low-level UDP communication function to write level
bool setSpeakerLevel(const SpeakerInfo &speaker, float level) {
  // Clear incoming packets
  udp.clear();

  // Construct command JSON
  char request[128];
  snprintf(request, sizeof(request), "{\"audio\":{\"out\":{\"level\":%.1f}}}", level);
  
  Serial.printf("Setting volume on speaker %s:%d to %.1f dB...\n", speaker.ip.toString().c_str(), speaker.port, level);

  udp.beginPacket(speaker.ip, speaker.port);
  udp.write((const uint8_t*)request, strlen(request));
  if (udp.endPacket() == 0) {
    Serial.println("UDP send packet failed.");
    return false;
  }
  
  // Wait up to 100ms for reply, but we won't fail the set command if we don't receive it.
  unsigned long start = millis();
  while (millis() - start < 100) {
    if (udp.parsePacket() > 0) {
      char buf[256];
      int n = udp.read(buf, sizeof(buf) - 1);
      if (n > 0) {
        buf[n] = '\0';
        Serial.printf("Speaker set response ack: %s\n", buf);
      }
      break;
    }
    delay(5);
  }

  return true;
}

void handleRoot() {
  server.send(200, "text/html", HTML_CONTENT);
}
