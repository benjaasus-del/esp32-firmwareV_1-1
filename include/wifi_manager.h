#pragma once

// ตรวจสอบ SW1 ตอน startup — ถ้ากดค้าง WIFI_RESET_HOLD_MS → ล้าง credential
// เรียกก่อน wifiManagerConnect()
void wifiManagerCheckReset();

// เชื่อมต่อ WiFi: auto-connect ถ้ามี credential บันทึกอยู่
// ถ้าไม่มี หรือ connect ไม่ได้ → เปิด AP portal
// return true = connected
bool wifiManagerConnect();

// ลอง reconnect ด้วย credential ที่บันทึกไว้ (สำหรับเรียกใน loop)
void wifiManagerReconnect();
