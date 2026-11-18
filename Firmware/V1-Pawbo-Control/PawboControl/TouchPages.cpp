#include "TouchPages.h"
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <time.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <DFRobotDFPlayerMini.h>

extern Adafruit_ST7789 tft1;
extern Adafruit_ST7789 tft2;

namespace {

constexpr int TFT_WIDTH = 240;
constexpr int TFT_HEIGHT = 240;
constexpr uint32_t EYE_PERIOD_MS = 2400;
constexpr uint32_t SMALL_EYE_FRAME_MS = 17;
constexpr uint32_t LARGE_EYE_FRAME_MS = 33;
constexpr uint32_t BUTTON_REPEAT_DELAY_MS = 380;
constexpr uint32_t BUTTON_REPEAT_FAST_AFTER_MS = 1200;

enum class Page { Eyes, Clock, Alarm };
enum class AlarmField { Hour, Minute, Enabled };

struct Button {
  int pin;
  bool candidate = false;
  bool pressed = false;
  uint32_t changedAt = 0;
  uint32_t pressedAt = 0;
  uint32_t repeatAt = 0;
  bool repeating = false;
};

constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3);
}

constexpr uint16_t BACKGROUND = rgb565(7, 4, 13);
constexpr uint16_t WEB = rgb565(46, 26, 57);
constexpr uint16_t EYE_SHADOW = rgb565(29, 7, 25);
constexpr uint16_t EYE_RIM = rgb565(104, 18, 41);
constexpr uint16_t PUPIL = rgb565(10, 3, 12);
constexpr uint16_t HIGHLIGHT = rgb565(255, 201, 182);

uint32_t lastPulseAt = 0;
uint32_t eyeStartedAt = 0;

int mirrorX(int x, bool mirror) {
  return mirror ? TFT_WIDTH - 1 - x : x;
}

