// verticalGardenAI.ino
// Vertical Garden AI — ESP32 IoT Sensor Controller
// 
// Features:
//   - BH1750 (4x) via TCA9548A — Light intensity
//   - AM2315 — Temperature & Humidity
//   - MQTT publish to Pi4 (Laila)
//   - ArduinoOTA for wireless updates
//   - GitHub Actions CI/CD ready
//
// Hardware:
//   ESP32-D0WD-V3 | TCA9548A (0x70) | BH1750 x4 | AM2315
//
// Wiring:
//   ESP32 SDA=GPIO21, SCL=GPIO22 → TCA9548A
//   TCA9548A: Ch.0→BH1750#1, Ch.1→BH1750#2, Ch.2→BH1750#3
//             Ch.4→BH1750#4, Ch.5→AM2315

#include <Wire.h>
#include <BH1750.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>

// ===== WiFi =====
const char* WIFI_SSID = "Aroon2.4G";
const char* WIFI_PASS = "0945639334";

// ===== MQTT =====
const char* MQTT_BROKER = "192.168.0.120";     // Pi4 local (Tailscale if remote)
const uint16_t MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "verticalgarden-esp32";
const char* MQTT_TOPIC_PREFIX = "sensor/light";

// ===== OTA =====
const char* OTA_HOSTNAME = "verticalgarden-esp32";
const char* OTA_PASSWORD = "123456";

// ===== I2C (TCA9548A) =====
const uint8_t PIN_SDA = 21;
const uint8_t PIN_SCL = 22;
const uint8_t TCA9548A_ADDR = 0x70;
const uint8_t BH1750_CH[] = {0, 1, 2, 4};
const uint8_t BH1750_NUM = 4;
const uint8_t BH1750_I2C_ADDR = 0x23;
const float CAL_FACTOR = 2.1;
const uint8_t AM2315_CH = 5;
const uint8_t AM2315_I2C_ADDR = 0x5C;

// ===== Timing =====
const unsigned long READ_INTERVAL_MS = 2000;
const unsigned long MQTT_PUBLISH_INTERVAL_MS = 30000;
const unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;
unsigned long lastReadTime = 0;
unsigned long lastMqttPublishTime = 0;
unsigned long lastMqttReconnectTime = 0;

// ===== Objects =====
BH1750 lightMeters[BH1750_NUM];
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
StaticJsonDocument<384> jsonDoc;

// ===== Sensor Data =====
float luxVals[BH1750_NUM] = {0};
float temperature = NAN;
float humidity = NAN;

// ===== Built-in LED =====
const uint8_t LED_PIN = 2;

// ============================================================
// TCA9548A: Select I2C channel
// ============================================================
void selectChannel(uint8_t channel) {
  Wire.beginTransmission(TCA9548A_ADDR);
  Wire.write(1 << channel);
  Wire.endTransmission();
}

// ============================================================
// BH1750: Init with retry
// ============================================================
bool initBH1750(uint8_t index) {
  selectChannel(BH1750_CH[index]);
  delay(50);

  for (int r = 0; r < 3; r++) {
    if (lightMeters[index].begin(BH1750::CONTINUOUS_HIGH_RES_MODE,
                                  BH1750_I2C_ADDR, &Wire)) {
      return true;
    }
    delay(200);
  }
  return false;
}

// ============================================================
// AM2315: Read via raw I2C
// ============================================================
bool readAM2315(float &temp, float &humid) {
  selectChannel(AM2315_CH);
  delay(50);

  // Wake
  Wire.beginTransmission(AM2315_I2C_ADDR);
  Wire.write((uint8_t)0x00);
  Wire.endTransmission();
  delay(50);

  // Read command: 0x03, start=0x00, len=4
  Wire.beginTransmission(AM2315_I2C_ADDR);
  Wire.write(0x03);
  Wire.write(0x00);
  Wire.write(0x04);
  Wire.endTransmission();
  delay(20);

  uint8_t received = Wire.requestFrom(AM2315_I2C_ADDR, (uint8_t)8);
  if (received < 8) return false;

  uint8_t buf[8];
  for (uint8_t i = 0; i < 8; i++) buf[i] = Wire.read();
  if (buf[0] != 0x03 || buf[1] != 4) return false;

  uint16_t rawHumid = ((uint16_t)buf[2] << 8) | buf[3];
  uint16_t rawTemp  = ((uint16_t)(buf[4] & 0x7F) << 8) | buf[5];

  humid = rawHumid / 10.0;
  temp  = rawTemp / 10.0;
  if (buf[4] >> 7) temp = -temp;

  return true;
}

