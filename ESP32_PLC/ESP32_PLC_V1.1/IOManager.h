#pragma once
#include <Arduino.h>

namespace IOManager {
  void begin();
  void update();
  bool readDigital(uint8_t pin);
  void writeDigital(uint8_t pin, bool value);
  int readAnalog(uint8_t pin);
  void writeAnalog(uint8_t pin, int value);
  // Pin definitions (user-facing names)
  constexpr uint8_t DI1 = 34;
  constexpr uint8_t DI2 = 35;
  constexpr uint8_t DO1 = 25;
  constexpr uint8_t DO2 = 26;
  constexpr uint8_t ADC1 = 36; // analog input
}
