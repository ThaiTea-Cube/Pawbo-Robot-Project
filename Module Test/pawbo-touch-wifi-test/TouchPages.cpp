#include "TouchPages.h"

#include <Adafruit_ST7789.h>
#include <cstring>
#include <new>
#include <time.h>

extern Adafruit_ST7789 tft1;
extern Adafruit_ST7789 tft2;

namespace {
constexpr int TFT_WIDTH = 240;
constexpr int TFT_HEIGHT = 240;
constexpr uint32_t PULSE_INTERVAL_MS = 180;
constexpr uint8_t PULSE_LEVELS[] = {155, 161, 172, 188, 209, 230, 248, 255,
                                    248, 230, 209, 188, 172, 161};
constexpr uint8_t PULSE_STEPS = sizeof(PULSE_LEVELS) / sizeof(PULSE_LEVELS[0]);

enum class Page { Spider, Clock, Hello };

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
uint8_t pulse = 0;

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

void drawFace(Adafruit_GFX& display, bool mirror) {
  display.fillScreen(BACKGROUND);
  drawWeb(display, mirror);
  drawSmallEye(display, mirrorX(50, mirror), 60, 15);
  drawSmallEye(display, mirrorX(113, mirror), 42, 18);
  drawSmallEye(display, mirrorX(40, mirror), 132, 12);

  const int x = mirrorX(143, mirror);
  display.fillCircle(x, 136, 72, EYE_SHADOW);
  display.drawCircle(x, 136, 68, EYE_RIM);
  display.fillCircle(x, 136, 62, EYE_RIM);
  display.fillCircle(x, 136, 56, PUPIL);
}

void drawIris(Adafruit_GFX& display, bool mirror, uint8_t glow) {
  const int x = mirrorX(143, mirror);
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
constexpr int TOUCH_ACTIVE_LEVEL = HIGH;
constexpr uint32_t TOUCH_STABLE_MS = 40;
constexpr uint32_t SLIDE_MS = 640;
constexpr uint32_t SLIDE_FRAME_MS = 20;

Page currentPage = Page::Spider;
Page incomingPage = Page::Spider;
bool sliding = false;
uint32_t slideStartedAt = 0;
uint32_t lastSlideFrameAt = 0;
bool stableTouched = false;
bool candidateTouched = false;
uint32_t candidateSince = 0;
time_t lastClockSecond = -1;
char clockTime[9] = "--:--:--";
char clockDate[11] = "----/--/--";

// A panel of palette indices preserves the RGB565 colors using one byte per pixel.
class SlideCanvas : public Adafruit_GFX {
 public:
  SlideCanvas() : Adafruit_GFX(TFT_WIDTH, TFT_HEIGHT) {}

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x >= 0 && x < TFT_WIDTH && y >= 0 && y < TFT_HEIGHT) {
      pixels[y][x] = findColor(color);
    }
  }

  void fillScreen(uint16_t color) override {
    palette[0] = color;
    paletteSize = 1;
    lastIndex = 0;
    memset(pixels, 0, sizeof(pixels));
  }

  void sendSlice(Adafruit_ST7789& display, int sourceX, int destX, int width) {
    display.startWrite();
    display.setAddrWindow(destX, 0, width, TFT_HEIGHT);
    for (int y = 0; y < TFT_HEIGHT; y += BATCH_ROWS) {
      const int remaining = TFT_HEIGHT - y;
      const int rows = remaining < BATCH_ROWS ? remaining : BATCH_ROWS;
      uint16_t* output = transfer;
      for (int row = 0; row < rows; ++row) {
        const uint8_t* source = &pixels[y + row][sourceX];
        for (int x = 0; x < width; ++x) {
          *output++ = palette[source[x]];
        }
      }
      display.writePixels(transfer, rows * width);
    }
    display.endWrite();
  }

 private:
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

  static constexpr int BATCH_ROWS = 16;
  static constexpr unsigned PALETTE_CAPACITY = 256;
  uint16_t palette[PALETTE_CAPACITY]{};
  unsigned paletteSize = 1;
  uint8_t lastIndex = 0;
  uint8_t pixels[TFT_HEIGHT][TFT_WIDTH];
  uint16_t transfer[TFT_WIDTH * BATCH_ROWS];
};

SlideCanvas* slideCanvas = nullptr;

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

