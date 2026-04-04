#pragma once
#include <Arduino.h>
#include "sensors.h"

void mqttSetup();
bool mqttConnect();
void mqttLoop();
bool mqttIsConnected();
void mqttUpdateSensorCache(const SensorData& s);
void mqttPublishTelemetry(const SensorData& s);
void mqttPublishStatus(const char* status);
