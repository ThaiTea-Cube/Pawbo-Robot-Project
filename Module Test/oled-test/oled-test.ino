#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

constexpr int OLED1_SDA = 21;
constexpr int OLED1_SCL = 22;
constexpr int OLED2_SDA = 32;
constexpr int OLED2_SCL = 33;
constexpr uint32_t I2C_HZ = 100000;
constexpr uint8_t WIDTH = 128;
constexpr uint8_t HEIGHT = 64;
constexpr uint8_t OLED_ADDRESSES[] = {0x3C, 0x3D};
constexpr uint32_t FRAME_INTERVAL_MS = 1000;

TwoWire secondBus(1);
Adafruit_SSD1306 oled1(WIDTH, HEIGHT, &Wire, -1, I2C_HZ, I2C_HZ);
Adafruit_SSD1306 oled2(WIDTH, HEIGHT, &secondBus, -1, I2C_HZ, I2C_HZ);
bool oled1Ready = false;
bool oled2Ready = false;
uint32_t lastFrameAt = 0;
uint32_t frame = 0;

bool startDisplay(Adafruit_SSD1306& display, TwoWire& bus, int sda, int scl, int number) {
  if (!bus.begin(sda, scl, I2C_HZ)) {
    Serial.printf("OLED %d: could not start I2C on SDA=%d SCL=%d\n", number, sda, scl);
    return false;
  }

  for (uint8_t address : OLED_ADDRESSES) {
    bus.beginTransmission(address);
    if (bus.endTransmission() != 0) {
      continue;
    }
    // The bus is already running on the selected pins.
    if (!display.begin(SSD1306_SWITCHCAPVCC, address, false, false)) {
      Serial.printf("OLED %d: SSD1306 initialization failed\n", number);
      return false;
    }
    Serial.printf("OLED %d: I2C ACK at 0x%02X (SDA=%d SCL=%d)\n",
                  number, unsigned(address), sda, scl);
    return true;
  }

  Serial.printf("OLED %d: no ACK at 0x3C or 0x3D. Check power and wires.\n", number);
  return false;
}

void drawTest(Adafruit_SSD1306& display, int number) {
  display.clearDisplay();
  display.drawRect(0, 0, WIDTH, HEIGHT, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(8, 8);
  display.print("OLED ");
  display.print(number);
  display.setTextSize(1);
  display.setCursor(8, 30);
  display.print("Frame ");
  display.print(frame);

  // The two markers move in opposite directions.
  const int offset = (frame % 13) * 8;
  const int x = number == 1 ? 8 + offset : 104 - offset;
  display.fillRect(x, 46, 16, 8, SSD1306_WHITE);
  display.display();
}

void updateDisplays() {
  if (oled1Ready) {
    drawTest(oled1, 1);
  }
  if (oled2Ready) {
    drawTest(oled2, 2);
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);
  oled1Ready = startDisplay(oled1, Wire, OLED1_SDA, OLED1_SCL, 1);
  oled2Ready = startDisplay(oled2, secondBus, OLED2_SDA, OLED2_SCL, 2);
  Serial.println("Check both OLED labels, borders and moving markers.");
  updateDisplays();
  lastFrameAt = millis();
}

void loop() {
  const uint32_t now = millis();
  if (now - lastFrameAt >= FRAME_INTERVAL_MS) {
    lastFrameAt = now;
    ++frame;
    updateDisplays();
  }
  delay(10);
}
