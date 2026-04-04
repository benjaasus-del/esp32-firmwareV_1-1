# SmartFarm ESP32 Firmware v1.0.0

Firmware สำหรับ ESP32 ที่ทำหน้าที่อ่านค่า Sensor, ควบคุม Relay และส่งข้อมูลผ่าน MQTT  
รองรับ Water Temperature, Air Temperature/Humidity, Water Overflow, Water Dry และ Relay Control  
เอกสารนี้ครอบคลุมทุกรายละเอียดเพื่อใช้เป็นแนวทางสร้าง **Django SmartFarm Dashboard**

---

## สารบัญ

1. [โครงสร้างโปรเจกต์](#1-โครงสร้างโปรเจกต์)
2. [Hardware & Wiring](#2-hardware--wiring)
3. [Sensor ที่ใช้งาน](#3-sensor-ที่ใช้งาน)
4. [Relay Control](#4-relay-control)
5. [WiFi & Boot Sequence](#5-wifi--boot-sequence)
6. [MQTT Protocol](#6-mqtt-protocol)
7. [MQTT Topics & Payload Schema](#7-mqtt-topics--payload-schema)
8. [OLED Display Layout](#8-oled-display-layout)
9. [Timing & Intervals](#9-timing--intervals)
10. [Sensor Simulation Mode](#10-sensor-simulation-mode)
11. [แก้ไข Config ก่อน Flash](#11-แก้ไข-config-ก่อน-flash)
12. [Django Dashboard — Architecture](#12-django-dashboard--architecture)
13. [Django Data Models](#13-django-data-models)
14. [Django MQTT Consumer](#14-django-mqtt-consumer)
15. [Django REST API Endpoints](#15-django-rest-api-endpoints)
16. [Django Channels (WebSocket)](#16-django-channels-websocket)
17. [Tech Stack แนะนำ](#17-tech-stack-แนะนำ)
18. [Environment Variables](#18-environment-variables)
19. [MQTT Quick Reference](#19-mqtt-quick-reference)
20. [การทดสอบด้วย MQTT Explorer](#20-การทดสอบด้วย-mqtt-explorer)

---

## 1. โครงสร้างโปรเจกต์

```
esp32-firmwareV_1/
├── platformio.ini          # PlatformIO build config, lib_deps
├── include/
│   ├── config.h            # ค่าคงที่ทั้งหมด (WiFi, MQTT, GPIO, Timing)
│   ├── sensors.h           # struct SensorData + function declarations
│   ├── mqtt_client.h       # MQTT function declarations
│   ├── relay_control.h     # Relay function declarations
│   └── display.h           # OLED function declarations
└── src/
    ├── main.cpp            # Setup + main loop
    ├── sensors.cpp         # DS18B20 + XY-MD03 Modbus RTU
    ├── mqtt_client.cpp     # PubSubClient + ArduinoJson
    ├── relay_control.cpp   # GPIO relay driver (Active LOW)
    └── display.cpp         # OLED SSD1306 128x64 driver
```

### Library Dependencies (platformio.ini)

| Library | Version | ใช้งาน |
|---|---|---|
| knolleary/PubSubClient | ^2.8 | MQTT client |
| bblanchon/ArduinoJson | ^7.3 | JSON serialize/deserialize |
| paulstoffregen/OneWire | ^2.3 | DS18B20 OneWire protocol |
| milesburton/DallasTemperature | ^3.11 | DS18B20 driver |
| 4-20ma/ModbusMaster | ^2.0 | Modbus RTU (XY-MD03) |
| adafruit/Adafruit SSD1306 | ^2.5 | OLED display driver |
| adafruit/Adafruit GFX Library | ^1.11 | OLED graphics primitives |

---

## 2. Hardware & Wiring

### GPIO Map

| GPIO | Signal | Device | รายละเอียด |
|---|---|---|---|
| GPIO 14 | OneWire Data | DS18B20 | Water Temperature |
| GPIO 1 (TX0) | RS485 TX | XY-MD03 via MAX13487 | Modbus RTU — Serial0 |
| GPIO 3 (RX0) | RS485 RX | XY-MD03 via MAX13487 | Modbus RTU — Serial0 |
| GPIO 17 | Relay 1 | Water Pump | Active LOW |
| GPIO 16 | Relay 2 | Fan / Ventilation | Active LOW |
| GPIO 4 | Relay 3 | Heater | Active LOW |
| GPIO 33 | Level Overflow | Water Overflow Sensor | Active LOW via opto isolate |
| GPIO 27 | Level Dry | Water Dry Sensor | Active LOW via opto isolate |
| GPIO 21 | I2C SDA | OLED SSD1306 | I2C address 0x3C |
| GPIO 22 | I2C SCL | OLED SSD1306 | I2C address 0x3C |

> **หมายเหตุ:** GPIO 1 และ 3 (Serial0) ถูก reconfigure สำหรับ Modbus RTU  
> → USB Serial (Debug print) ไม่สามารถใช้งานได้หลัง `sensorsInit()` เรียก

### Wiring Diagram (Text)

```
ESP32
├── GPIO14 ────────── DS18B20 Data (+ 4.7kΩ pull-up ไปยัง 3.3V)
│
├── GPIO1  (TX0) ──── MAX13487 DI
├── GPIO3  (RX0) ──── MAX13487 RO      ──── XY-MD03 RS485 A/B
│   (MAX13487 auto-direction: ไม่ต้องการ DE/RE pin)
│
├── GPIO17 ────────── Relay Module IN1 (Pump)
├── GPIO16 ────────── Relay Module IN2 (Fan)
├── GPIO4  ────────── Relay Module IN3 (Heater)
│   (Relay Module: Active LOW with optocoupler)
│
├── GPIO33 ────────── Water Overflow Sensor (Opto Isolate, Active LOW)
├── GPIO27 ────────── Water Dry Sensor (Opto Isolate, Active LOW)
│
├── GPIO21 (SDA) ──── OLED SSD1306 SDA
└── GPIO22 (SCL) ──── OLED SSD1306 SCL
    (I2C: built-in pull-up หรือ 4.7kΩ ไปยัง 3.3V)
```

---

## 3. Sensor ที่ใช้งาน

### 3.1 DS18B20 — อุณหภูมิน้ำ (Water Temperature)

| พารามิเตอร์ | ค่า |
|---|---|
| Protocol | OneWire บน GPIO 14 |
| Resolution | 11-bit (~375 ms conversion time) |
| ช่วงวัดได้ | -55°C ถึง +125°C |
| ทศนิยม | 1 ตำแหน่ง |
| ค่า invalid | `85.0°C` = power-on default (ยังไม่พร้อม), `-127°C` = ไม่มี sensor |
| Field ใน payload | `sensors.water_temp` (float, °C) |

### 3.2 XY-MD03 — อุณหภูมิอากาศ + ความชื้น

| พารามิเตอร์ | ค่า |
|---|---|
| Protocol | Modbus RTU via RS485 |
| IC แปลง | MAX13487 (auto-direction, ไม่ต้องการ DE pin) |
| Baud Rate | 9600 bps, 8N1 |
| Slave Address | 0x01 |
| Function Code | 04 (Read Input Registers) |

**Modbus Registers:**

| Register | Raw Value | การแปลง | Field |
|---|---|---|---|
| 0x0001 | Temperature × 10 | หาร 10 = °C | `sensors.air_temp` |
| 0x0002 | Humidity × 10 | หาร 10 = % | `sensors.air_humidity` |

### 3.3 Water Level Sensors — Overflow / Dry

| Sensor | GPIO | Logic | Field ใน payload | ความหมายเมื่อเป็น `true` |
|---|---|---|---|---|
| Water Overflow | 33 | Active LOW ผ่าน opto isolate | `sensors.water_overflow` | ระดับน้ำถึงจุดล้น |
| Water Dry | 27 | Active LOW ผ่าน opto isolate | `sensors.water_dry` | น้ำแห้ง / ต่ำกว่าจุดกำหนด |

- ค่า 2 field นี้ถูกอ่านทุกครั้งที่ `sensorsRead()` และถูกส่งใน Telemetry ทุกครั้ง
- เป็น boolean state จึงมี field ใน payload เสมอ ต่างจากค่า temperature/humidity ที่อาจหายไปเมื่อ sensor invalid

### 3.4 SensorData Struct (C++)

```cpp
struct SensorData {
    float waterTemp;         // DS18B20 (°C)
    float airTemp;           // XY-MD03 (°C)
    float airHumidity;       // XY-MD03 (%)
  bool  waterOverflow;     // true = น้ำล้น
  bool  waterDry;          // true = น้ำแห้ง
    bool  waterTempValid;    // false = sensor error / disconnected
    bool  airTempValid;      // false = Modbus read failed
    bool  airHumidityValid;  // false = Modbus read failed
};
```

> **สำคัญ:** field กลุ่ม temperature/humidity ใน `sensors` ของ MQTT payload **อาจไม่มี key** ถ้า sensor นั้น invalid  
> แต่ `sensors.water_overflow` และ `sensors.water_dry` จะถูกส่งเสมอเพราะเป็น digital input

---

## 4. Relay Control

| ชื่อ Relay | GPIO | หน้าที่ | MQTT Field |
|---|---|---|---|
| Relay 1 | 17 | Water Pump | `relay1_pump` |
| Relay 2 | 16 | Fan / Ventilation | `relay2_fan` |
| Relay 3 | 4 | Heater | `relay3_heater` |

**Logic:**
- `RELAY_ACTIVE_HIGH = false` → relay module ทั่วไปที่มี optocoupler
- `digitalWrite(LOW)` = relay **เปิด** (ทำงาน)
- `digitalWrite(HIGH)` = relay **ปิด** (หยุดทำงาน)
- ใน MQTT payload: `true` = ON (ทำงาน), `false` = OFF (หยุด)
- ทุก relay ปิดอัตโนมัติตอน boot

---

## 5. WiFi & Boot Sequence

**Boot Order:**
```
relayInit()     → ปิด relay ทุกช่อง (safe state)
displayInit()   → เริ่ม OLED, แสดง "Booting..."
sensorsInit()   → เริ่ม DS18B20 + Modbus Serial0
wifiConnect()   → เชื่อมต่อ WiFi (timeout 15s)
displayShowIP() → แสดง IP บนจอ 2 วินาที
mqttSetup()     → ตั้งค่า broker, callback, buffer
mqttConnect()   → connect + subscribe + publish "online"
```

**WiFi Parameters:**
- Mode: `WIFI_STA` (Station)
- Timeout per attempt: 15,000 ms
- Reconnect interval: 5,000 ms (เมื่อหลุด)

---

## 6. MQTT Protocol

| พารามิเตอร์ | ค่า |
|---|---|
| Broker | `broker.hivemq.com` |
| Port | `1883` (TCP, ไม่มี TLS) |
| Client ID | `BOARD_ID` จาก `include/config.h` |
| Username / Password | ไม่มี (Public Free Broker) |
| Keep Alive | 60 วินาที |
| Buffer Size | 512 bytes |
| QoS Telemetry publish | 0 (at most once) |
| QoS Control subscribe | 1 (at least once) |
| QoS Status publish | 1 + retain = true |

### QoS (Quality of Service) — ความหมายและการใช้งาน

MQTT กำหนด QoS 3 ระดับ แต่ละระดับควบคุม **การรับประกันการส่งข้อความ** ระหว่าง Publisher ↔ Broker และ Broker ↔ Subscriber

#### QoS 0 — At Most Once (ส่งได้มากที่สุด 1 ครั้ง)

```
Publisher ──► Broker ──► Subscriber
  (ส่งแล้วลืม)   (ส่งแล้วลืม)
```

- ไม่มี Acknowledgment (ACK) กลับมา
- ข้อความ **อาจหายได้** หากเครือข่ายขัดข้องหรือผู้รับออฟไลน์
- Broker **ไม่เก็บ** ข้อความไว้รอ Subscriber
- Overhead ต่ำที่สุด — เหมาะกับข้อมูลที่ส่งบ่อยและรับค่าใหม่ได้อยู่แล้ว

**ใช้กับโปรเจกต์นี้:** `smartfarm/{id}/telemetry`
→ ส่งทุก 5 วินาที ถ้าหายไป 1 รอบก็ไม่เป็นไร รอบถัดไปมาแทนได้

---

#### QoS 1 — At Least Once (ส่งอย่างน้อย 1 ครั้ง)

```
Publisher ──► Broker  →  PUBACK ──► Publisher
                 │
             Subscriber  →  (ไม่มี ACK กลับถึง Broker)
```

- Publisher ส่งซ้ำจนกว่าจะได้รับ **PUBACK** จาก Broker
- ข้อความ **ถูกส่งถึงผู้รับอย่างแน่นอน** แต่อาจซ้ำกันได้ (duplicate)
- Subscriber ต้องออกแบบให้รับมือกับข้อความซ้ำ (Idempotent)
- Overhead ปานกลาง — มี packet ไปกลับ 1 รอบ (PUBLISH + PUBACK)

**ใช้กับโปรเจกต์นี้:**
- `smartfarm/{id}/status` (publish) → ต้องมั่นใจว่า Dashboard รู้ว่า board online/offline
- `smartfarm/{id}/control` (subscribe) → คำสั่ง relay ต้องถึง ESP32 ทุกครั้ง ห้ามหาย

---

#### QoS 2 — Exactly Once (ส่งครั้งเดียวแน่นอน)

```
Publisher ──► Broker  →  PUBREC
Publisher  ←  PUBREL  ←  Broker
Publisher ──► PUBCOMP ──► Broker ──► Subscriber
```

- 4-way handshake: PUBLISH → PUBREC → PUBREL → PUBCOMP
- ข้อความ **ถึงผู้รับครั้งเดียวเท่านั้น** ไม่ซ้ำ, ไม่หาย
- Overhead สูงที่สุด — ใช้ packet 4 ชุด
- เหมาะกับการเงิน, คำสั่งที่ห้ามซ้ำ เช่น เปิด/ปิดวาล์วในระบบที่ซ้ำกันแล้วเสียหาย

**ไม่ใช้ในโปรเจกต์นี้** — QoS 1 เพียงพอแล้วเพราะ relay command ถูกออกแบบให้รับซ้ำได้ (idempotent)

---

#### สรุปเปรียบเทียบ QoS

| ระดับ | ชื่อ | การรับประกัน | ซ้ำได้? | Packet | ใช้เมื่อ |
|---|---|---|---|---|---|
| **0** | At Most Once | ไม่รับประกัน | ไม่ซ้ำ | 1 | ข้อมูล Sensor ที่ส่งบ่อย |
| **1** | At Least Once | รับประกัน | อาจซ้ำ | 2 | Status, คำสั่งสำคัญ |
| **2** | Exactly Once | รับประกัน | ไม่ซ้ำ | 4 | การเงิน, คำสั่งที่ห้ามซ้ำ |

> **หมายเหตุ:** QoS ที่ระบุตอน **Publish** และ **Subscribe** อาจต่างกัน  
> Broker จะส่งให้ Subscriber ด้วย QoS ที่ **ต่ำกว่า** ระหว่าง QoS ของ Publish กับ QoS ที่ Subscriber ขอ

### Last Will and Testament (LWT)

เมื่อ board หลุดโดยไม่ตั้งใจ (ไฟดับ, reset, หลุด WiFi) broker จะ publish อัตโนมัติ:

**Topic:** `smartfarm/{BOARD_ID}/status`

```json
{
  "board_id": "ESP32-FARM-001-NATTAPHOL-PALM",
  "status": "offline",
  "timestamp": 123456
}
```

> LWT payload ไม่มี field `ip`, `firmware`, `uptime` — ต้องตรวจสอบใน Django ด้วย

---

## 7. MQTT Topics & Payload Schema

### ภาพรวมการไหลของข้อมูล

```
Backend / Dashboard
    │
    ├─── subscribe ──► smartfarm/+/telemetry   (รับค่า sensor + water level + relay state)
    ├─── subscribe ──► smartfarm/+/status      (รับสถานะ online/offline)
    └─── publish  ──► smartfarm/{id}/control   (ส่งคำสั่งควบคุม)

ESP32
    ├─── publish  ──► telemetry  (ทุก 5 วิ + ทันทีหลัง relay_control)
    ├─── publish  ──► status     (ทุก 30 วิ + ตอน connect/disconnect + LWT)
    └─── subscribe ──► control   (รอรับคำสั่งตลอดเวลา)
```

### Topic Pattern

```
smartfarm/{board_id}/telemetry   ← ESP32 → Dashboard  (publish ทุก 5s)
smartfarm/{board_id}/status      ← ESP32 → Dashboard  (publish ทุก 30s, retained)
smartfarm/{board_id}/control     ← Dashboard → ESP32  (ESP32 subscribe)
```

ตัวอย่างจาก config ปัจจุบัน (`BOARD_ID = "ESP32-FARM-001-NATTAPHOL-PALM"`):
```
smartfarm/ESP32-FARM-001-NATTAPHOL-PALM/telemetry
smartfarm/ESP32-FARM-001-NATTAPHOL-PALM/status
smartfarm/ESP32-FARM-001-NATTAPHOL-PALM/control
```

Subscribe ทุก board พร้อมกัน (Django):
```
smartfarm/+/telemetry
smartfarm/+/status
```

ตัวอย่างการ subscribe/publish แบบเจาะจง:
```
Subscribe telemetry ของบอร์ดเดียว:
smartfarm/ESP32-FARM-001-NATTAPHOL-PALM/telemetry

Subscribe status ของทุกบอร์ด:
smartfarm/+/status

Publish คำสั่งไปยังบอร์ดเดียว:
smartfarm/ESP32-FARM-001-NATTAPHOL-PALM/control
```

### สรุป Topics ทั้งหมด

| Topic | ทิศทาง | QoS | Retain | Trigger |
|---|---|---|---|---|
| `smartfarm/{id}/telemetry` | ESP32 → Dashboard | 0 | false | ทุก 5s + หลัง relay_control |
| `smartfarm/{id}/status` | ESP32 → Dashboard | 1 | **true** | ทุก 30s + connect + LWT |
| `smartfarm/{id}/control` | Dashboard → ESP32 | 1 | false | on-demand |

---

### Topic 1: Telemetry

| พารามิเตอร์ | ค่า |
|---|---|
| Topic | `smartfarm/{board_id}/telemetry` |
| Direction | ESP32 → Dashboard |
| Trigger | ทุก 5,000 ms **และ** ทันทีหลังรับคำสั่ง `relay_control` |
| QoS | 0 |
| Retain | false |

**Payload:**

```json
{
  "board_id": "ESP32-FARM-001-NATTAPHOL-PALM",
  "timestamp": 123456,
  "rssi": -65,
  "sensors": {
    "water_temp": 28.5,
    "air_temp": 32.1,
    "air_humidity": 65.0,
    "water_overflow": false,
    "water_dry": false
  },
  "relays": {
    "relay1_pump": false,
    "relay2_fan": true,
    "relay3_heater": false
  }
}
```

**Field Reference:**

| Field | Type | Unit | หมายเหตุ |
|---|---|---|---|
| `board_id` | string | - | ตรงกับ BOARD_ID ใน config.h |
| `timestamp` | integer | ms | `millis()` นับจาก boot ไม่ใช่ Unix epoch — ใช้ server time แทน |
| `rssi` | integer | dBm | WiFi signal strength (ค่าลบ ยิ่งใกล้ 0 ยิ่งแรง) |
| `sensors.water_temp` | float | °C | อาจ **ไม่มี key** ถ้า DS18B20 error |
| `sensors.air_temp` | float | °C | อาจ **ไม่มี key** ถ้า Modbus error |
| `sensors.air_humidity` | float | % | อาจ **ไม่มี key** ถ้า Modbus error |
| `sensors.water_overflow` | boolean | - | `true` = น้ำล้น |
| `sensors.water_dry` | boolean | - | `true` = น้ำแห้ง/น้ำต่ำ |
| `relays.relay1_pump` | boolean | - | true = ปั๊มทำงาน |
| `relays.relay2_fan` | boolean | - | true = พัดลมทำงาน |
| `relays.relay3_heater` | boolean | - | true = ฮีตเตอร์ทำงาน |

**ตัวอย่าง Telemetry เมื่อระดับน้ำผิดปกติ:**

```json
{
  "board_id": "ESP32-FARM-001-NATTAPHOL-PALM",
  "timestamp": 128991,
  "rssi": -58,
  "sensors": {
    "water_temp": 27.8,
    "air_temp": 31.4,
    "air_humidity": 68.2,
    "water_overflow": true,
    "water_dry": false
  },
  "relays": {
    "relay1_pump": false,
    "relay2_fan": true,
    "relay3_heater": false
  }
}
```

---

### Topic 2: Status

| พารามิเตอร์ | ค่า |
|---|---|
| Topic | `smartfarm/{board_id}/status` |
| Direction | ESP32 → Dashboard |
| Trigger | ทุก 30,000 ms + ทันทีที่ connect + ก่อน reboot + LWT (broker ส่งอัตโนมัติเมื่อหลุด) |
| QoS | 1 |
| Retain | **true** (subscriber ใหม่จะได้รับค่าล่าสุดทันที) |

**Payload (Online):**

```json
{
  "board_id": "ESP32-FARM-001-NATTAPHOL-PALM",
  "status": "online",
  "ip": "192.168.1.100",
  "firmware": "1.0.0",
  "uptime": 3600,
  "timestamp": 3600000
}
```

**Payload (Offline — LWT หรือ reboot):**

```json
{
  "board_id": "ESP32-FARM-001-NATTAPHOL-PALM",
  "status": "offline",
  "timestamp": 3600123
}
```

**Field Reference:**

| Field | Type | หมายเหตุ |
|---|---|---|
| `board_id` | string | Device identifier |
| `status` | string | `"online"` หรือ `"offline"` |
| `ip` | string | IP ใน LAN — **ไม่มีใน LWT payload** |
| `firmware` | string | Firmware version — **ไม่มีใน LWT payload** |
| `uptime` | integer | วินาที นับจาก boot — **ไม่มีใน LWT payload** |
| `timestamp` | integer | `millis()` ms |

---

### Topic 3: Control

| พารามิเตอร์ | ค่า |
|---|---|
| Topic | `smartfarm/{board_id}/control` |
| Direction | Dashboard → ESP32 |
| QoS | 1 |
| Retain | false |

**Command: relay_control**

```json
{
  "command": "relay_control",
  "relays": {
    "relay1_pump": true,
    "relay2_fan": false,
    "relay3_heater": false
  }
}
```

- ส่งเฉพาะ relay ที่ต้องการเปลี่ยนได้ (partial update — ไม่จำเป็นต้องส่งครบ 3 ตัว)
- ESP32 ตอบกลับด้วย **Telemetry ทันที** พร้อม relay state ล่าสุด (ไม่รอรอบปกติ 5 วินาที)
- `true` = relay ทำงาน (ON), `false` = relay หยุด (OFF)

**Command: reboot**

```json
{
  "command": "reboot"
}
```

- ESP32 publish `status: "offline"` ก่อน restart (flush ออก network 200ms)
- board จะ reconnect และ publish `status: "online"` ใหม่หลัง boot เสร็จ (~3 วินาที)

**Command: ping**

```json
{
  "command": "ping"
}
```

- ESP32 ตอบกลับด้วย `status: "online"` ผ่าน Topic Status ทันที
- ใช้ตรวจสอบว่า board ยังออนไลน์และรับคำสั่งได้

---

## 8. OLED Display Layout

จอ SSD1306 ขนาด 128×64 pixels, I2C 0x3C

```
┌────────────────────────────────────┐
│ SmartFarm          W:OK  M:OK      │  y=0   (status header, font1)
├────────────────────────────────────┤  y=9   (horizontal divider)
│ H2O  28.5C      OF:0 DR:0          │  y=11  (water temp + level state)
│ Air:32.1C       Hum:65%            │  y=29  (air temp + humidity, font1)
├────────────────────────────────────┤  y=38  (horizontal divider)
│  ┌──────┐  ┌──────┐  ┌──────┐     │
│  │ Pump │  │ Fan  │  │ Heat │     │  y=47  (relay label)
│  │  ON  │  │ OFF  │  │ OFF  │     │  y=56  (relay state)
│  └──────┘  └──────┘  └──────┘     │  x=1,44,87 (แต่ละ box กว้าง 40px)
└────────────────────────────────────┘
```

| Element | รายละเอียด |
|---|---|
| `W:OK` / `W:--` | WiFi connected / disconnected |
| `M:OK` / `M:--` | MQTT connected / disconnected |
| `OF:1` / `DR:1` | Overflow / Dry sensor active |
| Relay **ON** | filled white box + black text |
| Relay **OFF** | empty outline + white text |
| `---C` / `--%` | แสดงเมื่อ sensor invalid |

---

## 9. Timing & Intervals

| Event | Interval | รายละเอียด |
|---|---|---|
| `sensorsRead()` + `mqttPublishTelemetry()` | 5,000 ms | อ่าน sensor + publish MQTT |
| `displayUpdate()` | 2,000 ms | อัปเดต OLED |
| `mqttPublishStatus("online")` | 30,000 ms | Heartbeat |
| WiFi reconnect | 5,000 ms | เมื่อ WiFi หลุด |
| MQTT reconnect | 5,000 ms | เมื่อ MQTT หลุด (WiFi ยังอยู่) |

> `delay(400)` ใน `sensorsRead()` รอ DS18B20 conversion (11-bit = 375ms)  
> → ทำให้ loop หยุด 400ms ทุกรอบที่อ่าน sensor (ทุก 5 วินาที)

---

## 10. Sensor Simulation Mode

เปิดใน `config.h`:
```c
#define SIMULATE_SENSORS
```

เมื่อเปิด: ถ้า sensor อ่านค่าไม่ได้ จะสุ่มค่าในช่วงที่กำหนดแทน

| Sensor | ช่วงค่าจำลอง | Field |
|---|---|---|
| DS18B20 Water Temp | 20.0 – 35.0°C | `water_temp` |
| XY-MD03 Air Temp | 25.0 – 40.0°C | `air_temp` |
| XY-MD03 Humidity | 40.0 – 90.0% | `air_humidity` |

- ใช้ `esp_random()` สุ่มค่า ทศนิยม 1 ตำแหน่ง
- Dashboard ไม่ต้องแยก simulated / real data — format payload เหมือนกันทุกอย่าง
- Water Overflow / Water Dry ไม่ถูกจำลอง ยังคงอ่านจาก digital input จริงเสมอ

---

## 11. แก้ไข Config ก่อน Flash

แก้ไขค่าในไฟล์ `include/config.h` ก่อน build และ flash:

```c
// Board Identity
#define BOARD_ID      "ESP32-FARM-001-NATTAPHOL-PALM"   // ต้องตรงกับ boardId ใน Django

// WiFi
#define WIFI_SSID       "your-wifi-ssid"
#define WIFI_PASSWORD   "your-wifi-password"

// MQTT Broker (ถ้าใช้ Private broker ให้เปลี่ยน)
#define MQTT_BROKER     "broker.hivemq.com"
#define MQTT_PORT       1883
#define MQTT_USER       ""
#define MQTT_PASS       ""

// Level sensors
#define PIN_LEVEL_OVERFLOW 33
#define PIN_LEVEL_DRY      27
#define LEVEL_SENSOR_ACTIVE_HIGH false

// Sensor Simulation (comment ออกเมื่อ sensor พร้อม)
#define SIMULATE_SENSORS
```

---

## 12. Django Dashboard — Architecture

```
[ESP32 Firmware]
      |
      |  MQTT publish
      |  (telemetry / status)
      v
[HiveMQ Public Broker]
[broker.hivemq.com:1883]
      |
      |  MQTT subscribe
      v
[Django Backend]
  |
  +-- [MQTT Subscriber Thread]  (paho-mqtt, management command)
  |     |-- parse JSON payload
  |     |-- save to PostgreSQL
  |     +-- push to Channel Layer (Redis)
  |
  +-- [Django REST Framework]
  |     |-- GET  /api/boards/
  |     |-- GET  /api/boards/{id}/
  |     |-- GET  /api/boards/{id}/telemetry/
  |     |-- GET  /api/boards/{id}/telemetry/latest/
  |     |-- POST /api/boards/{id}/control/relay/
  |     |-- POST /api/boards/{id}/control/reboot/
  |     +-- POST /api/boards/{id}/control/ping/
  |
  +-- [Django Channels + Redis]
  |     +-- ws://host/ws/boards/{board_id}/  (real-time push)
  |
  +-- [Django Templates + HTMX / Chart.js]
        +-- Board list, Board detail, Sensor charts, Relay controls
```

---

## 13. Django Data Models

```python
# boards/models.py

from django.db import models
from django.utils import timezone


class Board(models.Model):
    """ESP32 device แต่ละตัว"""
    board_id   = models.CharField(max_length=64, unique=True)    # "ESP32-FARM-001"
    name       = models.CharField(max_length=128, blank=True)    # ชื่อที่ตั้งเอง
    firmware   = models.CharField(max_length=32, blank=True)     # "1.0.0"
    ip_address = models.GenericIPAddressField(null=True, blank=True)
    is_online  = models.BooleanField(default=False)
    last_seen  = models.DateTimeField(null=True, blank=True)
    uptime     = models.PositiveIntegerField(default=0)          # seconds
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        ordering = ['board_id']

    def __str__(self):
        return self.board_id


class TelemetryLog(models.Model):
    """บันทึก sensor + relay state ทุกครั้งที่รับ MQTT telemetry"""
    board        = models.ForeignKey(Board, on_delete=models.CASCADE,
                                     related_name='telemetry_logs')
    received_at  = models.DateTimeField(default=timezone.now, db_index=True)

    # Sensors (nullable เมื่อ sensor error)
    water_temp    = models.FloatField(null=True, blank=True)    # °C
    air_temp      = models.FloatField(null=True, blank=True)    # °C
    air_humidity  = models.FloatField(null=True, blank=True)    # %
    water_overflow = models.BooleanField(default=False)
    water_dry      = models.BooleanField(default=False)
    rssi          = models.IntegerField(null=True, blank=True)  # dBm

    # Relay states
    relay1_pump   = models.BooleanField(default=False)
    relay2_fan    = models.BooleanField(default=False)
    relay3_heater = models.BooleanField(default=False)

    class Meta:
        ordering = ['-received_at']
        indexes  = [models.Index(fields=['board', '-received_at'])]


class RelayCommand(models.Model):
    """บันทึกคำสั่งที่ Dashboard ส่งไปยัง ESP32"""
    board    = models.ForeignKey(Board, on_delete=models.CASCADE,
                                  related_name='relay_commands')
    sent_at  = models.DateTimeField(auto_now_add=True)
    command  = models.CharField(max_length=32)    # "relay_control", "reboot", "ping"
    payload  = models.JSONField()                 # full JSON payload ที่ส่งออกไป
    sent_by  = models.CharField(max_length=128, blank=True)  # username / "system"

    class Meta:
        ordering = ['-sent_at']
```

---

## 14. Django MQTT Consumer

```python
# boards/mqtt_subscriber.py
# pip install paho-mqtt

import json
import threading
import paho.mqtt.client as mqtt
from django.utils import timezone

BROKER_HOST      = "broker.hivemq.com"
BROKER_PORT      = 1883
TOPIC_TELEMETRY  = "smartfarm/+/telemetry"
TOPIC_STATUS     = "smartfarm/+/status"


def on_connect(client, userdata, flags, rc):
    client.subscribe(TOPIC_TELEMETRY, qos=0)
    client.subscribe(TOPIC_STATUS, qos=1)


def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
    except (json.JSONDecodeError, UnicodeDecodeError):
        return

    parts = msg.topic.split("/")
    # parts = ["smartfarm", "{board_id}", "{type}"]
    if len(parts) != 3:
        return

    board_id   = parts[1]
    topic_type = parts[2]

    # import ใน function เพื่อหลีก circular import เมื่อ start ก่อน Django ready
    from boards.models import Board, TelemetryLog

    board, _ = Board.objects.get_or_create(board_id=board_id)

    if topic_type == "telemetry":
        sensors = payload.get("sensors", {})
        relays  = payload.get("relays", {})

        log = TelemetryLog.objects.create(
            board         = board,
            received_at   = timezone.now(),   # ใช้ server time (ESP32 timestamp เป็น millis)
            water_temp    = sensors.get("water_temp"),
            air_temp      = sensors.get("air_temp"),
            air_humidity  = sensors.get("air_humidity"),
            water_overflow = sensors.get("water_overflow", False),
            water_dry      = sensors.get("water_dry", False),
            rssi          = payload.get("rssi"),
            relay1_pump   = relays.get("relay1_pump", False),
            relay2_fan    = relays.get("relay2_fan", False),
            relay3_heater = relays.get("relay3_heater", False),
        )

        board.last_seen = timezone.now()
        board.is_online = True
        board.save(update_fields=["last_seen", "is_online"])

        # Push real-time ไปยัง WebSocket (ถ้าใช้ Django Channels)
        _push_to_websocket(board_id, "telemetry", {
            "type":          "telemetry",
            "board_id":      board_id,
            "received_at":   log.received_at.isoformat(),
            "water_temp":    log.water_temp,
            "air_temp":      log.air_temp,
            "air_humidity":  log.air_humidity,
            "water_overflow": log.water_overflow,
            "water_dry":      log.water_dry,
            "rssi":          log.rssi,
            "relay1_pump":   log.relay1_pump,
            "relay2_fan":    log.relay2_fan,
            "relay3_heater": log.relay3_heater,
        })

    elif topic_type == "status":
        status = payload.get("status", "offline")
        board.is_online  = (status == "online")
        board.last_seen  = timezone.now()
        board.firmware   = payload.get("firmware") or board.firmware
        board.ip_address = payload.get("ip") or board.ip_address
        board.uptime     = payload.get("uptime", board.uptime)
        board.save(update_fields=["is_online", "last_seen", "firmware",
                                  "ip_address", "uptime"])

        _push_to_websocket(board_id, "status", {
            "type":      "status",
            "board_id":  board_id,
            "is_online": board.is_online,
            "uptime":    board.uptime,
        })


def _push_to_websocket(board_id: str, event_type: str, data: dict):
    """ส่งข้อมูลไปยัง WebSocket group ผ่าน Django Channels"""
    try:
        from asgiref.sync import async_to_sync
        from channels.layers import get_channel_layer
        channel_layer = get_channel_layer()
        if channel_layer:
            async_to_sync(channel_layer.group_send)(
                f"board_{board_id}",
                {"type": f"board.{event_type}", "data": data},
            )
    except Exception:
        pass  # Channels เป็น optional — ถ้าไม่ได้ใช้ก็ข้ามไป


def publish_control(board_id: str, payload: dict):
    """ส่งคำสั่งควบคุมไปยัง ESP32 ผ่าน MQTT"""
    topic = f"smartfarm/{board_id}/control"
    pub   = mqtt.Client()
    pub.connect(BROKER_HOST, BROKER_PORT)
    pub.publish(topic, json.dumps(payload), qos=1)
    pub.disconnect()


def start_mqtt_subscriber():
    """เริ่ม MQTT subscriber ใน background thread"""
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(BROKER_HOST, BROKER_PORT, keepalive=60)
    t = threading.Thread(target=client.loop_forever, daemon=True)
    t.start()
    return client
```

### เรียกใช้ใน Django Apps

```python
# boards/apps.py
from django.apps import AppConfig

class BoardsConfig(AppConfig):
    name = 'boards'

    def ready(self):
        from boards.mqtt_subscriber import start_mqtt_subscriber
        start_mqtt_subscriber()
```

---

## 15. Django REST API Endpoints

### Boards

```
GET  /api/boards/
     Response: รายการ board ทั้งหมด + is_online + last_seen + ค่า sensor ล่าสุด

GET  /api/boards/{board_id}/
     Response: ข้อมูล board + telemetry ล่าสุด 1 รายการ

GET  /api/boards/{board_id}/telemetry/
     Query params: ?limit=100 &from=2024-01-01T00:00:00Z &to=2024-01-02T00:00:00Z
     Response:
     {
       "count": 1440,
       "results": [
         {
           "received_at": "2024-01-01T12:00:00Z",
           "water_temp": 28.5,
           "air_temp": 32.1,
           "air_humidity": 65.0,
           "water_overflow": false,
           "water_dry": false,
           "rssi": -65,
           "relay1_pump": false,
           "relay2_fan": true,
           "relay3_heater": false
         },
         ...
       ]
     }

GET  /api/boards/{board_id}/telemetry/latest/
     Response: ค่าล่าสุด 1 รายการ
```

### Control

```
POST /api/boards/{board_id}/control/relay/
     Body:
     {
       "relay1_pump": true,
       "relay2_fan": false,
       "relay3_heater": false
     }
     Action: publish MQTT control payload + บันทึก RelayCommand

POST /api/boards/{board_id}/control/reboot/
     Action: publish {"command": "reboot"}

POST /api/boards/{board_id}/control/ping/
     Action: publish {"command": "ping"}
```

### ตัวอย่าง View (Control Relay)

```python
# boards/views.py
from rest_framework.decorators import api_view
from rest_framework.response import Response
from boards.models import Board, RelayCommand
from boards.mqtt_subscriber import publish_control

@api_view(["POST"])
def control_relay(request, board_id):
    board = Board.objects.get(board_id=board_id)

    relays = {
        k: v for k, v in request.data.items()
        if k in ("relay1_pump", "relay2_fan", "relay3_heater")
        and isinstance(v, bool)
    }

    if not relays:
        return Response({"error": "no valid relay fields"}, status=400)

    payload = {"command": "relay_control", "relays": relays}

    publish_control(board_id, payload)

    RelayCommand.objects.create(
        board   = board,
        command = "relay_control",
        payload = payload,
        sent_by = request.user.username if request.user.is_authenticated else "api",
    )

    return Response({"status": "sent", "payload": payload})
```

---

## 16. Django Channels (WebSocket)

```python
# boards/consumers.py
# pip install channels channels-redis

import json
from channels.generic.websocket import AsyncWebsocketConsumer


class BoardConsumer(AsyncWebsocketConsumer):

    async def connect(self):
        self.board_id   = self.scope["url_route"]["kwargs"]["board_id"]
        self.group_name = f"board_{self.board_id}"
        await self.channel_layer.group_add(self.group_name, self.channel_name)
        await self.accept()

    async def disconnect(self, close_code):
        await self.channel_layer.group_discard(self.group_name, self.channel_name)

    async def board_telemetry(self, event):
        """รับ telemetry จาก MQTT subscriber แล้ว push ไปยัง browser"""
        await self.send(text_data=json.dumps(event["data"]))

    async def board_status(self, event):
        """รับ status update แล้ว push ไปยัง browser"""
        await self.send(text_data=json.dumps(event["data"]))
```

```python
# routing.py
from django.urls import re_path
from boards.consumers import BoardConsumer

websocket_urlpatterns = [
    re_path(r"ws/boards/(?P<board_id>[^/]+)/$", BoardConsumer.as_asgi()),
]
```

**WebSocket URL:** `ws://localhost:8000/ws/boards/ESP32-FARM-001-NATTAPHOL-PALM/`

**Messages ที่ Browser จะได้รับ:**

```json
// Telemetry (ทุก 5 วินาที)
{
  "type": "telemetry",
  "board_id": "ESP32-FARM-001-NATTAPHOL-PALM",
  "received_at": "2024-01-01T12:00:00Z",
  "water_temp": 28.5,
  "air_temp": 32.1,
  "air_humidity": 65.0,
  "water_overflow": false,
  "water_dry": false,
  "rssi": -65,
  "relay1_pump": false,
  "relay2_fan": true,
  "relay3_heater": false
}

// Status (เมื่อ board online/offline)
{
  "type": "status",
  "board_id": "ESP32-FARM-001-NATTAPHOL-PALM",
  "is_online": true,
  "uptime": 3600
}
```

---

## 17. Tech Stack แนะนำ

### Backend

```
Python 3.11+
Django 5.x
Django REST Framework       ← REST API
Django Channels             ← WebSocket real-time
channels-redis              ← Channel layer
paho-mqtt                   ← MQTT subscriber
PostgreSQL                  ← production database
SQLite                      ← development database
Redis                       ← Channel layer + cache
Celery (optional)           ← scheduled tasks, alert notifications
```

### Frontend (Option A — Django Template + HTMX)

```
Django Templates + Bootstrap 5
HTMX                        ← real-time update โดยไม่ต้องใช้ React
Chart.js หรือ ApexCharts    ← sensor history graphs
Alpine.js                   ← interactive UI components
```

### Frontend (Option B — SPA)

```
React หรือ Vue.js + Tailwind CSS
WebSocket client (native หรือ socket.io)
axios                       ← REST API calls
Recharts หรือ ECharts        ← charts
```

---

## 18. Environment Variables

```env
# .env สำหรับ Django Dashboard

# Django
SECRET_KEY=your-django-secret-key-here
DEBUG=True
ALLOWED_HOSTS=localhost,127.0.0.1

# Database
DATABASE_URL=sqlite:///db.sqlite3
# DATABASE_URL=postgresql://user:password@localhost:5432/smartfarm_db

# MQTT Broker
MQTT_BROKER_HOST=broker.hivemq.com
MQTT_BROKER_PORT=1883
MQTT_USERNAME=
MQTT_PASSWORD=

# Redis (สำหรับ Django Channels)
REDIS_URL=redis://localhost:6379/0

# Default Board (ถ้ามี board เดียว)
DEFAULT_BOARD_ID=ESP32-FARM-001-NATTAPHOL-PALM
```

---

## 19. MQTT Quick Reference

> สรุปรวดเร็วสำหรับนำไปใช้งาน

### Topics Summary

| Topic | Direction | QoS | Retain | Interval |
|---|---|---|---|---|
| `smartfarm/{id}/telemetry` | ESP32 → Dashboard | 0 | false | 5s |
| `smartfarm/{id}/status` | ESP32 → Dashboard | 1 | **true** | 30s |
| `smartfarm/{id}/control` | Dashboard → ESP32 | 1 | false | on-demand |

### Full Payload Reference

```
─── PUBLISH: smartfarm/ESP32-FARM-001-NATTAPHOL-PALM/telemetry ────
{
  "board_id":  "ESP32-FARM-001-NATTAPHOL-PALM",
  "timestamp": <millis: integer>,
  "rssi":      <dBm: integer>,
  "sensors": {
    "water_temp":   <float °C>,      // optional — ไม่มีถ้า sensor error
    "air_temp":     <float °C>,      // optional — ไม่มีถ้า sensor error
    "air_humidity": <float %>,       // optional — ไม่มีถ้า sensor error
    "water_overflow": <bool>,        // true = น้ำล้น
    "water_dry":      <bool>         // true = น้ำแห้ง/น้ำต่ำ
  },
  "relays": {
    "relay1_pump":   <bool>,         // Water Pump
    "relay2_fan":    <bool>,         // Fan
    "relay3_heater": <bool>          // Heater
  }
}

─── PUBLISH: smartfarm/ESP32-FARM-001-NATTAPHOL-PALM/status (retain=true) ────────
{
  "board_id":  "ESP32-FARM-001-NATTAPHOL-PALM",
  "status":    "online" | "offline",
  "ip":        "<ip-address>",       // ไม่มีใน LWT
  "firmware":  "1.0.0",             // ไม่มีใน LWT
  "uptime":    <seconds: integer>,   // ไม่มีใน LWT
  "timestamp": <millis: integer>
}

─── SUBSCRIBE: smartfarm/ESP32-FARM-001-NATTAPHOL-PALM/control ────
// สั่ง relay
{
  "command": "relay_control",
  "relays": {
    "relay1_pump":   <bool>,
    "relay2_fan":    <bool>,
    "relay3_heater": <bool>
  }
}

// สั่ง reboot
{ "command": "reboot" }

// ตรวจสอบ online
{ "command": "ping" }
```

### Python Parse Telemetry (ตัวอย่าง)

```python
import json

def parse_telemetry(raw_payload: bytes) -> dict:
    data    = json.loads(raw_payload)
    sensors = data.get("sensors", {})
    relays  = data.get("relays", {})
    return {
        "board_id":      data["board_id"],
        "rssi":          data.get("rssi"),
        "water_temp":    sensors.get("water_temp"),    # None ถ้า sensor error
        "air_temp":      sensors.get("air_temp"),
        "air_humidity":  sensors.get("air_humidity"),
        "water_overflow": sensors.get("water_overflow", False),
        "water_dry":      sensors.get("water_dry", False),
        "relay1_pump":   relays.get("relay1_pump", False),
        "relay2_fan":    relays.get("relay2_fan", False),
        "relay3_heater": relays.get("relay3_heater", False),
    }
```

---

## 20. การทดสอบด้วย MQTT Explorer

MQTT Explorer เป็น GUI tool สำหรับ monitor และ publish MQTT messages ได้ง่าย เหมาะสำหรับทดสอบ firmware โดยไม่ต้องรัน Django

### ติดตั้ง MQTT Explorer

ดาวน์โหลดได้จาก [mqtt-explorer.com](http://mqtt-explorer.com) รองรับ Windows / macOS / Linux

---

### เชื่อมต่อกับ HiveMQ Public Broker

1. เปิด MQTT Explorer
2. คลิก **+** เพื่อเพิ่ม Connection ใหม่
3. กรอกข้อมูลดังนี้:

| Field | ค่า |
|---|---|
| Name | `HiveMQ Public` |
| Protocol | `mqtt://` |
| Host | `broker.hivemq.com` |
| Port | `1883` |
| Username | (ว่าง) |
| Password | (ว่าง) |

4. คลิก **CONNECT**

> ถ้าเชื่อมต่อสำเร็จ จะเห็น topic tree ด้านซ้ายเริ่มเติมข้อมูล

---

### Monitor Telemetry (ดูข้อมูล Sensor + Relay)

1. ในช่อง **Topic** (บน toolbar) พิมพ์:
   ```
   smartfarm/#
   ```
2. คลิก **Subscribe** (หรือ Enter)
3. เปิด ESP32 → ภายในประมาณ 5 วินาทีจะเห็น message ปรากฏใต้:
   ```
   smartfarm/
     └── ESP32-FARM-001-NATTAPHOL-PALM/
       ├── telemetry    ← sensor + water level + relay state (ทุก 5s)
       └── status       ← online/offline (ทุก 30s, retained)
   ```
4. คลิก topic `telemetry` → ดู payload JSON ใน panel ขวามือ

**ตัวอย่าง payload ที่ควรเห็น:**
```json
{
  "board_id": "ESP32-FARM-001-NATTAPHOL-PALM",
  "timestamp": 12450,
  "rssi": -62,
  "sensors": {
    "water_temp": 28.5,
    "air_temp": 32.1,
    "air_humidity": 65.0,
    "water_overflow": false,
    "water_dry": false
  },
  "relays": {
    "relay1_pump": false,
    "relay2_fan": false,
    "relay3_heater": false
  }
}
```

---

### ทดสอบสั่ง Relay (Control Command)

1. ในช่อง **Publish** (panel ล่างขวา) กรอก topic:
   ```
  smartfarm/ESP32-FARM-001-NATTAPHOL-PALM/control
   ```
2. เลือก **QoS 1**
3. วาง payload JSON ในกล่องข้อความ แล้วคลิก **Publish**

#### เปิด Pump (relay1_pump = true)
```json
{
  "command": "relay_control",
  "relays": {
    "relay1_pump": true
  }
}
```

#### ปิด Pump
```json
{
  "command": "relay_control",
  "relays": {
    "relay1_pump": false
  }
}
```

#### เปิดพร้อมกันหลายตัว
```json
{
  "command": "relay_control",
  "relays": {
    "relay1_pump": true,
    "relay2_fan": true,
    "relay3_heater": false
  }
}
```

> หลัง publish ESP32 จะตอบกลับด้วย telemetry ทันที — ตรวจสอบได้ใน topic `telemetry`

---

### ทดสอบ Ping
```json
{
  "command": "ping"
}
```
ESP32 ตอบกลับด้วย status `"online"` ใน topic `smartfarm/ESP32-FARM-001-NATTAPHOL-PALM/status`

---

### ทดสอบ Reboot

> **คำเตือน:** ESP32 จะ restart จริง

```json
{
  "command": "reboot"
}
```
ดูลำดับ:
1. topic `status` → `"offline"` (ก่อน reboot)
2. ESP32 restart (~3 วินาที)
3. topic `status` → `"online"` (หลัง boot เสร็จ)

---

### Checklist ทดสอบก่อนส่ง Production

| # | การทดสอบ | วิธีตรวจสอบ |
|---|---|---|
| 1 | ESP32 connect broker | เห็น `status: "online"` ใน MQTT Explorer |
| 2 | Telemetry มาทุก 5s | topic `telemetry` update สม่ำเสมอ |
| 3 | Sensor data ถูกต้อง | ค่า `water_temp`, `air_temp`, `air_humidity` สมเหตุสมผล |
| 4 | Level sensor state ถูกต้อง | ค่า `water_overflow`, `water_dry` เปลี่ยนตามสถานะจริง |
| 5 | RSSI อยู่ในช่วงปกติ | ค่า `rssi` ระหว่าง -80 ถึง -40 dBm |
| 6 | Relay สั่งจาก Dashboard | Publish control → relay state เปลี่ยนใน telemetry ถัดไป |
| 7 | Partial relay update | ส่งเฉพาะ `relay1_pump` → relay อื่นไม่เปลี่ยน |
| 8 | Ping response | ส่ง `ping` → ได้ `status: "online"` กลับมา |
| 9 | LWT (ถอด WiFi) | ตัด WiFi ESP32 → broker ส่ง `status: "offline"` อัตโนมัติ |
| 10 | Reconnect หลังหลุด WiFi | ต่อ WiFi กลับ → ESP32 reconnect และ publish `online` อีกครั้ง |

---

*SmartFarm ESP32 Firmware v1.0.0 — PlatformIO + Arduino Framework*
