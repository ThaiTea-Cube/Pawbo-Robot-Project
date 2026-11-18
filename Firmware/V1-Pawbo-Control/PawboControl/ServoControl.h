#pragma once

#include <stdint.h>

namespace ServoControl {
struct Status {
  bool ready;
  int activeServo;
  int targetAngle;
};

void begin();
void setBrokerConnected(bool connected);
bool move(int servo, int angle, uint32_t durationMs);
bool stop();
Status status();
inline bool ready() { return status().ready; }
inline bool active() { return status().activeServo != 0; }
bool withinRange(int servo, int angle);
}
