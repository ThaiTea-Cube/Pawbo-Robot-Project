# ทดสอบตั้งค่า Wi‑Fi ผ่าน QR และแสดงเวลาไทยสองจอ

ใช้ Massmore ESP32 กับจอ ST7789 ขนาด 240 × 240 สองจอ ผังขาและการหมุนภาพ 180° อ้างอิงจาก `D:\Project_DataD\Project_2026\Pawbo Robot Project\Module Test\tft-test`

| ขาจอ | จอ 1 | จอ 2 |
| --- | --- | --- |
| SCL (SPI SCK) | GPIO18 | GPIO18 |
| SDA (SPI MOSI) | GPIO23 | GPIO23 |
| RST | GPIO4 | GPIO4 |
| DC | GPIO2 | GPIO2 |
| CS | GPIO32 | GPIO33 |
| VCC | 3V3 | 3V3 |
| GND | GND | GND |

ต่อ BL ตามสเปกของโมดูลจอ หากเป็นขาเปิดไฟแบบ active-high ที่รองรับ 3.3 V ให้ต่อ 3V3

## อัปโหลด

1. ใน Arduino IDE ติดตั้งบอร์ด **esp32 by Espressif Systems 3.x** และไลบรารี **Adafruit ST7735 and ST7789 Library** และ **Adafruit GFX Library** พร้อม dependencies ที่ IDE แจ้ง
2. เปิด `wifi-clock-test.ino` เลือกบอร์ดและพอร์ตของ Massmore ESP32 ตามที่ใช้กับ `tft-test` หากใช้โปรไฟล์ ESP32 ทั่วไป ให้เลือก **ESP32 Dev Module**
3. ตั้ง **Tools → Erase All Flash Before Sketch Upload → Enabled** แล้วกด **Upload** ค่านี้ทำให้การอัปโหลดแต่ละครั้งเริ่มตั้งค่า Wi‑Fi ใหม่ แม้อัปโหลดไบนารีเดิม และจะลบข้อมูลแฟลชส่วนอื่นของบอร์ดด้วย การกด EN/RESET หรือปิดแล้วเปิดไฟจะเก็บชื่อเครือข่ายและรหัสผ่านไว้ใน EEPROM จำลองบนแฟลช

## ทดลองบนบอร์ด

1. สแกน QR บน **TFT1** และเข้าร่วมเครือข่าย Pawbo ตามคำแนะนำบน **TFT2** หากมือถือไม่เปิดหน้าตั้งค่าเอง ให้เปิด `http://192.168.4.1/` ในเบราว์เซอร์
2. เลือก SSID จากผลสแกนเครือข่าย **2.4 GHz** แล้วกด **Continue** หน้าเว็บจะแสดงช่องตามประเภทที่เลือก
   - **Open** เชื่อมต่อโดยไม่กรอกรหัสผ่าน
   - **WPA2/WPA3 Personal** กรอกรหัสผ่าน Wi‑Fi
   - **Enterprise (PEAP)** ใช้ PEAP/MSCHAPv2 กรอกชื่อผู้ใช้และรหัสผ่าน สามารถใส่ outer identity เพิ่มได้ถ้าเครือข่ายกำหนด ช่องใบรับรอง CA อยู่ใน **Advanced settings** และเว้นว่างได้ เมื่อเว้นว่างบอร์ดจะเชื่อมต่อโดยไม่ตรวจสอบใบรับรองของเซิร์ฟเวอร์ RADIUS ผลสแกนระบุได้ว่าเป็น Enterprise ส่วนวิธี EAP ต้องตรวจจากผู้ดูแล
3. ค่าเริ่มต้นใช้ DHCP ถ้าต้องกำหนด IPv4 เอง เปิด **Advanced settings → Use manual IPv4** แล้วกรอก IP, gateway, subnet mask และ DNS
4. กด **Connect and save** และเปิดหน้ารอไว้ เมื่อบอร์ดได้รับ IPv4 และบันทึกลง EEPROM สำเร็จ หน้ารอจะได้รับ HTTP 303 ไปยัง `AFTER_SETUP_URL` ซึ่งตั้งเป็น Google จากนั้นบอร์ดจะปิดฮอตสปอต หากหน้าต่าง captive portal ของมือถือปิดเอง ให้เปิดปลายทางในเบราว์เซอร์หลังมือถือกลับมาใช้อินเทอร์เน็ต
5. ทั้งสองจอจะแสดงเวลาไทย UTC+7 และวันที่เมื่อซิงก์ NTP กับ `time.navy.mi.th` สำเร็จ หากขึ้น **Waiting for NTP** ให้ตรวจว่าเครือข่ายออกอินเทอร์เน็ตและส่ง UDP พอร์ต 123 ได้
6. กด EN/RESET เพื่อทดสอบการใช้ค่าเดิม หากเชื่อมต่อไม่ได้ภายใน 30 วินาทีเมื่อเปิดเครื่อง บอร์ดจะเปิดหน้าตั้งค่าอีกครั้ง เมื่ออัปโหลดโดยเปิด **Erase All Flash Before Sketch Upload** บอร์ดจะเริ่มตั้งค่าใหม่

[คู่มือเมนูอัปโหลดของ Espressif](https://documentation.espressif.com/projects/arduino-esp32/en/latest/guides/tools_menu.html) · [เอกสาร NECTEC ที่ระบุ NTP ของกองทัพเรือ](https://www.nectec.or.th/default/standard/std-nt/NT-4003.2-6005.pdf)