void drawWeb(Adafruit_GFX& display, bool mirror) {
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

void drawSmallEye(Adafruit_GFX& display, int x, int y, int radius) {
  display.fillCircle(x, y, radius + 5, EYE_SHADOW);
  display.fillCircle(x, y, radius, EYE_RIM);
  display.fillCircle(x, y, radius - 3, PUPIL);
  display.fillCircle(x - radius / 3, y - radius / 3, 3, HIGHLIGHT);
}

void drawEyeRim(Adafruit_GFX& display, int x) {
  display.drawCircle(x, 136, 68, EYE_RIM);
  display.fillCircle(x, 136, 62, EYE_RIM);
  display.fillCircle(x, 136, 56, PUPIL);
}

void drawFace(Adafruit_GFX& display, bool mirror) {
  display.fillScreen(BACKGROUND);
  drawWeb(display, mirror);
  drawSmallEye(display, mirrorX(50, mirror), 60, 15);
  drawSmallEye(display, mirrorX(113, mirror), 42, 18);
  drawSmallEye(display, mirrorX(40, mirror), 132, 12);

  const int x = mirrorX(143, mirror);
  display.fillCircle(x, 136, 72, EYE_SHADOW);
  drawEyeRim(display, x);
}

void drawIris(Adafruit_GFX& display, int x, uint8_t glow) {
  constexpr int y = 136;
  display.fillCircle(x, y, 49, rgb565(glow * 3 / 4, 10, 29));
  display.fillCircle(x, y, 40, rgb565(glow, glow / 6, glow / 8));
  display.drawCircle(x, y, 39, rgb565(glow, glow / 3, glow / 5));
  display.fillTriangle(x, y - 42, x - 14, y - 1, x + 14, y - 1, PUPIL);
  display.fillTriangle(x, y + 42, x - 14, y - 1, x + 14, y - 1, PUPIL);
  display.fillCircle(x - 21, y - 19, 6, HIGHLIGHT);
  display.fillCircle(x + 23, y + 18, 3, HIGHLIGHT);
}

constexpr int TOUCH_PIN = 35;  // Massmore IO7
constexpr int LEFT_BUTTON_PIN = 25;
constexpr int RIGHT_BUTTON_PIN = 27;
constexpr int ALARM_RX_PIN = 13;
constexpr int ALARM_TX_PIN = 14;
constexpr int TOUCH_ACTIVE_LEVEL = HIGH;
constexpr uint32_t TOUCH_STABLE_MS = 40;
constexpr uint32_t LONG_TOUCH_MS = 700;
constexpr uint32_t TOUCH_STARTUP_MS = 700;
constexpr uint32_t ALARM_RING_MS = 60000;
constexpr uint32_t SLIDE_MS = 640;
constexpr uint32_t SLIDE_FRAME_MS = 20;

constexpr time_t FIRST_VALID_TIME = 1704067200;  // 2024-01-01 UTC

Page currentPage = Page::Eyes;
Page incomingPage = Page::Eyes;
bool sliding = false;
int slideDirection = 1;
uint32_t slideStartedAt = 0;
uint32_t lastSlideFrameAt = 0;
bool stableTouched = false;
bool candidateTouched = false;
uint32_t candidateSince = 0;
uint32_t touchedAt = 0;
uint32_t touchReadyAt = 0;
bool longHandled = false;
Button leftButton{LEFT_BUTTON_PIN};
Button rightButton{RIGHT_BUTTON_PIN};
uint8_t eyeStyle = 0;
uint32_t eyeElapsed = 0;
constexpr const char* EYE_NAMES[] = {"Widow Core", "Ion Hunter", "Void Eclipse",
                                     "Kitsune Ember", "Ghost Sonar", "Glitch Hex"};
constexpr unsigned EYE_COUNT = sizeof(EYE_NAMES) / sizeof(EYE_NAMES[0]);
Preferences preferences;
HardwareSerial alarmSerial(2);
DFRobotDFPlayerMini alarmPlayer;
constexpr uint16_t MINUTES_PER_DAY = 24 * 60;
constexpr uint16_t ALARM_ENABLED_BIT = 0x8000;
bool settingsReady = false;
uint16_t alarmMinute = 7 * 60;
uint16_t draftMinute = alarmMinute;
bool alarmEnabled = false;
bool draftEnabled = false;
bool editingAlarm = false;
AlarmField alarmField = AlarmField::Hour;
bool alarmRinging = false;
uint32_t alarmStartedAt = 0;
uint32_t lastAlarmDay = 0;
time_t lastClockSecond = -1;
time_t clockAtSlideStart = -1;
char clockTime[9] = "--:--:--";
char clockDate[11] = "----/--/--";

constexpr int BATCH_ROWS = 16;
uint16_t pixelTransfer[TFT_WIDTH * BATCH_ROWS];

struct SlideSnapshot {
  uint16_t* runs = nullptr;
  uint16_t rowStarts[TFT_HEIGHT + 1]{};
  uint16_t palette[16]{};
};

// The eye and text scenes use at most 16 colors per panel.
class SlideCanvas : public Adafruit_GFX {
 public:
  SlideCanvas() : Adafruit_GFX(TFT_WIDTH, TFT_HEIGHT) {}

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x >= 0 && x < TFT_WIDTH && y >= 0 && y < TFT_HEIGHT) {
      uint8_t& packed = pixels[y][x / 2];
      const uint8_t index = findColor(color);
      packed = x & 1 ? (packed & 0x0F) | (index << 4) : (packed & 0xF0) | index;
    }
  }

  void fillScreen(uint16_t color) override {
    palette[0] = color;
    paletteSize = 1;
    lastIndex = 0;
    memset(pixels, 0, sizeof(pixels));
  }

  void cache(SlideSnapshot& snapshot) {
    constexpr size_t MAX_CACHE_BYTES = 10240;
    std::free(snapshot.runs);
    snapshot.runs = nullptr;
    const unsigned runCount = encodeRuns(snapshot, nullptr);
    const size_t bytes = runCount * sizeof(uint16_t);
    if (bytes > MAX_CACHE_BYTES) {
      return;
    }
    snapshot.runs = static_cast<uint16_t*>(std::malloc(bytes));
    if (snapshot.runs != nullptr) {
      memcpy(snapshot.palette, palette, sizeof(palette));
      encodeRuns(snapshot, snapshot.runs);
    }
  }

  void sendSlice(Adafruit_ST7789& display, int sourceX, int destX, int width,
                 int top = 0, int height = TFT_HEIGHT, const SlideSnapshot* snapshot = nullptr) {
    display.startWrite();
    display.setAddrWindow(destX, top, width, height);
    const int bottom = top + height;
    for (int y = top; y < bottom; y += BATCH_ROWS) {
      const int rows = min(BATCH_ROWS, bottom - y);
      uint16_t* output = pixelTransfer;
      for (int row = 0; row < rows; ++row) {
        if (snapshot != nullptr) {
          output = decodeRow(*snapshot, y + row, sourceX, width, output);
          continue;
        }
        const uint8_t* source = pixels[y + row];
        int x = sourceX;
        const int end = x + width;
        if (x & 1) {
          *output++ = palette[source[x++ / 2] >> 4];
        }
        for (; x + 1 < end; x += 2) {
          const uint8_t packed = source[x / 2];
          *output++ = palette[packed & 0x0F];
          *output++ = palette[packed >> 4];
        }
        if (x < end) {
          *output++ = palette[source[x / 2] & 0x0F];
        }
      }
      display.writePixels(pixelTransfer, rows * width);
    }
    display.endWrite();
  }

 private:
  uint8_t colorIndexAt(int y, int x) const {
    const uint8_t packed = pixels[y][x / 2];
    return (x & 1) ? packed >> 4 : packed & 0x0F;
  }

  unsigned encodeRuns(SlideSnapshot& snapshot, uint16_t* runs) const {
    unsigned count = 0;
    for (int y = 0; y < TFT_HEIGHT; ++y) {
      snapshot.rowStarts[y] = count;
      int start = 0;
      while (start < TFT_WIDTH) {
        const uint8_t color = colorIndexAt(y, start);
        int end = start + 1;
        while (end < TFT_WIDTH && colorIndexAt(y, end) == color) {
          ++end;
        }
        if (runs != nullptr) {
          runs[count] = ((end - start) << 4) | color;
        }
        ++count;
        start = end;
      }
    }
    snapshot.rowStarts[TFT_HEIGHT] = count;
    return count;
  }

  static uint16_t* decodeRow(const SlideSnapshot& snapshot, int y, int sourceX,
                             int width, uint16_t* output) {
    const int sliceEnd = sourceX + width;
    int x = 0;
    for (unsigned i = snapshot.rowStarts[y]; i < snapshot.rowStarts[y + 1] && x < sliceEnd; ++i) {
      const uint16_t run = snapshot.runs[i];
      const int end = x + (run >> 4);
      const int visible = min(end, sliceEnd) - max(x, sourceX);
      if (visible > 0) {
        std::fill_n(output, visible, snapshot.palette[run & 0x0F]);
        output += visible;
      }
      x = end;
    }
    return output;
  }

  uint8_t findColor(uint16_t color) {
    if (palette[lastIndex] == color) {
      return lastIndex;
    }
    for (unsigned i = 0; i < paletteSize; ++i) {
      if (palette[i] == color) {
        lastIndex = i;
        return lastIndex;
      }
    }
    if (paletteSize == PALETTE_CAPACITY) {
      return 0;
    }
    lastIndex = paletteSize++;
    palette[lastIndex] = color;
    return lastIndex;
  }

  static constexpr unsigned PALETTE_CAPACITY = 16;
  uint16_t palette[PALETTE_CAPACITY]{};
  unsigned paletteSize = 1;
  uint8_t lastIndex = 0;
  uint8_t pixels[TFT_HEIGHT][TFT_WIDTH / 2];
};

