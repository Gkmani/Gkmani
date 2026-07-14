#include <Arduino.h>
#include "IOManager.h"
#include "WebServerModule.h"
#include "ModbusModule.h"
#include "SDCardManager.h"
#include "LogicEngine.h"

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("ESP32 PLC starting...");
  IOManager::begin();
  SDCardManager::begin();
  WebServerModule::begin();
  ModbusModule::begin();
  LogicEngine::begin();
  Serial.println("Setup complete.");
}

void loop() {
  IOManager::update();
  LogicEngine::update(); // Executes currently loaded script (ladder or mini-py)
  ModbusModule::update();
  WebServerModule::update();
  delay(10); // 10 ms main cycle
}