// ============================================================
// MQTT Publish
// ============================================================
void publishSensorData() {
  if (!mqttClient.connected()) return;

  char topic[64];
  char payload[32];

  // Individual topics
  for (uint8_t i = 0; i < BH1750_NUM; i++) {
    snprintf(topic, sizeof(topic), "%s/lux%d", MQTT_TOPIC_PREFIX, i + 1);
    snprintf(payload, sizeof(payload), "%.0f", luxVals[i]);
    mqttClient.publish(topic, payload, true);
  }

  if (!isnan(temperature)) {
    snprintf(topic, sizeof(topic), "%s/temperature", MQTT_TOPIC_PREFIX);
    snprintf(payload, sizeof(payload), "%.1f", temperature);
    mqttClient.publish(topic, payload, true);
  }

  if (!isnan(humidity)) {
    snprintf(topic, sizeof(topic), "%s/humidity", MQTT_TOPIC_PREFIX);
    snprintf(payload, sizeof(payload), "%.1f", humidity);
    mqttClient.publish(topic, payload, true);
  }

  // JSON combined
  jsonDoc.clear();
  jsonDoc["lux1"] = (int)round(luxVals[0]);
  jsonDoc["lux2"] = (int)round(luxVals[1]);
  jsonDoc["lux3"] = (int)round(luxVals[2]);
  jsonDoc["lux4"] = (int)round(luxVals[3]);
  jsonDoc["temperature"] = temperature;
  jsonDoc["humidity"] = humidity;
  jsonDoc["rssi"] = WiFi.RSSI();
  jsonDoc["uptime_s"] = millis() / 1000;
  jsonDoc["cal"] = CAL_FACTOR;

  char jsonBuffer[256];
  size_t jsonLen = serializeJson(jsonDoc, jsonBuffer);

  snprintf(topic, sizeof(topic), "%s/data", MQTT_TOPIC_PREFIX);
  mqttClient.publish(topic, (const uint8_t*)jsonBuffer, jsonLen, true);
}

void publishStatus(const char* status) {
  char topic[64];
  snprintf(topic, sizeof(topic), "%s/status", MQTT_TOPIC_PREFIX);
  mqttClient.publish(topic, status, true);
}

// ============================================================
// WiFi
// ============================================================
void connectWiFi() {
  Serial.print(F("\nWiFi: Connecting to "));
  Serial.print(WIFI_SSID);
  Serial.print(F("..."));

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(F("."));
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F(" OK"));
    Serial.print(F("  IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F(" FAILED"));
  }
}

// ============================================================
// MQTT
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Reserved for OTA trigger or commands
}

bool connectMQTT() {
  if (!mqttClient.connected()) {
    Serial.print(F("MQTT: Connecting..."));

    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);

    if (mqttClient.connect(MQTT_CLIENT_ID)) {
      Serial.println(F(" OK"));
      publishStatus("ONLINE");
      digitalWrite(LED_PIN, HIGH);
      return true;
    } else {
      Serial.print(F(" FAIL (rc="));
      Serial.print(mqttClient.state());
      Serial.println(F(")"));
      digitalWrite(LED_PIN, LOW);
      return false;
    }
  }
  return true;
}

// ============================================================
// OTA
// ============================================================
void setupOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("OTA Start: " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("OTA: Ready @ " + WiFi.localIP().toString());
}