SlideCanvas slideCanvas;
SlideSnapshot slideFrames[4];

void clearSlideCache() {
  for (auto& frame : slideFrames) {
    std::free(frame.runs);
    frame.runs = nullptr;
  }
}

uint8_t eyeGlow() {
  const float phase = static_cast<float>(eyeElapsed % EYE_PERIOD_MS) / EYE_PERIOD_MS;
  return static_cast<uint8_t>(205.0f - 50.0f * cosf(phase * 6.2831853f) + 0.5f);
}

void eyeArc(Adafruit_GFX& display, int x, int y, int radius, float start,
            float sweep, uint16_t color) {
  for (int i = 0; i < 20; ++i) {
    const float a = start + sweep * i / 20;
    const float b = start + sweep * (i + 1) / 20;
    display.drawLine(x + cosf(a) * radius, y + sinf(a) * radius,
                     x + cosf(b) * radius, y + sinf(b) * radius, color);
  }
}

void drawStyledEye(Adafruit_GFX& display, bool right) {
  constexpr float TAU = 6.2831853f;
  const float phase = (eyeElapsed % 14400u) * TAU / 14400;
  if (eyeStyle == 0) {
    drawFace(display, right);
    drawIris(display, mirrorX(143, right), eyeGlow());
    return;
  }

  switch (eyeStyle) {
    case 1: {  // Ion Hunter
      display.fillScreen(rgb565(7, 20, 33));
      const int x = 120 + sinf(phase * 2) * 9;
      const uint16_t cyan = rgb565(102, 239, 255);
      display.drawCircle(x, 120, 82, rgb565(37, 67, 86));
      display.drawCircle(x, 120, 65, rgb565(45, 96, 118));
      for (int n = 0; n < 3; ++n) {
        const float a = (right ? -phase : phase) * 3 + n * TAU / 3;
        for (int r = 56; r <= 60; ++r) {
          eyeArc(display, x, 120, r, a, 1.2f, cyan);
        }
      }
      display.fillCircle(x, 120, 25, rgb565(21, 58, 81));
      display.fillCircle(x, 120, 13, cyan);
      display.fillCircle(x, 120, 6, ST77XX_WHITE);
      break;
    }
    case 2: {  // Void Eclipse
      display.fillScreen(rgb565(11, 8, 20));
      const uint16_t purple = rgb565(204, 160, 255);
      display.drawCircle(120, 120, 79, rgb565(56, 32, 89));
      display.fillCircle(120, 120, 65, rgb565(97, 57, 153));
      for (int r = 58; r <= 62; ++r) {
        eyeArc(display, 120, 120, r, phase, 3.9f, purple);
      }
      display.fillCircle(113, 116, 52, rgb565(8, 7, 17));
      for (int n = 0; n < 3; ++n) {
        const float a = phase * (right ? -1 : 1) + n * TAU / 3;
        display.fillCircle(120 + cosf(a) * 79, 120 + sinf(a) * 79, 2 + n, purple);
      }
      break;
    }
    case 3: {  // Kitsune Ember
      display.fillScreen(rgb565(22, 9, 17));
      const int a = mirrorX(25, right), b = mirrorX(215, right);
      const int c = mirrorX(86, right), d = mirrorX(151, right);
      display.fillTriangle(a, 86, b, 128, c, 147, rgb565(111, 25, 36));
      display.fillTriangle(c, 147, b, 128, d, 153, rgb565(111, 25, 36));
      display.fillTriangle(a, 94, b, 128, c, 132, rgb565(255, 124, 48));
      display.fillTriangle(c, 132, b, 128, d, 141, rgb565(226, 66, 36));
      const int p = mirrorX(130 + sinf(phase * 2) * 6, right);
      display.fillTriangle(p, 106, p - 9, 129, p + 8, 134, PUPIL);
      display.drawLine(mirrorX(42, right), 74, mirrorX(100, right), 87, HIGHLIGHT);
      display.drawLine(mirrorX(55, right), 157, mirrorX(116, right), 174, rgb565(190, 64, 86));
      break;
    }
    case 4: {  // Ghost Sonar
      display.fillScreen(rgb565(6, 21, 19));
      for (int r = 26; r <= 86; r += 20) {
        display.drawCircle(120, 120, r, rgb565(29, 96, 75));
      }
      display.drawFastHLine(34, 120, 173, rgb565(40, 90, 80));
      display.drawFastVLine(120, 34, 173, rgb565(40, 90, 80));
      const float sweep = phase * 2 + (right ? 0.55f : 0);
      for (int i = 7; i >= 0; --i) {
        const float a = sweep - i * 0.09f;
        display.drawLine(120, 120, 120 + cosf(a) * 86, 120 + sinf(a) * 86,
                         rgb565(107 - i * 11, 255 - i * 25, 167 - i * 17));
      }
      for (int n = 0; n < 3; ++n) {
        const float a = 0.6f + n * TAU / 3;
        const int r = 55 + n * 8;
        const bool lit = fmodf(sweep - a + TAU, TAU) < 0.8f;
        display.fillCircle(120 + cosf(a) * r, 120 + sinf(a) * r, 3,
                           lit ? rgb565(148, 255, 196) : rgb565(40, 108, 72));
      }
      display.fillCircle(120, 120, 5, rgb565(173, 255, 209));
      break;
    }
    case 5: {  // Glitch Hex
      display.fillScreen(rgb565(16, 9, 25));
      const uint32_t eyeFrame = eyeElapsed / 100;
      const int jitter = eyeFrame % 17 < 3 ? (right ? -10 : 10) : 0;
      for (int y = 51; y < 196; y += 6) {
        display.drawFastHLine(35, y, 170, rgb565(37, 22, 48));
      }
      for (int layer = 0; layer < 3; ++layer) {
        constexpr int SHIFT[] = {-5, 5, 0};
        constexpr uint16_t COLORS[] = {rgb565(38, 217, 227), rgb565(240, 70, 166), PUPIL};
        const int x = 120 + SHIFT[layer] + jitter;
        display.fillTriangle(x - 85, 123, x - 45, 86, x + 79, 109, COLORS[layer]);
        display.fillTriangle(x - 85, 123, x + 79, 109, x + 50, 155, COLORS[layer]);
        display.fillTriangle(x - 85, 123, x + 50, 155, x - 40, 164, COLORS[layer]);
      }
      const int x = 120 + jitter;
      const uint16_t magenta = rgb565(240, 67, 161);
      display.fillTriangle(x, 94, x - 26, 120, x + 26, 120, magenta);
      display.fillTriangle(x, 148, x - 26, 120, x + 26, 120, magenta);
      display.fillCircle(x, 120, 9, PUPIL);
      for (int n = 0; n < 5; ++n) {
        const int start = 22 + (eyeFrame * 19 + n * 53 + (right ? 11 : 0)) % 150;
        display.fillRect(start, 58 + n * 32, 24, 2, n % 2 ? rgb565(69, 228, 232) : magenta);
      }
      break;
    }
  }
}

