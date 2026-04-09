#pragma once

// ================================================================
//  SmartFarm ESP32 — Configuration
//  แก้ไขค่าตรงนี้ก่อน flash firmware
// ================================================================

// ==================== Board Identity ====================
#define BOARD_ID      "ESP32-FARM-001-Benja-Benz"   // ต้องตรงกับ boardId ที่ register ใน backend
#define FIRMWARE_VER  "1.0.0"

// ==================== WiFi ====================
// SSID/Password บันทึกใน NVS โดย WiFiManager — ไม่ต้อง hardcode ที่นี่
#define WIFI_TIMEOUT_MS        15000   // ms รอ connect ก่อน fallback portal
#define WIFI_PORTAL_SSID       "SmartFarm-Setup"
#define WIFI_PORTAL_TIMEOUT_S  180     // วินาที portal ก่อน reboot อัตโนมัติ
#define WIFI_RESET_HOLD_MS     5000    // ms กด SW1 ค้างเพื่อ reset WiFi

// ==================== MQTT Broker ====================
#define MQTT_BROKER     "broker.hivemq.com"  // HiveMQ Public Free Broker
#define MQTT_PORT       1883
#define MQTT_USER       ""
#define MQTT_PASS       ""
#define MQTT_KEEPALIVE  60               // seconds

// MQTT Topics (auto-generated จาก BOARD_ID)
#define TOPIC_TELEMETRY  "smartfarm/" BOARD_ID "/telemetry"
#define TOPIC_STATUS     "smartfarm/" BOARD_ID "/status"
#define TOPIC_CONTROL    "smartfarm/" BOARD_ID "/control"

// ==================== GPIO Pins ====================

// DS18B20 — Water Temperature (OneWire)
#define PIN_ONE_WIRE       14

// XY-MD03 — Air Temp + Humidity via RS485 Modbus RTU (MAX13487 auto-direction)
#define PIN_RS485_RX       3    // ESP32 RX0 (Serial0)
#define PIN_RS485_TX       1    // ESP32 TX0 (Serial0)
// MAX13487: ไม่ต้องการ DE pin (auto-direction)

// Relay Outputs
#define PIN_RELAY1_PUMP    17
#define PIN_RELAY2_FAN     16
#define PIN_RELAY3_HEATER  4

// Level Sensors (Opto Isolate, Active LOW)
#define PIN_LEVEL_OVERFLOW 33 //iso1
#define PIN_LEVEL_DRY      27 //iso2

// Toggle Switch Inputs (Active LOW, external pull-up 10kΩ)
#define PIN_SW1            34   // SW1 — Toggle Switch 1
#define PIN_SW2            35   // SW2 — Toggle Switch 2
#define PIN_SW3            32   // SW3 — Toggle Switch 3

// ==================== Modbus (XY-MD03) ====================
#define MODBUS_SLAVE_ADDR  0x01
#define MODBUS_BAUD        9600

// ==================== Relay Logic ====================
// false = Active LOW (relay module ทั่วไปที่มี optocoupler)
// true  = Active HIGH
#define RELAY_ACTIVE_HIGH  false

// ==================== Level Sensor Logic ====================
// false = Active LOW ผ่าน opto isolate
// true  = Active HIGH
#define LEVEL_SENSOR_ACTIVE_HIGH false

// ==================== Toggle Switch Logic ====================
// Active LOW — มี external pull-up 10kΩ, กด = GND = LOW
// GPIO 34, 35 เป็น input-only (ไม่มี internal pull-up/down)
#define SW_ACTIVE_HIGH false

// ==================== OLED SSD1306 (I2C) ====================
#define PIN_OLED_SDA   21
#define PIN_OLED_SCL   22
#define OLED_ADDR      0x3C
#define OLED_WIDTH     128
#define OLED_HEIGHT    64

// ==================== Sensor Simulation ====================
// เปิด comment บรรทัดนี้เมื่อ sensor ไม่ได้เชื่อมต่อ เพื่อสุ่มค่าจำลอง
#define SIMULATE_SENSORS

// ช่วงค่าจำลอง DS18B20 (water temp °C)
#define SIM_WATER_TEMP_MIN  20.0f
#define SIM_WATER_TEMP_MAX  35.0f

// ช่วงค่าจำลอง XY-MD03 (air temp °C, humidity %)
#define SIM_AIR_TEMP_MIN    25.0f
#define SIM_AIR_TEMP_MAX    40.0f
#define SIM_HUMIDITY_MIN    40.0f
#define SIM_HUMIDITY_MAX    90.0f

// ==================== Timing ====================
#define TELEMETRY_INTERVAL_MS  10000   // ส่ง sensor data ทุก 5 วินาที
#define DISPLAY_INTERVAL_MS    2000    // อัปเดตหน้าจอทุก 2 วินาที
#define STATUS_INTERVAL_MS     30000   // ส่ง heartbeat ทุก 30 วินาที
#define RECONNECT_INTERVAL_MS  5000    // ลอง reconnect WiFi/MQTT ทุก 5 วินาที
