#pragma once
#include <Arduino.h>
#include "sensors.h"

void displayInit();
void displayShowIP(const char* ip);
void displayUpdate(const SensorData& data, bool wifiOk, bool mqttOk);
