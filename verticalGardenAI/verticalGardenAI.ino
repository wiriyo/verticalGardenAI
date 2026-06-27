// verticalGardenAI.ino
// Main firmware for Vertical Garden AI — ESP32 IoT Controller
// 
// Architecture:
// - Reads sensors (BH1750, AM2315, etc.)
// - Publishes to MQTT broker
// - Supports OTA updates
// - Auto WiFi reconnection

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>

// ===== WiFi Configuration =====
const char* WIFI_SSID = "Aroon2.4G";
const char* WIFI_PASS = "0945639334";

// ===== MQTT Configuration =====
// TODO: Change to Pi4 Tailscale IP when deploying to site
const char* MQTT_BROKER = "100.116.43.103";  // Pi4 Tailscale IP
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC_SENSOR = "sensor/verticalgarden/data";
const char* MQTT_TOPIC_STATUS = "sensor/verticalgarden/status";

// ===== OTA Configuration =====
const char* OTA_HOSTNAME = "verticalgarden-esp32";
const char* OTA_PASSWORD = "123456";

// ===== MQTT Client =====
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
StaticJsonDocument<512> jsonDoc;

// ===== Timing =====
unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL_MS = 5000;  // Read every 5s
unsigned long lastMqttPublish = 0;
const unsigned long MQTT_INTERVAL_MS = 30000;    // Publish every 30s

// ===== Forward Declarations =====
void setupWiFi();
void setupMQTT();
void setupOTA();
void readSensors();
void publishData();
void mqttCallback(char* topic, byte* payload, unsigned int length);

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== Vertical Garden AI v0.1 ===");

  setupWiFi();
  setupMQTT();
  setupOTA();

  Serial.println("✅ Setup complete!");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  // Handle OTA
  ArduinoOTA.handle();

  // Reconnect WiFi if needed
  if (WiFi.status() != WL_CONNECTED) {
    setupWiFi();
  }

  // Reconnect MQTT if needed
  if (!mqttClient.connected()) {
    mqttConnect();
  }
  mqttClient.loop();

  // Read sensors periodically
  unsigned long now = millis();
  if (now - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = now;
    readSensors();
  }

  // Publish to MQTT periodically
  if (now - lastMqttPublish >= MQTT_INTERVAL_MS) {
    lastMqttPublish = now;
    publishData();
  }
}

// ============================================================
// WiFi
// ============================================================
void setupWiFi() {
  Serial.print("📡 Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected!");
    Serial.print("   IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WiFi connection failed!");
  }
}

// ============================================================
// MQTT
// ============================================================
void setupMQTT() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
}

void mqttConnect() {
  while (!mqttClient.connected()) {
    Serial.print("📡 Connecting to MQTT...");
    String clientId = "verticalgarden-esp32-" + String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" ✅");
      // Subscribe to OTA trigger topic
      mqttClient.subscribe("sensor/verticalgarden/ota");
      mqttClient.publish(MQTT_TOPIC_STATUS, "ONLINE");
    } else {
      Serial.print(" ❌ (rc=");
      Serial.print(mqttClient.state());
      Serial.println(") retrying in 5s...");
      delay(5000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("📨 MQTT message: ");
  Serial.print(topic);

  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';

  if (strcmp(topic, "sensor/verticalgarden/ota") == 0) {
    if (strcmp(message, "CHECK") == 0) {
      Serial.println(" → OTA check requested");
      // Laila will handle OTA via HTTP
    }
  }
}

// ============================================================
// OTA
// ============================================================
void setupOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("🔄 OTA update started...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n✅ OTA update complete!");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("   Progress: %u%%\r", progress / (total / 100));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("❌ OTA error [%u]\n", error);
  });

  ArduinoOTA.begin();
  Serial.println("🔄 OTA ready!");
}

// ============================================================
// Sensors (placeholder — expand with BH1750 + AM2315)
// ============================================================
void readSensors() {
  // TODO: Add actual sensor reading code
  // BH1750 via TCA9548A
  // AM2315 Temperature & Humidity
}

void publishData() {
  if (!mqttClient.connected()) return;

  jsonDoc.clear();
  jsonDoc["device"] = "verticalgarden-esp32";
  jsonDoc["ip"] = WiFi.localIP().toString();
  jsonDoc["rssi"] = WiFi.RSSI();
  jsonDoc["uptime"] = millis() / 1000;

  // TODO: Add sensor values
  // jsonDoc["lux1"] = value;
  // jsonDoc["temperature"] = temp;
  // jsonDoc["humidity"] = humid;

  char buffer[256];
  serializeJson(jsonDoc, buffer);

  if (mqttClient.publish(MQTT_TOPIC_SENSOR, buffer)) {
    Serial.println("📤 Data published to MQTT");
  }
}
