#include "SDCardManager.h"
#include <SD.h>
#include <SPI.h>

namespace SDCardManager {
  const char* LOGIC_PATH = "/logic.txt";

  void begin() {
    if (!SD.begin(5)) {
      Serial.println("SD mount failed. Ensure CS pin 5 or adjust in code.");
      return;
    }
    Serial.println("SD mounted.");
    if (!fileExists(LOGIC_PATH)) {
      // create default logic file
      saveFile(LOGIC_PATH, (const uint8_t*)"# default ladder logic\nLD DI1\nOUT DO1\n",  strlen("# default ladder logic\nLD DI1\nOUT DO1\n"));
    }
  }

  bool saveFile(const char* path, const uint8_t* data, size_t len) {
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    f.write(data, len);
    f.close();
    return true;
  }

  bool deleteFile(const char* path) {
    return SD.remove(path);
  }

  String readFileString(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return String();
    String s;
    while (f.available()) s += (char)f.read();
    f.close();
    return s;
  }

  bool fileExists(const char* path) {
    return SD.exists(path);
  }
}
