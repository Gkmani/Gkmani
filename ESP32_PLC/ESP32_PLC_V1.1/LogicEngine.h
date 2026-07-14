#pragma once
#include <Arduino.h>

namespace LogicEngine {
  enum ScriptType { NONE=0, LADDER=1, MINIPY=2, LUA=3 };
  void begin();
  void update();
  bool loadScript(const char* path, ScriptType type);
  ScriptType currentType();
  String currentScriptText();
}