void drawPage(Adafruit_GFX& display, Page page, bool rightPanel) {
  switch (page) {
    case Page::Spider:
      drawFace(display, rightPanel);
      drawIris(display, rightPanel, PULSE_LEVELS[pulse]);
      break;
    case Page::Clock:
      display.fillScreen(BACKGROUND);
      centerText(display, rightPanel ? "DATE" : "THAILAND TIME", 42, 2, HIGHLIGHT);
      drawClockValue(display, rightPanel);
      centerText(display, rightPanel ? "Royal Thai Navy NTP" : "UTC+7", 168, 2, HIGHLIGHT);
      break;
    case Page::Hello:
      display.fillScreen(BACKGROUND);
      centerText(display, rightPanel ? "world" : "hello", 100, 5, HIGHLIGHT);
      break;
  }
}

void drawCurrentPage() {
  drawPage(tft1, currentPage, false);
  drawPage(tft2, currentPage, true);
}

void drawSlideFrame(int offset) {
  // Treat the pair of panels as one 480-pixel viewport.
  for (int slot = 0; slot < 4; ++slot) {
    const int left = slot * TFT_WIDTH - offset;
    const int right = left + TFT_WIDTH;
    if (right <= 0 || left >= 2 * TFT_WIDTH) {
      continue;
    }
    drawPage(*slideCanvas, slot < 2 ? currentPage : incomingPage, (slot % 2) != 0);
    for (int panel = 0; panel < 2; ++panel) {
      const int panelLeft = panel * TFT_WIDTH;
      const int panelRight = panelLeft + TFT_WIDTH;
      const int start = max(left, panelLeft);
      const int end = min(right, panelRight);
      if (end > start) {
        slideCanvas->sendSlice(panel == 0 ? tft1 : tft2, start - left, start - panelLeft, end - start);
      }
    }
  }
}

int slideOffset(uint32_t elapsed) {
  const float progress = static_cast<float>(elapsed) / SLIDE_MS;
  const float eased = progress * progress * (3.0f - 2.0f * progress);
  return static_cast<int>(2 * TFT_WIDTH * eased + 0.5f);
}

void startNextPage(uint32_t now) {
  switch (currentPage) {
    case Page::Spider: incomingPage = Page::Clock; break;
    case Page::Clock: incomingPage = Page::Hello; break;
    case Page::Hello: incomingPage = Page::Spider; break;
  }
  slideCanvas = new (std::nothrow) SlideCanvas;
  if (!slideCanvas) {
    currentPage = incomingPage;
    drawCurrentPage();
    lastPulseAt = millis();
    return;
  }
  sliding = true;
  slideStartedAt = now;
  lastSlideFrameAt = now;
}

void updateTouch(uint32_t now) {
  const bool touched = digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_LEVEL;
  if (touched != candidateTouched) {
    candidateTouched = touched;
    candidateSince = now;
  }
  if (candidateTouched != stableTouched && now - candidateSince >= TOUCH_STABLE_MS) {
    stableTouched = candidateTouched;
    if (stableTouched && !sliding) {
      startNextPage(now);
    }
  }
}

void updateClock(bool synced) {
  if (!synced) {
    return;
  }
  const time_t now = time(nullptr);
  if (now == lastClockSecond) {
    return;
  }
  struct tm localTime{};
  if (!localtime_r(&now, &localTime)) {
    return;
  }
  lastClockSecond = now;
  strftime(clockTime, sizeof(clockTime), "%H:%M:%S", &localTime);
  strftime(clockDate, sizeof(clockDate), "%Y/%m/%d", &localTime);
  if (!sliding && currentPage == Page::Clock) {
    drawClockValue(tft1, false);
    drawClockValue(tft2, true);
  }
}
}  // namespace

void enterTouchPages(bool synced) {
  pinMode(TOUCH_PIN, INPUT);
  stableTouched = digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_LEVEL;
  candidateTouched = stableTouched;
  candidateSince = millis();
  updateClock(synced);
  currentPage = Page::Clock;
  drawCurrentPage();
  lastPulseAt = millis();
}

void tickTouchPages(bool synced) {
  updateClock(synced);
  const uint32_t now = millis();
  updateTouch(now);
  if (sliding) {
    const uint32_t elapsed = now - slideStartedAt;
    if (elapsed >= SLIDE_MS) {
      drawSlideFrame(2 * TFT_WIDTH);
      delete slideCanvas;
      slideCanvas = nullptr;
      currentPage = incomingPage;
      sliding = false;
      lastPulseAt = millis();
    } else if (now - lastSlideFrameAt >= SLIDE_FRAME_MS) {
      lastSlideFrameAt = now;
      drawSlideFrame(slideOffset(elapsed));
    }
  } else if (currentPage == Page::Spider && now - lastPulseAt >= PULSE_INTERVAL_MS) {
    lastPulseAt = now;
    pulse = (pulse + 1) % PULSE_STEPS;
    drawIris(tft1, false, PULSE_LEVELS[pulse]);
    drawIris(tft2, true, PULSE_LEVELS[pulse]);
  }
}
