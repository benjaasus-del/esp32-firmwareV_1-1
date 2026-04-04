#pragma once
#include <Arduino.h>

void relayInit();
void relaySet(uint8_t relayNum, bool on);   // relayNum: 1=pump, 2=fan, 3=heater
bool relayGet(uint8_t relayNum);
