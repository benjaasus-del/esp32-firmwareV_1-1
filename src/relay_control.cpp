#include "relay_control.h"
#include "config.h"

static const uint8_t RELAY_PINS[3] = {
  PIN_RELAY1_PUMP,
  PIN_RELAY2_FAN,
  PIN_RELAY3_HEATER,
};

static bool relayStates[3] = { false, false, false };

static void writePin(uint8_t pin, bool on) {
  // RELAY_ACTIVE_HIGH = false → LOW = ON (relay module ทั่วไป)
  digitalWrite(pin, RELAY_ACTIVE_HIGH ? on : !on);
}

void relayInit() {
  for (int i = 0; i < 3; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    writePin(RELAY_PINS[i], false);  // ปิดทุก relay ตอน boot
    relayStates[i] = false;
  }
}

void relaySet(uint8_t relayNum, bool on) {
  if (relayNum < 1 || relayNum > 3) return;
  uint8_t i = relayNum - 1;
  relayStates[i] = on;
  writePin(RELAY_PINS[i], on);
}

bool relayGet(uint8_t relayNum) {
  if (relayNum < 1 || relayNum > 3) return false;
  return relayStates[relayNum - 1];
}