void centerText(Adafruit_GFX& display, const char* text, int y, uint8_t size, uint16_t color) {
  const int textWidth = static_cast<int>(strlen(text)) * 6 * size;  // Default GFX font
  display.setTextSize(size);
  display.setTextColor(color);
  display.setCursor((TFT_WIDTH - textWidth) / 2, y);
  display.print(text);
}

void drawClockValue(Adafruit_GFX& display, bool rightPanel) {
  const char* value = rightPanel ? clockDate : clockTime;
  display.fillRect(0, 96, TFT_WIDTH, 48, BACKGROUND);
  centerText(display, value, 106, rightPanel ? 3 : 4, ST77XX_WHITE);
}

void drawAlarm(Adafruit_GFX& display, bool rightPanel) {
  const uint16_t minutes = editingAlarm ? draftMinute : alarmMinute;
  const bool enabled = editingAlarm ? draftEnabled : alarmEnabled;
  display.fillScreen(BACKGROUND);
  if (rightPanel) {
    centerText(display, editingAlarm && alarmField == AlarmField::Enabled ? "[ ALARM ]" : "ALARM",
               36, 2, HIGHLIGHT);
    centerText(display, enabled ? "ON" : "OFF", 94, 4, ST77XX_WHITE);
    centerText(display, lastClockSecond < 0 ? "Waiting for time" : "Daily / UTC+7", 152, 2, HIGHLIGHT);
    centerText(display, editingAlarm ? "Hold: save" : "Hold: edit", 192, 2, HIGHLIGHT);
  } else {
    char text[6];
    const unsigned hour = minutes / 60;
    const unsigned minute = minutes % 60;
    snprintf(text, sizeof(text), "%02u:%02u", hour, minute);
    centerText(display, editingAlarm ? "SET ALARM" : "WAKE TIME", 36, 2, HIGHLIGHT);
    centerText(display, text, 94, 5, ST77XX_WHITE);
    if (editingAlarm && alarmField != AlarmField::Enabled) {
      display.fillRect(alarmField == AlarmField::Hour ? 45 : 135, 139, 54, 3, HIGHLIGHT);
    }
    centerText(display, editingAlarm ? "Tap: next field" : "24-hour clock", 192, 2, HIGHLIGHT);
  }
}

