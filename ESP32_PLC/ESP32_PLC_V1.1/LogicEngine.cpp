#include "LogicEngine.h"
#include "SDCardManager.h"
#include "IOManager.h"

using namespace LogicEngine;

static ScriptType _type = NONE;
static String _script;

void LogicEngine::begin() {
  // Try to auto-load default ladder script from SD
  String s = SDCardManager::readFileString(SDCardManager::LOGIC_PATH);
  if (s.length()) {
    loadScript(SDCardManager::LOGIC_PATH, LADDER);
  }
}

ScriptType LogicEngine::currentType() { return _type; }
String LogicEngine::currentScriptText() { return _script; }

// ---------- Ladder interpreter (text-based simple format) ----------
// Format example:
// # comments start with #
// LD DI1       ; load contact DI1
// AND DI2      ; logical AND with DI2
// OUT DO1      ; set output DO1 if result true
//
// Supports: LD, AND, OR, OUT, SET, RST

static bool evalLadder(String script) {
  // simple line-based parser, one rung per sequential block separated by blank lines
  // For each rung evaluate boolean accumulator and execute OUT if true
  std::vector<String> lines;
  {
    int start = 0;
    while (start < script.length()) {
      int eol = script.indexOf('\n', start);
      if (eol == -1) eol = script.length();
      String ln = script.substring(start, eol);
      ln.trim();
      start = eol + 1;
      if (ln.length()==0) {
        // blank line denotes end of rung - process later; store empty marker
        lines.push_back(String(""));
      } else if (ln.startsWith("#")) {
        continue;
      } else {
        lines.push_back(ln);
      }
    }
  }

  // Evaluate by rungs: process until blank or end; accumulator true/false starts false for first instruction if LD uses contact sets it
  bool acc = false;
  bool accInit = false;
  for (size_t i=0;i<lines.size();++i) {
    String ln = lines[i];
    if (ln.length()==0) {
      // end of rung: reset accumulator
      acc = false;
      accInit = false;
      continue;
    }
    // tokenize
    ln.trim();
    int sp = ln.indexOf(' ');
    String op = (sp==-1)?ln:ln.substring(0,sp);
    String operand = (sp==-1)?"":ln.substring(sp+1);
    operand.trim();
    op.toUpperCase();
    if (op == "LD") {
      // load contact state into accumulator
      bool val = false;
      if (operand.startsWith("DI")) {
        int pinIndex = atoi(operand.substring(2).c_str());
        // map DI1 -> IOManager::DI1, only support DI1 and DI2 here
        if (pinIndex==1) val = IOManager::readDigital(IOManager::DI1);
        else if (pinIndex==2) val = IOManager::readDigital(IOManager::DI2);
      } else if (operand.startsWith("ADC")) {
        int v = IOManager::readAnalog(IOManager::ADC1);
        val = (v>0);
      }
      acc = val;
      accInit = true;
    } else if (op == "AND") {
      bool val = false;
      if (operand.startsWith("DI")) {
        int pinIndex = atoi(operand.substring(2).c_str());
        if (pinIndex==1) val = IOManager::readDigital(IOManager::DI1);
        else if (pinIndex==2) val = IOManager::readDigital(IOManager::DI2);
      }
      if (!accInit) acc = val;
      else acc = acc && val;
      accInit = true;
    } else if (op == "OR") {
      bool val = false;
      if (operand.startsWith("DI")) {
        int pinIndex = atoi(operand.substring(2).c_str());
        if (pinIndex==1) val = IOManager::readDigital(IOManager::DI1);
        else if (pinIndex==2) val = IOManager::readDigital(IOManager::DI2);
      }
      if (!accInit) acc = val;
      else acc = acc || val;
      accInit = true;
    } else if (op == "OUT" || op == "SET") {
      // operand is DOx
      if (operand.startsWith("DO")) {
        int pinIndex = atoi(operand.substring(2).c_str());
        if (pinIndex==1) IOManager::writeDigital(IOManager::DO1, acc);
        else if (pinIndex==2) IOManager::writeDigital(IOManager::DO2, acc);
      }
    } else if (op == "RST") {
      if (operand.startsWith("DO")) {
        int pinIndex = atoi(operand.substring(2).c_str());
        if (pinIndex==1) IOManager::writeDigital(IOManager::DO1, false);
        else if (pinIndex==2) IOManager::writeDigital(IOManager::DO2, false);
      }
    }
    // continue reading lines until blank-line resets acc per rung
  }
  return true;
}

