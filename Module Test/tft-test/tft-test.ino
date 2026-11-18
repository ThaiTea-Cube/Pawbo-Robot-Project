#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <cmath>

constexpr int TFT_SCK = 18;
constexpr int TFT_MOSI = 23;
constexpr int TFT_RST = 4;
constexpr int TFT_DC = 2;
constexpr int TFT1_CS = 32;
constexpr int TFT2_CS = 33;
constexpr int TFT_WIDTH = 240;
constexpr int TFT_HEIGHT = 240;
constexpr uint8_t TFT_ROTATION = 2;  // 180 degrees
constexpr uint32_t FRAME_INTERVAL_US = 16667;
constexpr uint32_t PULSE_PERIOD_US = 2400000;
constexpr uint32_t FPS_REPORT_MS = 3000;
constexpr int EYE_RADIUS = 50;
constexpr int EYE_SIZE = 2 * EYE_RADIUS + 1;
constexpr int EYE_X = 143;
constexpr int EYE_Y = 136;

constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3);
}

constexpr uint16_t BACKGROUND = rgb565(7, 4, 13);
constexpr uint16_t WEB = rgb565(46, 26, 57);
constexpr uint16_t EYE_SHADOW = rgb565(29, 7, 25);
constexpr uint16_t EYE_RIM = rgb565(104, 18, 41);
constexpr uint16_t PUPIL = rgb565(10, 3, 12);
constexpr uint16_t HIGHLIGHT = rgb565(255, 201, 182);

// The sketch resets both panels together before initializing either panel.
Adafruit_ST7789 tft1(&SPI, TFT1_CS, TFT_DC, -1);
Adafruit_ST7789 tft2(&SPI, TFT2_CS, TFT_DC, -1);
GFXcanvas16 irisCanvas(EYE_SIZE, EYE_SIZE);
uint32_t nextFrameAt = 0;
uint32_t previousFrameAt = 0;
uint32_t pulsePhase = 0;
uint32_t fpsReportAt = 0;
uint32_t framesDrawn = 0;

int mirrorX(int x, bool mirror) {
  return mirror ? TFT_WIDTH - 1 - x : x;
}

void drawWeb(Adafruit_ST7789& display, bool mirror) {
  constexpr int SPOKE_X[] = {0, 28, 59, 87, 100};
  constexpr int SPOKE_Y[] = {100, 96, 80, 50, 0};
  constexpr int SPOKES = sizeof(SPOKE_X) / sizeof(SPOKE_X[0]);
  const int corner = mirrorX(0, mirror);
  for (int spoke = 0; spoke < SPOKES; ++spoke) {
    display.drawLine(corner, 0, mirrorX(SPOKE_X[spoke] * 2, mirror), SPOKE_Y[spoke] * 2, WEB);
  }
  for (int ring = 40; ring <= 200; ring += 40) {
    for (int spoke = 1; spoke < SPOKES; ++spoke) {
      display.drawLine(mirrorX(SPOKE_X[spoke - 1] * ring / 100, mirror),
                       SPOKE_Y[spoke - 1] * ring / 100,
                       mirrorX(SPOKE_X[spoke] * ring / 100, mirror),
                       SPOKE_Y[spoke] * ring / 100, WEB);
    }
  }
}

void drawSmallEye(Adafruit_ST7789& display, int x, int y, int radius) {
  display.fillCircle(x, y, radius + 5, EYE_SHADOW);
  display.fillCircle(x, y, radius, EYE_RIM);
  display.fillCircle(x, y, radius - 3, PUPIL);
  display.fillCircle(x - radius / 3, y - radius / 3, 3, HIGHLIGHT);
}

void drawEyeRim(Adafruit_GFX& display, int x, int y) {
  display.drawCircle(x, y, 68, EYE_RIM);
  display.fillCircle(x, y, 62, EYE_RIM);
  display.fillCircle(x, y, 56, PUPIL);
}

void drawFace(Adafruit_ST7789& display, bool mirror) {
  display.fillScreen(BACKGROUND);
  drawWeb(display, mirror);
  drawSmallEye(display, mirrorX(50, mirror), 60, 15);
  drawSmallEye(display, mirrorX(113, mirror), 42, 18);
  drawSmallEye(display, mirrorX(40, mirror), 132, 12);

  const int x = mirrorX(EYE_X, mirror);
  display.fillCircle(x, EYE_Y, 72, EYE_SHADOW);
  drawEyeRim(display, x, EYE_Y);
}

