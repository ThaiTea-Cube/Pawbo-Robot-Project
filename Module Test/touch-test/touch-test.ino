#include <Arduino.h>

constexpr int TOUCH_PIN = 35;  // Massmore IO7, signal pin
constexpr int TOUCH_ACTIVE_LEVEL = HIGH;
constexpr uint32_t SENSOR_STARTUP_MS = 700;
constexpr uint32_t STABLE_TIME_MS = 40;

bool candidateTouched = false;
bool stableTouched = false;
uint32_t candidateSince = 0;
uint32_t touchCount = 0;

bool readTouched() {
  return digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_LEVEL;
}

void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, INPUT);
  Serial.println("Touch test on GPIO35. Keep your finger off the pad during startup.");
  delay(SENSOR_STARTUP_MS);

  stableTouched = readTouched();
  candidateTouched = stableTouched;
  candidateSince = millis();
  Serial.printf("Ready. Initial state: %s\n", stableTouched ? "TOUCHED" : "RELEASED");
}

void loop() {
  const uint32_t now = millis();
  const bool touched = readTouched();
  if (touched != candidateTouched) {
    candidateTouched = touched;
    candidateSince = now;
  }

  if (candidateTouched != stableTouched && now - candidateSince >= STABLE_TIME_MS) {
    stableTouched = candidateTouched;
    if (stableTouched) {
      ++touchCount;
    }
    Serial.printf("%s | Touch count: %lu\n",
                  stableTouched ? "TOUCH" : "RELEASE", static_cast<unsigned long>(touchCount));
  }
  delay(5);
}
