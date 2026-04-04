#include "sensors.h"
#include "config.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ModbusMaster.h>
#include <esp_random.h>

// สุ่มค่า float ในช่วง [minVal, maxVal] ทศนิยม 1 ตำแหน่ง
static float simRandom(float minVal, float maxVal) {
  float range = (maxVal - minVal) * 10.0f;
  return minVal + (float)(esp_random() % (uint32_t)range) / 10.0f;
}

static OneWire           oneWire(PIN_ONE_WIRE);
static DallasTemperature ds18b20(&oneWire);
static ModbusMaster      modbus;

static bool readActiveLevel(uint8_t pin) {
  int raw = digitalRead(pin);
  return LEVEL_SENSOR_ACTIVE_HIGH ? (raw == HIGH) : (raw == LOW);
}

void sensorsInit() {
  pinMode(PIN_LEVEL_OVERFLOW, INPUT_PULLUP);
  pinMode(PIN_LEVEL_DRY, INPUT_PULLUP);

  // DS18B20
  ds18b20.begin();
  ds18b20.setResolution(11);          // 11-bit (~375ms conversion)
  ds18b20.setWaitForConversion(false);

  // XY-MD03 Modbus RTU via Serial0 (MAX13487 auto-direction ไม่ต้องการ DE pin)
  // Serial0 ถูก reconfigure ที่นี่ — USB debug ไม่พร้อมใช้งานหลังจากนี้
  Serial.begin(MODBUS_BAUD, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
  modbus.begin(MODBUS_SLAVE_ADDR, Serial);
}

SensorData sensorsRead() {
  SensorData data = {};

  data.waterOverflow = readActiveLevel(PIN_LEVEL_OVERFLOW);
  data.waterDry      = readActiveLevel(PIN_LEVEL_DRY);

  // ---- DS18B20 : Water Temperature ----
  ds18b20.requestTemperatures();
  delay(400);  // รอ conversion เสร็จ (11-bit = 375ms)

  float wt = ds18b20.getTempCByIndex(0);

  // 85.0°C = power-on default (sensor ยังไม่พร้อม), DEVICE_DISCONNECTED = -127
  data.waterTempValid = (wt != DEVICE_DISCONNECTED_C && wt != 85.0f);
  data.waterTemp      = data.waterTempValid ? wt : NAN;

#ifdef SIMULATE_SENSORS
  if (!data.waterTempValid) {
    data.waterTemp      = simRandom(SIM_WATER_TEMP_MIN, SIM_WATER_TEMP_MAX);
    data.waterTempValid = true;
  }
#endif

  // DS18B20 read error logged silently (Serial0 used for Modbus)

  // ---- XY-MD03 : Air Temp + Humidity (Modbus FC04 Input Registers) ----
  // Register 0x0001 = Temperature × 10
  // Register 0x0002 = Humidity    × 10
  uint8_t result = modbus.readInputRegisters(0x0001, 2);

  if (result == ModbusMaster::ku8MBSuccess) {
    data.airTemp          = (float)modbus.getResponseBuffer(0) / 10.0f;
    data.airHumidity      = (float)modbus.getResponseBuffer(1) / 10.0f;
    data.airTempValid     = true;
    data.airHumidityValid = true;
  } else {
    data.airTempValid     = false;
    data.airHumidityValid = false;
    data.airTemp          = NAN;
    data.airHumidity      = NAN;
    (void)result; // Serial0 used for Modbus, cannot debug print

#ifdef SIMULATE_SENSORS
    data.airTemp          = simRandom(SIM_AIR_TEMP_MIN, SIM_AIR_TEMP_MAX);
    data.airHumidity      = simRandom(SIM_HUMIDITY_MIN, SIM_HUMIDITY_MAX);
    data.airTempValid     = true;
    data.airHumidityValid = true;
#endif
  }

  return data;
}
