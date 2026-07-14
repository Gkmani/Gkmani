#include "ModbusModule.h"
#include <ModbusRTU.h>
#include "IOManager.h"

static ModbusRTU mb;

namespace ModbusModule {

void begin() {
  Serial2.begin(9600, SERIAL_8N1, 16, 17); // RX,TX pins for RS485 (adjust)
  mb.begin(&Serial2);
  mb.slave(1);
  mb.addHreg(0); // holding register 0 -> ADC
  Serial.println("Modbus RTU started");
}

void update() {
  mb.task();
  // update register0 with ADC reading
  mb.Hreg(0, IOManager::readAnalog(IOManager::ADC1) >> 2); // scale 0-4095 -> 0-1023
}
} // namespace
