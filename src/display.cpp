#include "display.h"
#include "config.h"
#include "relay_control.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
static bool oledReady = false;

// ================================================================
//  Layout (128×64)
//
//  y= 0..8   Header: "SmartFarm"  |  "W:OK  M:OK"     font1
//  y= 9      ─── divider ───
//  y=13..20  H2O value                                 font1
//  y=24..31  Air temp + Humidity                       font1
//  y=35..42  Level sensors (Overflow + Dry)            font1
//  y=44      ─── divider ───
//  y=47..63  Relay boxes (3 × 40px wide)
//            ON  = filled rect, black text
//            OFF = empty  rect, white text
// ================================================================

static void drawRelayBox(uint8_t x, const char* label, bool on) {
  if (on) {
    oled.fillRect(x, 47, 40, 16, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
  } else {
    oled.drawRect(x, 47, 40, 16, SSD1306_WHITE);
    oled.setTextColor(SSD1306_WHITE);
  }
  oled.setTextSize(1);
  // center label
  uint8_t lw = strlen(label) * 6;
  oled.setCursor(x + (40 - lw) / 2, 49);
  oled.print(label);
  // center state
  const char* state = on ? "ON" : "OFF";
  uint8_t sw = strlen(state) * 6;
  oled.setCursor(x + (40 - sw) / 2, 56);
  oled.print(state);
  oled.setTextColor(SSD1306_WHITE);
}

void displayInit() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) return;
  oledReady = true;

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(20, 24);
  oled.print("SmartFarm");
  oled.setCursor(20, 36);
  oled.print("Booting...");
  oled.display();
}

void displayShowIP(const char* ip) {
  if (!oledReady) return;
  oled.clearDisplay();

  // ── Header ──────────────────────────────────────────────────
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.print("SmartFarm");
  oled.drawFastHLine(0, 9, 128, SSD1306_WHITE);

  // ── WiFi Connected ──────────────────────────────────────────
  oled.setCursor(16, 18);
  oled.print("WiFi Connected!");

  // ── IP Address (font2) ──────────────────────────────────────
  oled.setCursor(0, 34);
  oled.print("IP:");
  oled.setTextSize(1);
  oled.setCursor(0, 46);
  oled.print(ip);

  oled.display();
}

void displayUpdate(const SensorData& data, bool wifiOk, bool mqttOk) {
  if (!oledReady) return;
  oled.clearDisplay();

  // ── Header ──────────────────────────────────────────────────
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.print("SmartFarm");
  oled.setCursor(66, 0);
  oled.print(wifiOk ? "W:OK" : "W:--");
  oled.setCursor(96, 0);
  oled.print(mqttOk ? "M:OK" : "M:--");

  oled.drawFastHLine(0, 9, 128, SSD1306_WHITE);

  // ── Water Temperature ────────────────────────────────────────
  oled.setTextSize(1);
  oled.setCursor(0, 14);
  oled.print("H2O:");
  oled.setCursor(30, 14);
  if (data.waterTempValid) {
    oled.printf("%.1fC", data.waterTemp);
  } else {
    oled.print("---C");
  }

  // ── Air Temp + Humidity ──────────────────────────────────────
  oled.setCursor(0, 25);
  oled.print("Air:");
  if (data.airTempValid) {
    oled.printf("%.1fC", data.airTemp);
  } else {
    oled.print("---C");
  }
  oled.setCursor(74, 25);
  if (data.airHumidityValid) {
    oled.printf("Hum:%2.0f%%", data.airHumidity);
  } else {
    oled.print("Hum:--%");
  }

  // ── Level Sensors ────────────────────────────────────────────
  oled.setCursor(0, 36);
  oled.print("Ovf:");
  oled.print(data.waterOverflow ? "LOW" : "OK ");
  oled.setCursor(68, 36);
  oled.print("Dry:");
  oled.print(data.waterDry ? "LOW" : "OK ");

  oled.drawFastHLine(0, 44, 128, SSD1306_WHITE);

  // ── Relay Boxes ──────────────────────────────────────────────
  drawRelayBox(1,  "Pump", relayGet(1));
  drawRelayBox(44, "Fan",  relayGet(2));
  drawRelayBox(87, "Heat", relayGet(3));

  oled.display();
}
