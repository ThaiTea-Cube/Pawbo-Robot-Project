#include "ServoControl.h"

#include <Arduino.h>
#include <Adafruit_PWMServoDriver.h>
#include <Wire.h>
#include <WiFi.h>
#include <atomic>
#include <mutex>

namespace ServoControl {
namespace {
constexpr int ADDRESS = 0x40;
constexpr int FREQUENCY = 50;
constexpr uint16_t FULL_OFF = 4096;
struct JointLimits { int minimum; int maximum; };
// Bench limits. Calibrate each joint with its linkage before widening any range.
constexpr JointLimits LIMITS[] = {
  {75, 105}, {75, 105}, {75, 105}, {75, 105},
  {75, 105}, {75, 105}, {75, 105}, {75, 105}
};
constexpr int COUNT = sizeof(LIMITS) / sizeof(LIMITS[0]);
Adafruit_PWMServoDriver pwm(ADDRESS, Wire);
bool initialized = false;
bool healthy = false;
int channel = -1;
int angle = -1;
uint32_t deadline = 0;
uint32_t lastOffAttempt = 0;
std::recursive_mutex lock;
std::atomic<bool> brokerConnected{false};

bool disableAll() {
  bool result = true;
  for (int i = 0; i < COUNT; ++i) {
    if (pwm.setPWM(i, 0, FULL_OFF) != 0) result = false;
  }
  return result;
}

void watchdog(void*) {
  for (;;) {
    {
      const std::lock_guard<std::recursive_mutex> guard(lock);
      const uint32_t now = millis();
      const bool connected = brokerConnected.load() && WiFi.status() == WL_CONNECTED;
      if (channel >= 0 && (!connected || !healthy || static_cast<int32_t>(now - deadline) >= 0) &&
          now - lastOffAttempt >= 25) stop();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
}

void begin() {
  if (!Wire.begin(21, 22, 100000)) return;
  Wire.setTimeOut(20);
  if (!pwm.begin()) {
    Serial.println("[Servo] PCA9685 missing at 0x40");
    return;
  }
  initialized = true;
  pwm.setPWMFreq(FREQUENCY);
  Wire.beginTransmission(ADDRESS);
  Wire.write(PCA9685_MODE2);
  Wire.write(MODE2_OUTDRV);
  const bool modeSet = Wire.endTransmission() == 0;
  healthy = disableAll() && modeSet;
  if (healthy && xTaskCreate(watchdog, "servo-watch", 3072, nullptr, 2, nullptr) != pdPASS) {
    healthy = false;
  }
  Serial.println(healthy ? "[Servo] PCA9685 ready; outputs off" : "[Servo] Driver initialization failed");
}

bool withinRange(int servo, int degrees) {
  return servo >= 1 && servo <= COUNT &&
         degrees >= LIMITS[servo - 1].minimum && degrees <= LIMITS[servo - 1].maximum;
}

bool stop() {
  const std::lock_guard<std::recursive_mutex> guard(lock);
  if (!initialized) return false;
  if (healthy && channel < 0) return true;
  lastOffAttempt = millis();
  const bool off = healthy ? pwm.setPWM(channel, 0, FULL_OFF) == 0 : disableAll();
  if (!off) {
    healthy = false;
    Serial.println("[Servo] Could not confirm PWM off");
    return false;
  }
  channel = -1;
  angle = -1;
  return true;
}

bool move(int servo, int degrees, uint32_t durationMs) {
  const std::lock_guard<std::recursive_mutex> guard(lock);
  if (!healthy || !brokerConnected.load() || WiFi.status() != WL_CONNECTED ||
      !withinRange(servo, degrees) || durationMs < 100 || durationMs > 750) return false;
  if (channel >= 0 && channel != servo - 1 && !stop()) return false;
  // Same 1000-2000 us pulse convention as the module test; UI degrees are nominal.
  const uint32_t pulse = 1000 + (degrees * 1000 + 90) / 180;
  const uint16_t ticks = (pulse * FREQUENCY * 4096 + 500000) / 1000000;
  channel = servo - 1;
  if (pwm.setPWM(channel, 0, ticks) != 0) {
    healthy = false;
    stop();
    return false;
  }
  angle = degrees;
  deadline = millis() + durationMs;
  return true;
}

void setBrokerConnected(bool connected) { brokerConnected.store(connected); }

Status status() {
  const std::lock_guard<std::recursive_mutex> guard(lock);
  return {healthy, channel + 1, angle};
}
}
