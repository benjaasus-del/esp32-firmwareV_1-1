#include "wifi_manager.h"
#include "config.h"
#include "display.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>

static WiFiManager wm;

// ================================================================
//  wifiManagerCheckReset
//  เรียกตอน setup() ก่อน connect — ตรวจ SW1 ค้าง 5 วินาที
//  GPIO 34: input-only, external 10kΩ pull-up → กด = LOW
// ================================================================
void wifiManagerCheckReset() {
  pinMode(PIN_SW1, INPUT);  // GPIO 34 ไม่มี internal pull-up/down

  // ถ้าไม่ได้กด SW1 ให้ผ่านเลย
  if (digitalRead(PIN_SW1) == HIGH) return;

  // SW1 ถูกกดอยู่ → เริ่มนับถอยหลัง
  uint32_t pressStart = millis();
  bool resetTriggered = false;

  for (int sec = WIFI_RESET_HOLD_MS / 1000; sec > 0; sec--) {
    displayWifiResetCountdown(sec);
    delay(1000);

    if (digitalRead(PIN_SW1) == HIGH) {
      // ปล่อยก่อนครบเวลา → ยกเลิก
      return;
    }
  }

  // กดค้างครบเวลา → ล้าง credential
  wm.resetSettings();
  displayWifiResetDone();
  delay(2000);
}

// ================================================================
//  wifiManagerConnect
//  auto-connect ถ้ามี credential บันทึกใน NVS
//  ถ้าไม่มี / timeout → เปิด AP portal ให้ตั้งค่า
// ================================================================
bool wifiManagerConnect() {
  wm.setConnectTimeout(WIFI_TIMEOUT_MS / 1000);
  wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);

  // แสดงหน้าจอ connecting ระหว่างรอ
  displayWifiConnecting();

  // เปิด portal เมื่อ WiFiManager เข้า AP mode
  wm.setAPCallback([](WiFiManager*) {
    displayWifiPortal(WIFI_PORTAL_SSID);
  });

  // autoConnect: ถ้ามี credential → connect อัตโนมัติ
  //              ถ้าไม่มี / ล้มเหลว → เปิด portal
  bool ok = wm.autoConnect(WIFI_PORTAL_SSID);

  return ok;
}

// ================================================================
//  wifiManagerReconnect
//  เรียกใน loop() เมื่อ WiFi หลุด — ใช้ credential ที่บันทึกไว้
// ================================================================
void wifiManagerReconnect() {
  if (WiFi.status() == WL_CONNECTED) return;

  // WiFi.begin() ไม่มี argument → ใช้ credential จาก NVS (บันทึกโดย WiFiManager)
  WiFi.begin();
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(500);
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
  }
}