void drawPage(Adafruit_GFX& display, Page page, bool rightPanel) {
  switch (page) {
    case Page::Eyes:
      drawStyledEye(display, rightPanel);
      break;
    case Page::Clock:
      display.fillScreen(BACKGROUND);
      centerText(display, rightPanel ? "DATE" : "THAILAND TIME", 42, 2, HIGHLIGHT);
      drawClockValue(display, rightPanel);
      if (!rightPanel) {
        centerText(display, "UTC+7", 168, 2, HIGHLIGHT);
      }
      break;
    case Page::Alarm:
      drawAlarm(display, rightPanel);
      break;
  }
}

void drawCurrentPage() {
  drawPage(tft1, currentPage, false);
  drawPage(tft2, currentPage, true);
}

void drawEyeFrame() {
  SlideCanvas& canvas = slideCanvas;
  if (eyeStyle == 0) {
    canvas.fillScreen(EYE_SHADOW);
    drawEyeRim(canvas, 143);
    drawIris(canvas, 143, eyeGlow());
    canvas.sendSlice(tft1, 93, 93, 101, 86, 101);
    canvas.sendSlice(tft2, 93, 46, 101, 86, 101);
    return;
  }

  // Only transfer pixels that can change within each animation.
  constexpr int BOXES[][4] = {
      {28, 36, 185, 169},  // Ion Hunter
      {36, 36, 169, 169},  // Void Eclipse
      {112, 104, 42, 34},  // Kitsune Ember
      {32, 32, 177, 177},  // Ghost Sonar
      {20, 55, 196, 135},  // Glitch Hex
  };
  const int* box = BOXES[eyeStyle - 1];
  drawStyledEye(canvas, false);
  canvas.sendSlice(tft1, box[0], box[0], box[2], box[1], box[3]);
  const int rightX = eyeStyle == 3 ? TFT_WIDTH - box[0] - box[2] : box[0];
  drawStyledEye(canvas, true);
  canvas.sendSlice(tft2, rightX, rightX, box[2], box[1], box[3]);
}

