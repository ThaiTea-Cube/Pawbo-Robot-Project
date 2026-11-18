#pragma once

#include <stdint.h>

struct TouchPageStatus {
  uint16_t alarmMinute;
  bool alarmEnabled;
  bool alarmRinging;
  uint8_t eyeStyle;
};

void enterTouchPages();
void tickTouchPages();
TouchPageStatus touchPageStatus();
bool setDailyAlarm(uint16_t minutes, bool enabled);
bool selectEyes(uint8_t style);
