# PAWBO V1

เฟิร์มแวร์ ESP32 และเว็บสำหรับทดสอบ Servo ทีละตัว ตั้งปลุกทุกวัน ดูสถานะ และเลือกแบบตาบนจอ TFT ทั้งสองจอ

## เปิดใช้งาน

1. เปิด `PawboControl/PawboControl.ino` ใน Arduino IDE ใช้บอร์ด ESP32 และ Arduino-ESP32 core **3.3.10** ติดตั้ง **Adafruit GFX**, **Adafruit ST7735 and ST7789**, **Adafruit PWM Servo Driver**, **ArduinoJson v7** และ **DFRobotDFPlayerMini** ตัว MQTT ใช้ ESP-MQTT ที่มากับ ESP32 core
2. ตรวจบัญชีอุปกรณ์ใน `PawboControl/MqttCredentials.local.h` ถ้าไม่มีไฟล์นี้ ให้สร้างจากตัวประกาศ `MQTT_USERNAME` และ `MQTT_PASSWORD` ใน `MqttCredentials.h` แล้วใส่บัญชีอุปกรณ์ อัปโหลดโดยตั้ง **Erase All Flash Before Sketch Upload → Disabled** เพื่ออ่าน Wi‑Fi เดิมและค่า Alarm ของชุดทดสอบ
3. เปิด Serial Monitor ที่ **115200** ถ้าหุ่นต่อเครือข่ายเดิมไม่ได้ ให้เชื่อม Wi‑Fi `Pawbo-…` ด้วยรหัสจากจอหรือ Serial แล้วเปิด [หน้าตั้งค่า Wi‑Fi](http://192.168.4.1/) MQTT/TLS จะเริ่มหลังบอร์ดได้เวลาจาก NTP
4. เปิด `web/index.html` พร้อมอินเทอร์เน็ตเพื่อโหลด MQTT.js ใส่บัญชี MQTT สำหรับเว็บและรอให้หุ่นขึ้นออนไลน์ ให้เวลาเครื่องที่เปิดเว็บตรงกับเวลาปัจจุบันเพื่อผ่านการตรวจอายุคำสั่ง
5. แยกบัญชีเว็บกับ ESP32 ที่ Broker และกำหนด ACL ตามตารางด้านล่าง ถ้า ACL เดิมอนุญาตเฉพาะ `robot01/*` ต้องเพิ่ม topic ใต้ `pawbo/robot01/` สำหรับ V1

## การต่ออุปกรณ์และทดสอบ

- ใช้ [ผังต่อสาย PAWBO V1](PIN-WIRING.md) สำหรับ GPIO, ไฟเลี้ยง และการต่อโมดูลทุกตัว
- เริ่มทดสอบโดยถอดก้านส่งกำลังออกจาก Servo ช่วงเริ่มต้น 75–105° เป็นมุมตามคำสั่ง PWM ต้องคาลิเบรตแต่ละช่องก่อนต่อกับกลไกจริง แถบเลื่อนส่งเมื่อปล่อย หุ่นขับได้ทีละช่องและตัด PWM หลัง 650 ms จากคำสั่งเว็บ เมื่อกด STOP หรือเมื่อตรวจพบว่า Wi‑Fi / MQTT หลุด
- นาฬิกาปลุกใช้เวลาไทย บันทึกใน NVS และทำงานต่อได้เมื่อเครือข่ายหลุดหลังซิงก์เวลาแล้ว หลังปิดไฟต้องซิงก์ NTP ใหม่
- ทดสอบบันทึกปลุกแล้วรีบูต ตั้งปลุกให้ดังในนาทีถัดไป ลองแตะเพื่อปิดเสียง สลับแบบตาจากเว็บ และทดลอง STOP/ตัด Wi‑Fi ระหว่างสั่ง Servo โดยสังเกตอุปกรณ์จริง

## MQTT

อิงจาก [PAWBO — System Design](https://app.notion.com/p/4173703c34b6811c8707e35dc2be7ceb) ใช้ TLS ฝั่ง ESP32 และ WSS ฝั่งเว็บ บัญชีเว็บส่งเฉพาะ `cmd/*` และอ่าน `status` / `ack` ตามตาราง บัญชี ESP32 ใช้สิทธิ์กลับกันและส่ง `event/alarm`

| Topic ใต้ `pawbo/robot01/` | QoS | Retain | ข้อมูลคำสั่ง |
| --- | --- | --- | --- |
| `cmd/motion` | 0 | No | `action: "set_servo"` + `servo` (1–8), `angle` (75–105), `duration_ms` (100–750) |
| `cmd/stop` | 1 | No | `action: "stop"` |
| `cmd/alarm` | 1 | No | `action: "set_alarm"` + `hour` (0–23), `minute` (0–59), `enabled` (boolean) |
| `cmd/eyes` | 1 | No | `action: "set_eyes"` + `style` (0–5 ตามลำดับในเว็บ) |
| `status` | 1 | Yes | Online, Robot state, fault, session, Servo, Wi‑Fi, นาฬิกาปลุก, แบบตา |
| `ack` | 1 | No | `command_id`, `status`, `robot_state`, `error_code` |
| `event/alarm` | 1 | No | `event: "started"` หรือ `"dismissed"` |

ทุกคำสั่งมี `schema_version: 1`, `command_id` แบบข้อความ, `session` จากสถานะปัจจุบัน และ `issued_at` รูปแบบ UTC เช่น `2026-11-18T08:00:00Z` คำสั่งหมดอายุหลัง 5 วินาที เซสชันเปลี่ยนเมื่อเชื่อมต่อ Broker ใหม่ และเฟิร์มแวร์ตรวจคำสั่งซ้ำก่อนทำงาน

สำหรับ Servo ผล `completed` ยืนยันว่าไดรเวอร์รับค่า PWM แล้ว การวัดมุมจริงให้ตรวจที่ตัว Servo
