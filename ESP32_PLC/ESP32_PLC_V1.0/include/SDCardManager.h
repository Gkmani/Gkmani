#pragma once
#include <Arduino.h>
#include <FS.h>

namespace SDCardManager {
  void begin();
  bool saveFile(const char* path, const uint8_t* data, size_t len);
  bool deleteFile(const char* path);
  String readFileString(const char* path);
  bool fileExists(const char* path);
  const char* LOGIC_PATH; // default path to logic file
}