void drawSlideFrame(int offset) {
  // Treat the pair of panels as one 480-pixel viewport.
  for (int slot = 0; slot < 4; ++slot) {
    const int incomingShift = slot >= 2 && slideDirection < 0 ? -4 * TFT_WIDTH : 0;
    const int left = slot * TFT_WIDTH + incomingShift - slideDirection * offset;
    const int right = left + TFT_WIDTH;
    if (right <= 0 || left >= 2 * TFT_WIDTH) {
      continue;
    }
    const SlideSnapshot* snapshot = &slideFrames[slot];
    if (snapshot->runs == nullptr) {
      drawPage(slideCanvas, slot < 2 ? currentPage : incomingPage, (slot % 2) != 0);
      snapshot = nullptr;
    }
    for (int panel = 0; panel < 2; ++panel) {
      const int panelLeft = panel * TFT_WIDTH;
      const int panelRight = panelLeft + TFT_WIDTH;
      const int start = max(left, panelLeft);
      const int end = min(right, panelRight);
      if (end > start) {
        slideCanvas.sendSlice(panel == 0 ? tft1 : tft2, start - left, start - panelLeft,
                              end - start, 0, TFT_HEIGHT, snapshot);
      }
    }
  }
}

int slideOffset(uint32_t elapsed) {
  const float progress = static_cast<float>(elapsed) / SLIDE_MS;
  const float eased = progress * progress * (3.0f - 2.0f * progress);
  return static_cast<int>(2 * TFT_WIDTH * eased + 0.5f);
}

void startPageMove(int direction) {
  if (sliding) {
    return;
  }
  constexpr int PAGE_COUNT = 3;
  incomingPage = static_cast<Page>((static_cast<int>(currentPage) + direction + PAGE_COUNT) % PAGE_COUNT);
  clockAtSlideStart = lastClockSecond;
  for (int slot = 0; slot < 4; ++slot) {
    drawPage(slideCanvas, slot < 2 ? currentPage : incomingPage, (slot % 2) != 0);
    slideCanvas.cache(slideFrames[slot]);
  }
  slideDirection = direction;
  sliding = true;
  slideStartedAt = millis();
  lastSlideFrameAt = slideStartedAt;
}

void showAlarmAlert(uint32_t now) {
  alarmRinging = true;
  alarmStartedAt = now;
  sliding = false;
  clearSlideCache();
  alarmPlayer.volume(18);
  alarmPlayer.loop(1);
  tft1.fillScreen(BACKGROUND);
  tft2.fillScreen(BACKGROUND);
  centerText(tft1, "WAKE UP!", 108, 4, HIGHLIGHT);
  centerText(tft2, "Tap to dismiss", 112, 2, ST77XX_WHITE);
}

void dismissAlarm() {
  alarmPlayer.stop();
  alarmRinging = false;
  drawCurrentPage();
}