// ---------- Mini-Python interpreter (very small, line-based) ----------
// Supported syntax examples (each on its own line):
// DO1 = 1
// DO2 = 0
// IF DI1: DO1 = 1
// IF ADC > 2000: DO2 = 1
// comments with #
// This is intentionally minimal for safety and resource constraints.
static void runMiniPython(String script) {
  int start = 0;
  while (start < script.length()) {
    int eol = script.indexOf('\n', start);
    if (eol == -1) eol = script.length();
    String ln = script.substring(start, eol);
    start = eol + 1;
    ln.trim();
    if (ln.length()==0) continue;
    if (ln.startsWith("#")) continue;
    // IF condition: format IF <COND>: <ACTION>
    if (ln.startsWith("IF ")) {
      int colon = ln.indexOf(':');
      if (colon==-1) continue;
      String cond = ln.substring(3, colon);
      cond.trim();
      String action = ln.substring(colon+1);
      action.trim();
      bool condTrue = false;
      // support DIx, ADC comparisons
      if (cond.startsWith("DI")) {
        int idx = atoi(cond.substring(2).c_str());
        bool v = false;
        if (idx==1) v = IOManager::readDigital(IOManager::DI1);
        else if (idx==2) v = IOManager::readDigital(IOManager::DI2);
        condTrue = v;
      } else if (cond.indexOf('>')!=-1) {
        int gt = cond.indexOf('>');
        String left = cond.substring(0,gt); left.trim();
        String right = cond.substring(gt+1); right.trim();
        int rval = atoi(right.c_str());
        if (left=="ADC" || left.startsWith("ADC")) {
          int v = IOManager::readAnalog(IOManager::ADC1);
          condTrue = (v > rval);
        }
      }
      if (condTrue) {
        // execute action (simple assignment DOx = 0/1)
        int eq = action.indexOf('=');
        if (eq!=-1) {
          String left = action.substring(0,eq); left.trim();
          String right = action.substring(eq+1); right.trim();
          int val = atoi(right.c_str());
          if (left.startsWith("DO")) {
            int idx = atoi(left.substring(2).c_str());
            if (idx==1) IOManager::writeDigital(IOManager::DO1, val!=0);
            else if (idx==2) IOManager::writeDigital(IOManager::DO2, val!=0);
          }
        }
      }
    } else {
      // simple assignment like DO1 = 1
      int eq = ln.indexOf('=');
      if (eq!=-1) {
        String left = ln.substring(0,eq); left.trim();
        String right = ln.substring(eq+1); right.trim();
        int val = atoi(right.c_str());
        if (left.startsWith("DO")) {
          int idx = atoi(left.substring(2).c_str());
          if (idx==1) IOManager::writeDigital(IOManager::DO1, val!=0);
          else if (idx==2) IOManager::writeDigital(IOManager::DO2, val!=0);
        }
      }
    }
  }
}

bool LogicEngine::loadScript(const char* path, ScriptType type) {
  String s = SDCardManager::readFileString(path);
  if (s.length()==0) return false;
  _script = s;
  _type = type;
  Serial.printf("Loaded script (%d bytes) type=%d\n", _script.length(), (int)_type);
  return true;
}

void LogicEngine::update() {
  if (_type == LADDER) {
    evalLadder(_script);
  } else if (_type == MINIPY) {
    runMiniPython(_script);
  } else if (_type == LUA) {
    // Placeholder: integration point for Lua runtime
    // Future: call into embedded Lua VM with safe API mapping to IOManager
  }
}
