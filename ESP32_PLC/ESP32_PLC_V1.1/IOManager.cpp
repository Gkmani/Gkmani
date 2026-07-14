#include "IOManager.h"

using namespace IOManager;

void IOManager::begin() {
  pinMode(DI1, INPUT);
  pinMode(DI2, INPUT);
  pinMode(DO1, OUTPUT);
  pinMode(DO2, OUTPUT);

  // Setup PWM channel for analog output if needed
  ledcSetup(0, 5000, 8);
  ledcAttachPin(DO2, 0); // optional: use DO2 as PWM/dac output
  digitalWrite(DO1, LOW);
  digitalWrite(DO2, LOW);
}

void IOManager::update() {
  // Currently, IOManager doesn't need periodic polling beyond read/write operations.
  // Placeholder for future debouncing or input conditioning.
}

bool IOManager::readDigital(uint8_t pin) {
  return digitalRead(pin);
}

void IOManager::writeDigital(uint8_t pin, bool value) {
  digitalWrite(pin, value ? HIGH : LOW);
}

int IOManager::readAnalog(uint8_t pin) {
  return analogRead(pin); // 0-4095
}

void IOManager::writeAnalog(uint8_t pin, int value) {
  // value expected 0-255 for 8-bit PWM
  ledcWrite(0, value);
}
