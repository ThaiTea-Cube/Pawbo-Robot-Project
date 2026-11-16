#include <Arduino.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <cstring>

constexpr int RTC_CLK = 16;
constexpr int RTC_DAT = 17;
constexpr int RTC_CE = 19;  // Labeled RST on DS1302 modules
constexpr uint32_t PRINT_INTERVAL_MS = 1000;

ThreeWire rtcBus(RTC_DAT, RTC_CLK, RTC_CE);
RtcDS1302<ThreeWire> rtc(rtcBus);

uint32_t lastPrintAt = 0;
char command[32];
size_t commandLength = 0;
bool commandTooLong = false;

void printHelp() {
  Serial.println("s YYYY-MM-DD HH:MM:SS = set RTC | ? = show commands");
}

void printTime(const RtcDateTime& time) {
  Serial.printf("%04u-%02u-%02u %02u:%02u:%02u\n",
                unsigned(time.Year()), unsigned(time.Month()), unsigned(time.Day()),
                unsigned(time.Hour()), unsigned(time.Minute()), unsigned(time.Second()));
}

bool validTime(const RtcDateTime& time) {
  return time.Year() <= 2099 && time.IsValid();
}

uint8_t readTwoDigits(const char* digits) {
  return (digits[0] - '0') * 10 + (digits[1] - '0');
}

bool parseTime(const char* text, RtcDateTime& result) {
  constexpr char FORMAT[] = "0000-00-00 00:00:00";
  if (strlen(text) != sizeof(FORMAT) - 1) {
    return false;
  }
  for (size_t i = 0; i < sizeof(FORMAT) - 1; ++i) {
    if (FORMAT[i] == '0' ? (text[i] < '0' || text[i] > '9') : text[i] != FORMAT[i]) {
      return false;
    }
  }

  if (text[0] != '2' || text[1] != '0') {
    return false;
  }

  result = RtcDateTime(2000 + readTwoDigits(text + 2),
                       readTwoDigits(text + 5), readTwoDigits(text + 8),
                       readTwoDigits(text + 11), readTwoDigits(text + 14),
                       readTwoDigits(text + 17));
  return validTime(result);
}

void runCommand(const char* line) {
  if (strcmp(line, "?") == 0) {
    printHelp();
    return;
  }
  RtcDateTime requested;
  if (strncmp(line, "s ", 2) != 0 || !parseTime(line + 2, requested)) {
    Serial.println("Invalid command or date. Use a date in 2000-2099.");
    printHelp();
    return;
  }
  if (rtc.GetIsWriteProtected()) {
    rtc.SetIsWriteProtected(false);
  }
  rtc.SetDateTime(requested);
  const RtcDateTime actual = rtc.GetDateTime();
  if (!validTime(actual) || !rtc.GetIsRunning() ||
      actual.TotalSeconds() - requested.TotalSeconds() > 1) {
    Serial.println("Time set could not be verified. Check VCC, GND, CLK, DAT and RST.");
    return;
  }
  Serial.print("Time set: ");
  printTime(actual);
  lastPrintAt = millis();
}

void readCommands() {
  while (Serial.available()) {
    const int ch = Serial.read();
    if (ch == '\r' || ch == '\n') {
      if (commandTooLong) {
        Serial.println("Command too long.");
      } else if (commandLength != 0) {
        command[commandLength] = '\0';
        runCommand(command);
      }
      commandLength = 0;
      commandTooLong = false;
    } else if (commandLength < sizeof(command) - 1) {
      command[commandLength++] = ch;
    } else {
      commandTooLong = true;
    }
  }
}

void setup() {
  Serial.begin(115200);
  rtc.Begin();
  Serial.println("DS1302: CLK=GPIO16, DAT=GPIO17, RST=GPIO19.");
  printHelp();
}

void loop() {
  readCommands();
  const uint32_t now = millis();
  if (now - lastPrintAt >= PRINT_INTERVAL_MS) {
    lastPrintAt = now;
    const RtcDateTime time = rtc.GetDateTime();
    if (!validTime(time)) {
      Serial.println("Invalid RTC time. Check wiring or set the time.");
    } else if (!rtc.GetIsRunning()) {
      Serial.println("RTC clock stopped. Set the time to start it.");
    } else {
      printTime(time);
    }
  }
  delay(5);
}
