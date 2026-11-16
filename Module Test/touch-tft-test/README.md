# สลับหน้าจอ TFT สองจอด้วย Touch Sensor

สเก็ตช์นี้ใช้ ESP32 บน Massmore, จอ ST7789 SPI 240×240 สองจอ และ Touch Sensor ดิจิทัล 3 ขา เช่น TTP223 แบบ Direct จอทั้งสองหมุนภาพ 180°

## ต่อสาย

ใช้ [ผังสายจอจาก tft-test](../tft-test/README.md) โดยจอซ้ายใช้ `CS = GPIO32` และจอขวาใช้ `CS = GPIO33` จากมุมมองด้านหน้าหุ่นยนต์

| ขา Touch Sensor | ขาบน Massmore ESP32 |
| --- | --- |
| `VCC` / `VDD` | `3V3` |
| `GND` | `GND` ร่วมกับจอ |
| `OUT` / `SIG` / `I/O` | ขาสัญญาณ `IO7 / GPIO35` |

## ตั้งค่า Wi-Fi และอัปโหลด

1. ใน `touch-tft-test.ino` ค่าเริ่มต้นคือ `WIFI_SSID = "KMUTT-Secure"` (Wi-Fi 2.4 GHz แบบ WPA2-Enterprise, PEAP/MSCHAPv2) ใส่บัญชีของคุณใน `WIFI_USERNAME` และ `WIFI_PASSWORD` ในเครื่องตัวเอง ถ้า `WIFI_IDENTITY` ว่าง โค้ดจะใช้ค่าเดียวกับชื่อผู้ใช้ตามรูปแบบในคู่มือ KMUTT ถ้า KMUTT IT ให้ใบรับรอง CA สำหรับ RADIUS ให้ใส่ PEM ใน `WIFI_CA_PEM` โดยเก็บบรรทัดขึ้นต้นและลงท้ายของใบรับรองไว้
2. ติดตั้ง **Adafruit ST7735 and ST7789 Library** และ **Adafruit GFX Library** พร้อม dependencies ที่ Arduino IDE เสนอ จากนั้นอัปโหลด เปิด Serial Monitor ที่ **115200 baud** และเว้นนิ้วจากแผ่นเซ็นเซอร์ช่วงเปิดเครื่อง
3. ตอนเปิดเครื่องจะขึ้น **หน้า 1** ตาแมงมุมเรืองแดง แตะเพื่อเลื่อนจากขวาไปซ้ายเข้าสู่ **หน้า 2** เวลาไทยอยู่จอซ้าย วันที่อยู่จอขวา
4. ยกนิ้วแล้วแตะเพื่อเข้า **หน้า 3** จอซ้ายเขียน `hello` จอขวาเขียน `world` แตะอีกครั้งเพื่อวนกลับไปหน้าตาแมงมุมในทิศเดิม

เวลาแสดงเป็น UTC+7 โดยใช้ `time.navy.mi.th` ของกรมอุทกศาสตร์ กองทัพเรือเป็นค่าเริ่มต้น ยังไม่ได้ยืนยัน hostname สาธารณะของกองทัพบก หากมี hostname ของกองทัพบกที่ใช้งานได้ ให้ใส่ใน `NTP_SERVER` จะแสดง `--:--:--` จนกว่าจะซิงก์เวลาได้ ทุกหน้ายังเลื่อนได้ขณะรอ Wi-Fi [เอกสารบริการเทียบเวลาของกรมอุทกศาสตร์](https://www.hydro.navy.mi.th/hydrokm/download/55_timestandard.pdf)

Serial Monitor ขึ้น `Wi-Fi connected` เมื่อเชื่อมกับมหาวิทยาลัยแล้ว และขึ้น `Clock synchronized` เมื่อได้เวลา ถ้า Wi-Fi ต่อได้แต่เวลายังเป็นขีด ให้ตรวจว่ามหาวิทยาลัยอนุญาต UDP พอร์ต 123 หรือใช้ NTP Server ที่ฝ่าย IT ระบุ เมื่อเว้น `WIFI_CA_PEM` ว่าง สเก็ตช์จะเรียก PEAP โดยไม่ส่ง CA ที่เจาะจงให้ไลบรารี เช่นเดียวกับตัวอย่างตั้งค่าของ KMUTT [คู่มือ KMUTT-Secure](https://cc.kmutt.ac.th/en/wifi.html) · [ตัวอย่าง Enterprise ของ Espressif](https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFi/examples/WiFiClientEnterprise)

แต่ละหน้าค้าง 4 วินาทีแล้วเลื่อนต่ออัตโนมัติ ปรับระยะเวลาได้ที่ `PAGE_HOLD_MS` หรือแตะเพื่อข้ามไปหน้าถัดไป แตะค้างจะเปลี่ยนหน้าเพียงครั้งเดียว รอให้เลื่อนจบก่อนแตะอีกครั้ง หากสลับหน้าตอนปล่อยแทนตอนแตะ ให้เช็กว่าโมดูลใช้ Active Low หรือไม่ แล้วปรับ `TOUCH_ACTIVE_LEVEL` เป็น `LOW` ให้ตรงกับโมดูล