// ============================================================
// Read all sensors
// ============================================================
void readSensors() {
  // BH1750
  for (uint8_t i = 0; i < BH1750_NUM; i++) {
    selectChannel(BH1750_CH[i]);
    delay(10);

    float raw = lightMeters[i].readLightLevel();
    if (raw < 0) {
      initBH1750(i);
      delay(10);
      raw = lightMeters[i].readLightLevel();
    }
    luxVals[i] = (raw >= 0) ? (raw * CAL_FACTOR) : -1;
  }

  // AM2315
  readAM2315(temperature, humidity);

  // Serial log
  Serial.print(millis());
  Serial.print(F(" | "));
  for (uint8_t i = 0; i < BH1750_NUM; i++) {
    if (luxVals[i] >= 0) Serial.print(luxVals[i], 1);
    else Serial.print(F("ERR"));
    Serial.print(i < BH1750_NUM - 1 ? F(" | ") : F(""));
  }
  Serial.print(F(" | "));
  if (!isnan(temperature)) Serial.print(temperature, 1);
  else Serial.print(F("--"));
  Serial.print(F(" | "));
  if (!isnan(humidity)) Serial.print(humidity, 1);
  else Serial.print(F("--"));
  Serial.println();
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("\n=========================================="));
  Serial.println(F("  Vertical Garden AI — ESP32 IoT Sensor"));
  Serial.println(F("  BH1750 x4 + AM2315 + TCA9548A + MQTT"));
  Serial.println(F("=========================================="));

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // I2C
  Wire.begin(PIN_SDA, PIN_SCL);
  Serial.print(F("I2C: SDA=GPIO"));
  Serial.print(PIN_SDA);
  Serial.print(F(", SCL=GPIO"));
  Serial.println(PIN_SCL);

  // Init BH1750
  uint8_t detected = 0;
  for (uint8_t i = 0; i < BH1750_NUM; i++) {
    if (initBH1750(i)) {
      Serial.print(F("BH1750 #"));
      Serial.print(i + 1);
      Serial.print(F(" (Ch."));
      Serial.print(BH1750_CH[i]);
      Serial.println(F("): OK"));
      detected++;
    } else {
      Serial.print(F("BH1750 #"));
      Serial.print(i + 1);
      Serial.print(F(" (Ch."));
      Serial.print(BH1750_CH[i]);
      Serial.println(F("): FAIL"));
    }
  }
  Serial.print(F("BH1750: "));
  Serial.print(detected);
  Serial.print(F("/"));
  Serial.print(BH1750_NUM);
  Serial.print(F(" | Cal: "));
  Serial.println(CAL_FACTOR, 1);

  // AM2315 test
  float t, h;
  if (readAM2315(t, h)) {
    Serial.print(F("AM2315 (Ch."));
    Serial.print(AM2315_CH);
    Serial.println(F("): OK"));
  } else {
    Serial.print(F("AM2315 (Ch."));
    Serial.print(AM2315_CH);
    Serial.println(F("): FAIL"));
  }

  // WiFi
  connectWiFi();

  // OTA
  setupOTA();

  // MQTT
  connectMQTT();

  Serial.println(F("============= START ============="));
  Serial.println(F("Time | L1 | L2 | L3 | L4 | Temp | Hum"));
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  // WiFi + OTA
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();

    // MQTT reconnect
    if (!mqttClient.connected()) {
      if (now - lastMqttReconnectTime >= MQTT_RECONNECT_INTERVAL_MS) {
        lastMqttReconnectTime = now;
        connectMQTT();
      }
    } else {
      mqttClient.loop();
    }
  }

  // Read sensors
  if (now - lastReadTime >= READ_INTERVAL_MS) {
    lastReadTime = now;
    readSensors();
  }

  // Publish MQTT
  if (now - lastMqttPublishTime >= MQTT_PUBLISH_INTERVAL_MS) {
    lastMqttPublishTime = now;
    publishSensorData();
  }
}