void handleShortTouch() {
  if (alarmRinging) {
    dismissAlarm();
  } else if (editingAlarm) {
    alarmField = static_cast<AlarmField>((static_cast<int>(alarmField) + 1) % 3);
    drawCurrentPage();
  } else {
    startPageMove(1);
  }
}

void handleLongTouch() {
  if (alarmRinging) {
    dismissAlarm();
  } else if (!sliding && currentPage == Page::Eyes) {
    selectEyes((eyeStyle + 1) % EYE_COUNT);
    Serial.printf("Eye: %s\n", EYE_NAMES[eyeStyle]);
  } else if (!sliding && currentPage == Page::Alarm) {
    if (editingAlarm) {
      if (!setDailyAlarm(draftMinute, draftEnabled)) {
        Serial.println("Could not save alarm to flash.");
      }
    } else {
      draftMinute = alarmMinute;
      draftEnabled = alarmEnabled;
      alarmField = AlarmField::Hour;
    }
    editingAlarm = !editingAlarm;
    drawCurrentPage();
  }
}

void updateTouch(uint32_t now) {
  const bool touched = digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_LEVEL;
  if (touched != candidateTouched) {
    candidateTouched = touched;
    candidateSince = now;
  }
  if (candidateTouched != stableTouched && now - candidateSince >= TOUCH_STABLE_MS) {
    stableTouched = candidateTouched;
    if (stableTouched) {
      touchedAt = candidateSince;
      longHandled = false;
    } else if (!longHandled) {
      if (candidateSince - touchedAt >= LONG_TOUCH_MS) {
        handleLongTouch();
      } else {
        handleShortTouch();
      }
    }
  }
  if (stableTouched && candidateTouched && !longHandled && now - touchedAt >= LONG_TOUCH_MS) {
    longHandled = true;
    handleLongTouch();
  }
}

bool buttonPressed(Button& button, uint32_t now, bool allowRepeat) {
  const bool pressed = digitalRead(button.pin) == LOW;
  if (pressed != button.candidate) {
    button.candidate = pressed;
    button.changedAt = now;
  }
  if (button.pressed != button.candidate && now - button.changedAt >= TOUCH_STABLE_MS) {
    button.pressed = button.candidate;
    button.repeating = button.pressed && allowRepeat;
    button.pressedAt = now;
    button.repeatAt = now + BUTTON_REPEAT_DELAY_MS;
    return button.pressed;
  }
  if (!allowRepeat) {
    button.repeating = false;
  }
  if (button.repeating && button.candidate && static_cast<int32_t>(now - button.repeatAt) >= 0) {
    button.repeatAt = now + (now - button.pressedAt >= BUTTON_REPEAT_FAST_AFTER_MS ? 55 : 120);
    return true;
  }
  return false;
}

void updateButtons(uint32_t now) {
  const bool repeat = editingAlarm && !alarmRinging && alarmField != AlarmField::Enabled;
  const bool left = buttonPressed(leftButton, now, repeat);
  const bool right = buttonPressed(rightButton, now, repeat);
  if (left == right) {
    return;
  }
  if (alarmRinging) {
    dismissAlarm();
    return;
  }
  const int direction = right ? 1 : -1;
  if (!editingAlarm) {
    startPageMove(direction);
    return;
  }
  switch (alarmField) {
    case AlarmField::Hour:
      draftMinute = (draftMinute + MINUTES_PER_DAY + direction * 60) % MINUTES_PER_DAY;
      break;
    case AlarmField::Minute: {
      const int minute = (draftMinute % 60 + 60 + direction) % 60;
      draftMinute = draftMinute / 60 * 60 + minute;
      break;
    }
    case AlarmField::Enabled:
      draftEnabled = !draftEnabled;
      break;
  }
  if (alarmField == AlarmField::Enabled) {
    drawAlarm(tft2, true);
  } else {
    drawAlarm(tft1, false);
  }
}

void updateClock() {
  const time_t now = time(nullptr);
  if (now < FIRST_VALID_TIME || now == lastClockSecond) {
    return;
  }
  struct tm localTime{};
  if (!localtime_r(&now, &localTime)) {
    return;
  }
  const bool firstSync = lastClockSecond == -1;
  lastClockSecond = now;
  strftime(clockTime, sizeof(clockTime), "%H:%M:%S", &localTime);
  strftime(clockDate, sizeof(clockDate), "%Y/%m/%d", &localTime);
  const uint32_t day = static_cast<uint32_t>((now + 7 * 3600) / 86400);
  if (alarmEnabled && !alarmRinging && day != lastAlarmDay &&
      localTime.tm_hour * 60 + localTime.tm_min == alarmMinute) {
    lastAlarmDay = day;
    showAlarmAlert(millis());
  } else if (!alarmRinging && !sliding && currentPage == Page::Clock) {
    drawClockValue(tft1, false);
    drawClockValue(tft2, true);
  } else if (firstSync && !alarmRinging && !sliding && currentPage == Page::Alarm) {
    drawCurrentPage();
  }
}

}  // namespace

