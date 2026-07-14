#pragma once
#include <Arduino.h>

namespace WebServerModule {
  void begin();
  void update();
  bool authenticate(const String& user, const String& pass);
}
