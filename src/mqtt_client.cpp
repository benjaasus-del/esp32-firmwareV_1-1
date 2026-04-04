#include "mqtt_client.h"
#include "config.h"
#include "sensors.h"
#include "relay_control.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient   wifiClient;
static PubSubClient mqtt(wifiClient);
static SensorData   cachedSensorData = {};
static bool         pendingReboot    = false;

// ==================== Callback ====================
static void onMessage(char* topic, byte* payload, unsigned int length) {
  // แปลง payload เป็น null-terminated string
  char msg[length + 1];
  memcpy(msg, payload, length);
  msg[length] = '\0';


  if (strcmp(topic, TOPIC_CONTROL) != 0) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, msg);
  if (err) return;

  const char* cmd = doc["command"];
  if (!cmd) return;

  // ---- relay_control ----
  if (strcmp(cmd, "relay_control") == 0) {
    JsonObject relays = doc["relays"].as<JsonObject>();
    if (relays["relay1_pump"].is<bool>())   relaySet(1, relays["relay1_pump"].as<bool>());
    if (relays["relay2_fan"].is<bool>())    relaySet(2, relays["relay2_fan"].as<bool>());
    if (relays["relay3_heater"].is<bool>()) relaySet(3, relays["relay3_heater"].as<bool>());

    // ตอบกลับ relay state ทันทีโดยใช้ sensor data ที่ cache ไว้ ไม่ block อ่าน sensor ใหม่
    mqttPublishTelemetry(cachedSensorData);
  }

  // ---- reboot ----
  else if (strcmp(cmd, "reboot") == 0) {
    pendingReboot = true;  // จัดการใน mqttLoop() เพื่อไม่ block callback
  }

  // ---- ping ----
  else if (strcmp(cmd, "ping") == 0) {
    mqttPublishStatus("online");
  }
}

// ==================== Setup ====================
void mqttSetup() {
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setKeepAlive(MQTT_KEEPALIVE);
  mqtt.setBufferSize(512);
}

// ==================== Connect ====================
bool mqttConnect() {
  if (mqtt.connected()) return true;


  // LWT (Last Will and Testament) — backend จะรับรู้ทันทีที่ board หลุด
  JsonDocument lwtDoc;
  lwtDoc["board_id"]  = BOARD_ID;
  lwtDoc["status"]    = "offline";
  lwtDoc["timestamp"] = millis();
  char lwtBuf[128];
  serializeJson(lwtDoc, lwtBuf);

  bool ok;
  if (strlen(MQTT_USER) > 0) {
    ok = mqtt.connect(BOARD_ID, MQTT_USER, MQTT_PASS,
                      TOPIC_STATUS, 1, true, lwtBuf);
  } else {
    ok = mqtt.connect(BOARD_ID, nullptr, nullptr,
                      TOPIC_STATUS, 1, true, lwtBuf);
  }

  if (ok) {
    mqtt.subscribe(TOPIC_CONTROL, 1);
    mqttPublishStatus("online");
  }

  return ok;
}

// ==================== Sensor Cache ====================
void mqttUpdateSensorCache(const SensorData& s) {
  cachedSensorData = s;
}

// ==================== Loop ====================
void mqttLoop() {
  mqtt.loop();

  if (pendingReboot) {
    pendingReboot = false;
    mqttPublishStatus("offline");
    mqtt.loop();   // flush "offline" ออก network ก่อน restart
    delay(200);
    ESP.restart();
  }
}

bool mqttIsConnected() {
  return mqtt.connected();
}

// ==================== Publish Telemetry ====================
// Topic: smartfarm/{board_id}/telemetry
// Payload matches TelemetryPayload type ใน backend (types/index.ts)
void mqttPublishTelemetry(const SensorData& s) {
  JsonDocument doc;
  doc["board_id"]  = BOARD_ID;
  doc["timestamp"] = millis();
  doc["rssi"]      = WiFi.RSSI();

  JsonObject sensors = doc["sensors"].to<JsonObject>();
  if (s.waterTempValid)    sensors["water_temp"]   = roundf(s.waterTemp   * 10) / 10.0f;
  if (s.airTempValid)      sensors["air_temp"]     = roundf(s.airTemp     * 10) / 10.0f;
  if (s.airHumidityValid)  sensors["air_humidity"] = roundf(s.airHumidity * 10) / 10.0f;

  JsonObject relays = doc["relays"].to<JsonObject>();
  relays["relay1_pump"]    = relayGet(1);
  relays["relay2_fan"]     = relayGet(2);
  relays["relay3_heater"]  = relayGet(3);

  char buf[512];
  size_t len = serializeJson(doc, buf);

  mqtt.publish(TOPIC_TELEMETRY, (uint8_t*)buf, len, false);
}

// ==================== Publish Status ====================
// Topic: smartfarm/{board_id}/status  (retain = true)
// Payload matches StatusPayload type ใน backend
void mqttPublishStatus(const char* status) {
  JsonDocument doc;
  doc["board_id"]  = BOARD_ID;
  doc["status"]    = status;
  doc["ip"]        = WiFi.localIP().toString();
  doc["firmware"]  = FIRMWARE_VER;
  doc["uptime"]    = millis() / 1000;
  doc["timestamp"] = millis();

  char buf[256];
  size_t len = serializeJson(doc, buf);

  mqtt.publish(TOPIC_STATUS, (uint8_t*)buf, len, true);  // retain = true
}