TouchPageStatus touchPageStatus() {
  return {alarmMinute, alarmEnabled, alarmRinging, eyeStyle};
}

bool setDailyAlarm(uint16_t minutes, bool enabled) {
  if (minutes >= MINUTES_PER_DAY) return false;
  if (minutes != alarmMinute || enabled != alarmEnabled) {
    const uint16_t value = minutes | (enabled ? ALARM_ENABLED_BIT : 0);
    if (!settingsReady || preferences.putUShort("daily", value) != sizeof(value)) return false;
    alarmMinute = minutes;
    alarmEnabled = enabled;
  }
  if (!enabled && alarmRinging) dismissAlarm();
  if (!alarmRinging && !sliding && currentPage == Page::Alarm) drawCurrentPage();
  return true;
}

bool selectEyes(uint8_t style) {
  if (style >= EYE_COUNT || alarmRinging) return false;
  eyeStyle = style;
  eyeStartedAt = millis();
  eyeElapsed = 0;
  lastPulseAt = eyeStartedAt;
  editingAlarm = false;
  sliding = false;
  clearSlideCache();
  currentPage = Page::Eyes;
  drawCurrentPage();
  return true;
}

void enterTouchPages() {
  pinMode(TOUCH_PIN, INPUT);
  pinMode(LEFT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RIGHT_BUTTON_PIN, INPUT_PULLUP);
  settingsReady = preferences.begin("pawbo-alarm", false);
  if (settingsReady) {
    const uint16_t saved = preferences.getUShort("daily", alarmMinute);
    const uint16_t minutes = saved & ~ALARM_ENABLED_BIT;
    if (minutes < MINUTES_PER_DAY) {
      alarmMinute = minutes;
      alarmEnabled = (saved & ALARM_ENABLED_BIT) != 0;
    }
  }
  alarmSerial.begin(9600, SERIAL_8N1, ALARM_RX_PIN, ALARM_TX_PIN);
  alarmPlayer.begin(alarmSerial, false, false);
  touchReadyAt = millis() + TOUCH_STARTUP_MS;
  stableTouched = digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_LEVEL;
  candidateTouched = stableTouched;
  candidateSince = millis();
  longHandled = stableTouched;
  selectEyes(eyeStyle);
}

void tickTouchPages() {
  updateClock();
  const uint32_t inputTime = millis();
  if (static_cast<int32_t>(inputTime - touchReadyAt) >= 0) updateTouch(inputTime);
  updateButtons(millis());
  const uint32_t now = millis();
  if (alarmRinging) {
    if (millis() - alarmStartedAt >= ALARM_RING_MS) {
      dismissAlarm();
    }
  } else if (sliding) {
    const uint32_t elapsed = now - slideStartedAt;
    if (elapsed >= SLIDE_MS) {
      drawSlideFrame(2 * TFT_WIDTH);
      clearSlideCache();
      currentPage = incomingPage;
      sliding = false;
      lastPulseAt = millis();
      if (currentPage == Page::Clock && clockAtSlideStart != lastClockSecond) {
        drawClockValue(tft1, false);
        drawClockValue(tft2, true);
      } else if (currentPage == Page::Alarm && clockAtSlideStart < 0 && lastClockSecond >= 0) {
        drawAlarm(tft2, true);
      }
    } else if (now - lastSlideFrameAt >= SLIDE_FRAME_MS) {
      lastSlideFrameAt = now;
      drawSlideFrame(slideOffset(elapsed));
    }
  } else if (currentPage == Page::Eyes &&
             now - lastPulseAt >= (eyeStyle == 0 || eyeStyle == 3 ? SMALL_EYE_FRAME_MS : LARGE_EYE_FRAME_MS)) {
    lastPulseAt = now;
    eyeElapsed = now - eyeStartedAt;
    drawEyeFrame();
  }
}
