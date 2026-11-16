#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

constexpr int SDA_PIN = 21;
constexpr int SCL_PIN = 22;
constexpr uint8_t PCA_ADDRESS = 0x40;
constexpr uint8_t SERVO_COUNT = 8;
constexpr uint16_t PWM_HZ = 50;
constexpr uint16_t PWM_OFF = 4096;  // PCA9685 full-off flag
constexpr uint32_t STEP_MS = 800;
constexpr uint16_t PULSES_US[] = {1500, 1000, 2000, 1500};
constexpr uint8_t STEP_COUNT = sizeof(PULSES_US) / sizeof(PULSES_US[0]);

Adafruit_PWMServoDriver pwm(PCA_ADDRESS, Wire);
bool ready = false;
bool running = false;
uint8_t channel = 0;
uint8_t lastChannel = 0;
uint8_t step = 0;
uint32_t nextStepAt = 0;

void printHelp() {
  Serial.println("0-7: test one channel | a: test all | x: stop PWM | ?: help");
}

bool disableOutputs() {
  bool ok = true;
  for (uint8_t i = 0; i < SERVO_COUNT; ++i) {
    if (pwm.setPWM(i, 0, PWM_OFF) != 0) {
      Serial.printf("CH%u: failed to turn PWM off (I2C error)\n", unsigned(i));
      ok = false;
    }
  }
  return ok;
}

void abortOnWriteError() {
  running = false;
  ready = false;
  Serial.printf("CH%u: I2C write failed. Test aborted.\n", unsigned(channel));
  disableOutputs();
  Serial.println("Check wiring and reset ESP32 before retrying.");
}

void startTest(uint8_t first, uint8_t last) {
  channel = first;
  lastChannel = last;
  step = 0;
  nextStepAt = millis();
  running = true;
}

void serviceTest() {
  if (!running || static_cast<int32_t>(millis() - nextStepAt) < 0) {
    return;
  }

  if (step < STEP_COUNT) {
    uint16_t pulse = PULSES_US[step];
    uint16_t ticks = (uint32_t(pulse) * PWM_HZ * 4096 + 500000) / 1000000;
    if (pwm.setPWM(channel, 0, ticks) != 0) {
      abortOnWriteError();
      return;
    }
    Serial.printf("CH%u: %u us\n", unsigned(channel), unsigned(pulse));
    ++step;
    nextStepAt = millis() + STEP_MS;
    return;
  }

  if (pwm.setPWM(channel, 0, PWM_OFF) != 0) {
    abortOnWriteError();
    return;
  }
  Serial.printf("CH%u: sequence complete. Verify movement visually.\n", unsigned(channel));
  if (channel == lastChannel) {
    running = false;
    Serial.println("Test complete.");
  } else {
    ++channel;
    step = 0;
    nextStepAt = millis() + STEP_MS;
  }
}

void setup() {
  Serial.begin(115200);
  if (!Wire.begin(SDA_PIN, SCL_PIN, 100000)) {
    Serial.println("I2C initialization failed. Check ESP32 pin settings.");
    return;
  }
  if (!pwm.begin()) {
    Serial.printf("PCA9685 not responding at 0x%02X. Check VCC, GND, SDA, SCL and address.\n",
                  unsigned(PCA_ADDRESS));
    return;
  }
  pwm.setPWMFreq(PWM_HZ);
  ready = disableOutputs();
  if (ready) {
    Serial.println("PCA9685 ready. Waiting for a test command.");
    printHelp();
  }
}

void loop() {
  if (Serial.available()) {
    int command = Serial.read();
    if (command == '?') {
      printHelp();
    } else if (command == 'x' || command == 'X') {
      running = false;
      if (ready && disableOutputs()) {
        Serial.println("PWM stopped on CH0-CH7.");
      } else {
        ready = false;
        Serial.println("PWM off could not be confirmed. Disconnect servo power and check wiring.");
      }
    } else if ((command >= '0' && command <= '7') || command == 'a' || command == 'A') {
      if (!ready) {
        Serial.println("PCA9685 unavailable. Check wiring and reset ESP32.");
      } else if (running) {
        Serial.println("Test running. Send x to stop.");
      } else if (command >= '0' && command <= '7') {
        startTest(command - '0', command - '0');
      } else {
        startTest(0, SERVO_COUNT - 1);
      }
    } else if (command != '\r' && command != '\n' && command != ' ') {
      printHelp();
    }
  }
  serviceTest();
}
