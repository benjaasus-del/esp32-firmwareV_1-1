#pragma once
#include <Arduino.h>

struct SensorData {
  float waterTemp;        // DS18B20 (°C)
  float airTemp;          // XY-MD03 (°C)
  float airHumidity;      // XY-MD03 (%)
  bool  waterTempValid;
  bool  airTempValid;
  bool  airHumidityValid;
};

void        sensorsInit();
SensorData  sensorsRead();
