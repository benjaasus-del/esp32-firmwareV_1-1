#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "sensors.h"
#include "relay_control.h"
#include "mqtt_client.h"
#include "display.h"

// ==================== WiFi ====================
static void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(500);
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
  }
}

// ==================== Setup ====================
void setup() {
  // Serial0 (GPIO1/3) ถูกใช้สำหรับ Modbus (MAX13487) — ไม่เรียก Serial.begin() ที่นี่
  // sensorsInit() จะ configure Serial0 ที่ MODBUS_BAUD

  relayInit();
  displayInit();
  sensorsInit();
  wifiConnect();

  if (WiFi.status() == WL_CONNECTED) {
    displayShowIP(WiFi.localIP().toString().c_str());
    delay(2000);
    mqttSetup();
    mqttConnect();
  }
}

// ==================== Loop ====================
static uint32_t lastTelemetry  = 0;
static uint32_t lastDisplay    = 0;
static uint32_t lastStatus     = 0;
static uint32_t lastReconnect  = 0;
static SensorData lastSensorData = {};

void loop() {
  uint32_t now = millis();

  // ---- WiFi reconnect ----
  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  if (!wifiOk) {
    if (now - lastReconnect >= RECONNECT_INTERVAL_MS) {
      lastReconnect = now;
      wifiConnect();
    }
    if (now - lastDisplay >= DISPLAY_INTERVAL_MS) {
      lastDisplay = now;
      displayUpdate(lastSensorData, false, false);
    }
    return;
  }

  // ---- MQTT reconnect ----
  bool mqttOk = mqttIsConnected();
  if (!mqttOk) {
    if (now - lastReconnect >= RECONNECT_INTERVAL_MS) {
      lastReconnect = now;
      mqttConnect();
    }
    if (now - lastDisplay >= DISPLAY_INTERVAL_MS) {
      lastDisplay = now;
      displayUpdate(lastSensorData, true, false);
    }
    return;
  }

  mqttLoop();

  // ---- Telemetry (read sensors once, share with display) ----
  if (now - lastTelemetry >= TELEMETRY_INTERVAL_MS) {
    lastTelemetry = now;
    lastSensorData = sensorsRead();
    mqttUpdateSensorCache(lastSensorData);
    mqttPublishTelemetry(lastSensorData);
    lastDisplay = now;
    displayUpdate(lastSensorData, true, mqttOk);
  }

  // ---- Display update ----
  if (now - lastDisplay >= DISPLAY_INTERVAL_MS) {
    lastDisplay = now;
    displayUpdate(lastSensorData, true, mqttOk);
  }

  // ---- Heartbeat status ----
  if (now - lastStatus >= STATUS_INTERVAL_MS) {
    lastStatus = now;
    mqttPublishStatus("online");
  }
}
