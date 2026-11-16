# ทดสอบลำโพง Bluetooth ด้วย ESP32

มือถือ → Bluetooth Classic A2DP → ESP32 → DAC ที่ GPIO25 (IO1) → แอมป์แอนะล็อก → ลำโพง

สเก็ตช์รับเพลงจากมือถือและรวมเสียงซ้าย–ขวาออกเป็นโมโน 8 บิต ไม่ต้องใช้ DFPlayer หรือ microSD ใช้กับ ESP32 รุ่นดั้งเดิม เช่น ESP32-WROOM-32 บนบอร์ด Massmore ในโครงการ

## ต่อสาย

| จาก | ไปที่ |
| --- | --- |
| Massmore `IO1 / GPIO25` (ขาสัญญาณของ IO1) | อินพุตแอนะล็อกของแอมป์ ถ้าแอมป์ไม่มีตัวเก็บประจุคัปปลิงที่อินพุต ให้ต่อผ่านตัวเก็บประจุคั่น DC ตามสเปกแอมป์ |
| Massmore `GND` | กราวด์ฝั่งอินพุตของแอมป์ |
| เอาต์พุตช่องหนึ่งของแอมป์ | ลำโพงตามผังและอิมพีแดนซ์ที่แอมป์รองรับ |

ปิดไฟก่อนต่อสาย แล้วจ่ายไฟให้แอมป์ตามสเปก ขา GPIO25 ส่งสัญญาณเข้าแอมป์ ให้ต่อลำโพงเปล่าที่เอาต์พุตของแอมป์เท่านั้น

## ติดตั้งและทดสอบ

1. ใน Arduino IDE ติดตั้ง **esp32 by Espressif Systems** รุ่น 3.x ผ่าน Boards Manager แล้วเลือก **ESP32 Dev Module** หรือรุ่นจริงที่ตรงกับบอร์ด หากคอมไพล์แล้วพื้นที่แฟลชไม่พอ ให้เลือก Partition Scheme ที่มีพื้นที่แอปใหญ่ขึ้นและตรงกับขนาดแฟลชของบอร์ด
2. ดาวน์โหลด [ESP32-A2DP จาก GitHub เป็น ZIP](https://github.com/pschatzmann/ESP32-A2DP/archive/refs/heads/main.zip) แล้วเลือก **Sketch → Include Library → Add .ZIP Library…** สเก็ตช์ใช้ ESP32-A2DP รุ่นที่มีเมธอด `channels()` และใช้ไดรเวอร์ DAC ที่มากับชุดบอร์ด ESP32
3. เปิด `speaker-test.ino` เลือกพอร์ตแล้วอัปโหลด เปิด Serial Monitor ที่ **115200 baud**
  4. บนมือถือ เปิด Bluetooth แล้วจับคู่กับ **Pawbo Speaker Test** เปิดเพลงโดยเริ่มจากระดับเสียงเบา เมื่อเริ่มรับเสียง Serial Monitor ควรแสดง `GPIO25 DAC ready at ... Hz`
5. ถือว่าผ่านเมื่อได้ยินเพลงจากลำโพงและสามารถหยุดและเล่นต่อจากมือถือได้ ถ้าจับคู่แล้วเงียบ ให้เช็กว่ามือถือเลือก Pawbo เป็นเอาต์พุตเสียง ตรวจไฟแอมป์ สายอินพุตที่ IO1 และกราวด์ร่วม

[ไลบรารี ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP) · [เอกสาร DAC ของ Espressif](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/dac.html)