void drawIris(Adafruit_GFX& display, int x, int y, uint8_t glow) {
  display.fillCircle(x, y, 49, rgb565(glow * 3 / 4, 10, 29));
  display.fillCircle(x, y, 40, rgb565(glow, glow / 6, glow / 8));
  display.drawCircle(x, y, 39, rgb565(glow, glow / 3, glow / 5));
  display.fillTriangle(x, y - 42, x - 14, y - 1, x + 14, y - 1, PUPIL);
  display.fillTriangle(x, y + 42, x - 14, y - 1, x + 14, y - 1, PUPIL);
  display.fillCircle(x - 21, y - 19, 6, HIGHLIGHT);
  display.fillCircle(x + 23, y + 18, 3, HIGHLIGHT);
}

void drawFrame() {
  constexpr float TAU = 6.2831853f;
  const float phase = static_cast<float>(pulsePhase) / PULSE_PERIOD_US;
  const uint8_t glow = static_cast<uint8_t>(205.0f - 50.0f * cosf(TAU * phase) + 0.5f);
  // The four corners of this patch remain inside the static shadow circle.
  irisCanvas.fillScreen(EYE_SHADOW);
  drawEyeRim(irisCanvas, EYE_RADIUS, EYE_RADIUS);
  drawIris(irisCanvas, EYE_RADIUS, EYE_RADIUS, glow);
  uint16_t* pixels = irisCanvas.getBuffer();
  tft1.drawRGBBitmap(EYE_X - EYE_RADIUS, EYE_Y - EYE_RADIUS, pixels, EYE_SIZE, EYE_SIZE);
  tft2.drawRGBBitmap(mirrorX(EYE_X, true) - EYE_RADIUS, EYE_Y - EYE_RADIUS, pixels, EYE_SIZE, EYE_SIZE);
}

void setup() {
  Serial.begin(115200);

  pinMode(TFT1_CS, OUTPUT);
  pinMode(TFT2_CS, OUTPUT);
  digitalWrite(TFT1_CS, HIGH);
  digitalWrite(TFT2_CS, HIGH);

  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, LOW);
  delay(20);
  digitalWrite(TFT_RST, HIGH);
  delay(150);

  SPI.begin(TFT_SCK, -1, TFT_MOSI, -1);
  tft1.init(TFT_WIDTH, TFT_HEIGHT, SPI_MODE0);
  tft2.init(TFT_WIDTH, TFT_HEIGHT, SPI_MODE0);

  tft1.setRotation(TFT_ROTATION);
  tft2.setRotation(TFT_ROTATION);
  drawFace(tft1, false);
  drawFace(tft2, true);
  if (irisCanvas.getBuffer() == nullptr) {
    Serial.println("Iris buffer allocation failed.");
    return;
  }
  drawFrame();
  previousFrameAt = micros();
  nextFrameAt = previousFrameAt + FRAME_INTERVAL_US;
  fpsReportAt = millis();
  Serial.println("Spider eyes drawn at 180 degrees. TFT 1=left, TFT 2=right.");
}

void loop() {
  const uint32_t now = micros();
  if (irisCanvas.getBuffer() != nullptr && static_cast<int32_t>(now - nextFrameAt) >= 0) {
    const uint32_t late = now - nextFrameAt;
    nextFrameAt = (late >= FRAME_INTERVAL_US ? now : nextFrameAt) + FRAME_INTERVAL_US;
    pulsePhase = (pulsePhase + (now - previousFrameAt) % PULSE_PERIOD_US) % PULSE_PERIOD_US;
    previousFrameAt = now;
    drawFrame();
    ++framesDrawn;
  }
  const uint32_t elapsedMs = millis() - fpsReportAt;
  if (elapsedMs >= FPS_REPORT_MS && framesDrawn != 0) {
    const float fps = framesDrawn * 1000.0f / elapsedMs;
    Serial.printf("Both TFTs: %.1f FPS\n", fps);
    framesDrawn = 0;
    fpsReportAt += elapsedMs;
  }
  delay(1);
}
