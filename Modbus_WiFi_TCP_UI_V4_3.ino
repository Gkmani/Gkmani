/*
 * Modbus WiFi Dashboard — V4.5  (Fixed Save & Restart buttons)
 *
 * FIXED: Save & Restart buttons now work properly
 * Fixed: Event handling in JavaScript
 * Hardware : ESP8266
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

/* ================================================================
   COMPILE-TIME CONSTANTS
   ================================================================ */

#define FW_VERSION           "V4.5-TCP-STA-REG-FIXED"

#define EEPROM_SIZE          1024
#define EEPROM_MAGIC         0xB6

#define MAX_VALUES           30
#define LABEL_SIZE           24
#define UNIT_SIZE            8

// Default Wi-Fi STA credentials
#define DEFAULT_STA_SSID     "PSI_Corp"
#define DEFAULT_STA_PASS     "Pass1234"

#define MODBUS_RETRIES       3
#define MODBUS_RETRY_MS      150

#define MODBUS_TCP_PORT      502
#define MODBUS_TCP_TIMEOUT   3000
#define RAW_TCP_PORT         8502
#define MAX_TCP_CLIENTS      3

#define MODBUS_MAX_RESP_BYTES  (MAX_VALUES * 2)

/* ================================================================
   ENUMS
   ================================================================ */

enum RegType : uint8_t {
  RT_UINT16     = 0,
  RT_INT16      = 1,
  RT_UINT32_AB  = 2,
  RT_UINT32_BA  = 3,
  RT_FLOAT32_AB = 4,
  RT_FLOAT32_BA = 5
};

/* ================================================================
   GLOBAL STATE
   ================================================================ */

ESP8266WebServer  server(80);
WiFiServer        rawTcpServer(RAW_TCP_PORT);
WiFiClient        rawTcpClients[MAX_TCP_CLIENTS];
WiFiClient        modbusTcpClient;

float    values[MAX_VALUES];
float    prevValues[MAX_VALUES];
bool     valueValid[MAX_VALUES];
bool     modbusOk    = false;
uint8_t  errorStreak = 0;
uint32_t lastReadMs  = 0;
uint32_t readCount   = 0;
uint32_t errorCount  = 0;
uint16_t mbapTxId    = 0;

uint16_t tcpRespBuf[MAX_VALUES * 2];
bool     staConnected = false;

/* ----------------------------------------------------------------
   Persisted Modbus config
   ---------------------------------------------------------------- */
uint8_t  slaveId;
uint8_t  funcCode;
uint16_t startAddr;
uint8_t  regCount;
uint8_t  valueCount;
uint8_t  pollSec;
RegType  regTypes[MAX_VALUES];
uint8_t  decimals[MAX_VALUES];
String   labels[MAX_VALUES];
String   units[MAX_VALUES];
uint8_t  regsPerValue[MAX_VALUES];

#define EEPROM_LABEL_BASE  64
#define EEPROM_UNIT_BASE   (EEPROM_LABEL_BASE + MAX_VALUES * LABEL_SIZE)

/* ----------------------------------------------------------------
   Persisted WiFi STA config
   ---------------------------------------------------------------- */
#define EE_WIFI_BASE   400
#define EE_WIFI_SSID   (EE_WIFI_BASE + 0)
#define EE_WIFI_PASS   (EE_WIFI_BASE + 33)
#define EE_WIFI_FLAGS  (EE_WIFI_BASE + 98)

char  wifiSsid[33];
char  wifiPass[65];
bool  wifiStaEnable;

/* ----------------------------------------------------------------
   Persisted Modbus TCP slave config
   ---------------------------------------------------------------- */
#define EE_MTCP_BASE   500
#define EE_MTCP_IP     (EE_MTCP_BASE + 0)
#define EE_MTCP_PORT   (EE_MTCP_BASE + 16)
#define EE_MTCP_EN     (EE_MTCP_BASE + 18)

char     modbusTcpIp[16];
uint16_t modbusTcpPort;

/* ================================================================
   EEPROM HELPERS
   ================================================================ */

static void eepromWriteStr(int base, const char *s, int maxLen) {
  for (int i = 0; i < maxLen; i++)
    EEPROM.write(base + i, (i < (int)strlen(s)) ? (uint8_t)s[i] : 0);
}

static void eepromWriteStr(int base, const String &s, int maxLen) {
  eepromWriteStr(base, s.c_str(), maxLen);
}

static void eepromReadStr(int base, char *dst, int maxLen) {
  int i;
  for (i = 0; i < maxLen - 1; i++) {
    char c = (char)EEPROM.read(base + i);
    if (c == '\0' || c < 0x20 || c > 0x7E) { dst[i] = '\0'; return; }
    dst[i] = c;
  }
  dst[maxLen - 1] = '\0';
}

static String eepromReadString(int base, int maxLen) {
  char buf[maxLen + 1];
  memset(buf, 0, sizeof(buf));
  eepromReadStr(base, buf, maxLen);
  buf[maxLen] = '\0';
  return String(buf);
}

/* ================================================================
   LOAD / SAVE CONFIG
   ================================================================ */

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  bool fresh = (EEPROM.read(0) != EEPROM_MAGIC);

  if (fresh) {
    slaveId   = 50;
    funcCode  = 4;
    pollSec   = 2;
    startAddr = 102;
    regCount  = 10;
    valueCount = 10;
    
    for (int i = 0; i < MAX_VALUES; i++) {
      regTypes[i] = RT_UINT16;
      decimals[i] = 2;
      labels[i]   = "Value " + String(i + 1);
      units[i]    = "";
      regsPerValue[i] = 1;
    }
  } else {
    slaveId   = EEPROM.read(1);
    funcCode  = EEPROM.read(2);
    pollSec   = EEPROM.read(3);
    EEPROM.get(4, startAddr);
    regCount  = EEPROM.read(6);
    valueCount = EEPROM.read(7);
    
    if (slaveId  == 0 || slaveId  > 247)           slaveId  = 50;
    if (funcCode != 3 && funcCode != 4)             funcCode = 4;
    if (pollSec  == 0)                              pollSec  = 2;
    if (regCount == 0 || regCount > MAX_VALUES)     regCount = 10;
    if (valueCount == 0 || valueCount > regCount)   valueCount = regCount;

    for (int i = 0; i < MAX_VALUES; i++) {
      uint8_t rt  = EEPROM.read(8 + i);
      regTypes[i] = (rt <= RT_FLOAT32_BA) ? (RegType)rt : RT_UINT16;
      decimals[i] = EEPROM.read(14 + i);
      if (decimals[i] > 6) decimals[i] = 2;
      labels[i]   = eepromReadString(EEPROM_LABEL_BASE + i * LABEL_SIZE, LABEL_SIZE);
      units[i]    = eepromReadString(EEPROM_UNIT_BASE  + i * UNIT_SIZE,  UNIT_SIZE);
      
      if (regTypes[i] == RT_UINT16 || regTypes[i] == RT_INT16) {
        regsPerValue[i] = 1;
      } else {
        regsPerValue[i] = 2;
      }
      
      if (labels[i].length() == 0)
        labels[i] = "Value " + String(i + 1);
    }
  }

  eepromReadStr(EE_WIFI_SSID, wifiSsid, sizeof(wifiSsid));
  eepromReadStr(EE_WIFI_PASS, wifiPass, sizeof(wifiPass));
  uint8_t wflags = EEPROM.read(EE_WIFI_FLAGS);
  
  if (strlen(wifiSsid) == 0) {
    strncpy(wifiSsid, DEFAULT_STA_SSID, sizeof(wifiSsid) - 1);
    strncpy(wifiPass, DEFAULT_STA_PASS, sizeof(wifiPass) - 1);
    wifiSsid[sizeof(wifiSsid) - 1] = '\0';
    wifiPass[sizeof(wifiPass) - 1] = '\0';
    wifiStaEnable = true;
  } else {
    wifiStaEnable = (wflags & 0x01) && (strlen(wifiSsid) > 0);
  }

  eepromReadStr(EE_MTCP_IP, modbusTcpIp, sizeof(modbusTcpIp));
  EEPROM.get(EE_MTCP_PORT, modbusTcpPort);
  if (modbusTcpPort == 0 || modbusTcpPort == 0xFFFF) modbusTcpPort = MODBUS_TCP_PORT;
  if (strlen(modbusTcpIp) == 0)
    strncpy(modbusTcpIp, "192.168.20.41", sizeof(modbusTcpIp));

  if (fresh) saveConfig();
}

void saveConfig() {
  EEPROM.write(0, EEPROM_MAGIC);
  EEPROM.write(1, slaveId);
  EEPROM.write(2, funcCode);
  EEPROM.write(3, pollSec);
  EEPROM.put(4, startAddr);
  EEPROM.write(6, regCount);
  EEPROM.write(7, valueCount);
  
  for (int i = 0; i < MAX_VALUES; i++) {
    EEPROM.write(8  + i, (uint8_t)regTypes[i]);
    EEPROM.write(14 + i, decimals[i]);
    eepromWriteStr(EEPROM_LABEL_BASE + i * LABEL_SIZE, labels[i], LABEL_SIZE);
    eepromWriteStr(EEPROM_UNIT_BASE  + i * UNIT_SIZE,  units[i],  UNIT_SIZE);
  }
  
  eepromWriteStr(EE_WIFI_SSID, wifiSsid, sizeof(wifiSsid));
  eepromWriteStr(EE_WIFI_PASS, wifiPass, sizeof(wifiPass));
  EEPROM.write(EE_WIFI_FLAGS, wifiStaEnable ? 0x01 : 0x00);
  eepromWriteStr(EE_MTCP_IP, modbusTcpIp, sizeof(modbusTcpIp));
  EEPROM.put(EE_MTCP_PORT, modbusTcpPort);
  EEPROM.write(EE_MTCP_EN, 0x01);
  EEPROM.commit();
}

/* ================================================================
   WIFI
   ================================================================ */

void initWiFi() {
  Serial.println("\n========================================");
  Serial.println("  Modbus TCP Dashboard " FW_VERSION);
  Serial.println("========================================");
  
  if (!wifiStaEnable || strlen(wifiSsid) == 0) {
    Serial.println("[ERROR] WiFi STA disabled or no SSID configured!");
    return;
  }

  Serial.print("[WiFi] Connecting to ");
  Serial.print(wifiSsid);
  Serial.print(" ... ");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid, wifiPass);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    staConnected = true;
    Serial.println("✓ CONNECTED!");
    Serial.print("  IP Address : ");
    Serial.println(WiFi.localIP());
  } else {
    staConnected = false;
    Serial.println("✗ CONNECTION FAILED!");
  }
  Serial.println("========================================\n");
}

void maintainWiFi() {
  if (staConnected && WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Connection lost! Reconnecting...");
    WiFi.reconnect();
    delay(2000);
    staConnected = (WiFi.status() == WL_CONNECTED);
  }
}

/* ================================================================
   MODBUS TCP
   ================================================================ */

bool modbusTcpConnect() {
  if (modbusTcpClient.connected()) return true;
  modbusTcpClient.stop();
  IPAddress ip;
  if (!ip.fromString(modbusTcpIp)) {
    Serial.println("[TCP] Invalid Modbus slave IP");
    return false;
  }
  bool ok = modbusTcpClient.connect(ip, modbusTcpPort);
  if (ok) {
    Serial.print("[TCP] Connected to ");
    Serial.print(modbusTcpIp);
    Serial.print(":");
    Serial.println(modbusTcpPort);
  }
  return ok;
}

bool modbusTcpRead(uint8_t fc, uint16_t addr, uint8_t qty) {
  if (!modbusTcpConnect()) return false;

  mbapTxId++;
  uint8_t req[12];
  req[0]  = mbapTxId >> 8;   req[1]  = mbapTxId & 0xFF;
  req[2]  = 0x00;            req[3]  = 0x00;
  req[4]  = 0x00;            req[5]  = 0x06;
  req[6]  = slaveId;
  req[7]  = fc;
  req[8]  = addr >> 8;       req[9]  = addr & 0xFF;
  req[10] = 0x00;            req[11] = qty;

  Serial.printf("[MB] Request: ID=%d, FC=%d, Addr=0x%04X (%d), Qty=%d\n", 
                slaveId, fc, addr, addr, qty);

  if (modbusTcpClient.write(req, sizeof(req)) != sizeof(req)) {
    modbusTcpClient.stop();
    return false;
  }

  uint32_t t0 = millis();
  while (modbusTcpClient.available() < 9) {
    if (millis() - t0 > MODBUS_TCP_TIMEOUT) {
      Serial.println("[MB] Timeout waiting for header");
      modbusTcpClient.stop();
      return false;
    }
    delay(1);
    yield();
  }

  uint8_t hdr[9];
  modbusTcpClient.read(hdr, 9);

  if (((uint16_t)(hdr[0] << 8) | hdr[1]) != mbapTxId) {
    Serial.println("[MB] Transaction ID mismatch");
    modbusTcpClient.stop();
    return false;
  }
  
  if (hdr[7] & 0x80) {
    Serial.printf("[MB] Exception response: 0x%02X\n", hdr[8]);
    modbusTcpClient.stop();
    return false;
  }

  uint8_t byteCount = hdr[8];
  if (byteCount > MODBUS_MAX_RESP_BYTES || byteCount == 0) {
    Serial.printf("[MB] Invalid byte count: %d\n", byteCount);
    modbusTcpClient.stop();
    return false;
  }

  uint8_t data[MODBUS_MAX_RESP_BYTES];
  t0 = millis();
  while (modbusTcpClient.available() < byteCount) {
    if (millis() - t0 > MODBUS_TCP_TIMEOUT) {
      Serial.println("[MB] Timeout waiting for data");
      modbusTcpClient.stop();
      return false;
    }
    delay(1);
    yield();
  }
  modbusTcpClient.read(data, byteCount);

  int regsCopied = min((int)(byteCount / 2), (int)qty);
  for (int i = 0; i < regsCopied; i++) {
    tcpRespBuf[i] = ((uint16_t)data[i * 2] << 8) | data[i * 2 + 1];
    Serial.printf("[MB] Register %d: 0x%04X (%d)\n", i, tcpRespBuf[i], tcpRespBuf[i]);
  }

  return true;
}

/* ================================================================
   DECODE & POLL
   ================================================================ */

void decodeRegisters() {
  int regOffset = 0;
  
  for (int i = 0; i < valueCount; i++) {
    prevValues[i] = values[i];
    
    if (regOffset + regsPerValue[i] > regCount) {
      valueValid[i] = false;
      continue;
    }
    
    switch (regTypes[i]) {
      case RT_UINT16:
        values[i] = (float)tcpRespBuf[regOffset];
        break;
      case RT_INT16:
        values[i] = (float)(int16_t)tcpRespBuf[regOffset];
        break;
      case RT_UINT32_AB: {
        uint32_t raw = ((uint32_t)tcpRespBuf[regOffset] << 16) | tcpRespBuf[regOffset + 1];
        values[i] = (float)raw;
        break;
      }
      case RT_UINT32_BA: {
        uint32_t raw = ((uint32_t)tcpRespBuf[regOffset + 1] << 16) | tcpRespBuf[regOffset];
        values[i] = (float)raw;
        break;
      }
      case RT_FLOAT32_AB: {
        uint32_t raw = ((uint32_t)tcpRespBuf[regOffset] << 16) | tcpRespBuf[regOffset + 1];
        memcpy(&values[i], &raw, 4);
        break;
      }
      case RT_FLOAT32_BA: {
        uint32_t raw = ((uint32_t)tcpRespBuf[regOffset + 1] << 16) | tcpRespBuf[regOffset];
        memcpy(&values[i], &raw, 4);
        break;
      }
    }
    
    if (!std::isfinite(values[i])) values[i] = 0.0f;
    valueValid[i] = true;
    regOffset += regsPerValue[i];
  }
}

void readModbus() {
  lastReadMs = millis();
  readCount++;

  bool success = false;
  for (int attempt = 0; attempt < MODBUS_RETRIES && !success; attempt++) {
    success = modbusTcpRead(funcCode, startAddr, regCount);
    if (!success && attempt < MODBUS_RETRIES - 1) {
      delay(MODBUS_RETRY_MS);
    }
  }

  if (success) {
    modbusOk    = true;
    errorStreak = 0;
    decodeRegisters();
    broadcastRawClients();
  } else {
    errorStreak++;
    errorCount++;
    if (errorStreak >= MODBUS_RETRIES) modbusOk = false;
  }
}

/* ================================================================
   RAW JSON STREAM
   ================================================================ */

void broadcastRawClients() {
  String line = "{\"t\":" + String(millis())
              + ",\"ok\":" + (modbusOk ? "true" : "false")
              + ",\"v\":[";
  
  for (int i = 0; i < valueCount; i++) {
    if (i) line += ',';
    if (valueValid[i]) {
      line += String(values[i], decimals[i]);
    } else {
      line += "null";
    }
  }
  line += "]}\n";

  for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
    if (rawTcpClients[i] && rawTcpClients[i].connected())
      rawTcpClients[i].print(line);
  }
}

void handleRawClients() {
  if (rawTcpServer.hasClient()) {
    WiFiClient nc = rawTcpServer.available();
    bool accepted = false;
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
      if (!rawTcpClients[i] || !rawTcpClients[i].connected()) {
        rawTcpClients[i].stop();
        rawTcpClients[i] = nc;
        accepted = true;
        break;
      }
    }
    if (!accepted) nc.stop();
  }
}

/* ================================================================
   CORS & API HANDLERS
   ================================================================ */

void addCORS() {
  server.sendHeader("Access-Control-Allow-Origin",  "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleOptions() { addCORS(); server.send(204); }

void apiData() {
  addCORS();
  DynamicJsonDocument doc(8192);
  
  doc["ok"]     = modbusOk;
  doc["count"]  = valueCount;
  doc["reads"]  = readCount;
  doc["errors"] = errorCount;
  doc["fw"]     = FW_VERSION;
  doc["slave"]  = modbusTcpIp;
  doc["port"]   = modbusTcpPort;
  
  JsonArray arr = doc.createNestedArray("registers");
  int regOffset = 0;
  int modbusAddr = 30000 + startAddr;
  
  for (int i = 0; i < valueCount; i++) {
    JsonObject o = arr.createNestedObject();
    o["label"] = labels[i];
    o["unit"]  = units[i];
    o["value"] = valueValid[i] ? values[i] : 0.0f;
    o["prev"]  = valueValid[i] ? prevValues[i] : 0.0f;
    o["valid"] = valueValid[i];
    o["dec"]   = decimals[i];
    o["type"]  = (uint8_t)regTypes[i];
    o["addr"]  = modbusAddr + regOffset;
    regOffset += regsPerValue[i];
  }
  
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void apiGetConfig() {
  addCORS();
  DynamicJsonDocument doc(4096);
  doc["slave"]   = slaveId;
  doc["func"]    = funcCode;
  doc["start"]   = startAddr;
  doc["count"]   = valueCount;
  doc["regCount"] = regCount;
  doc["poll"]    = pollSec;
  doc["tcpIp"]   = modbusTcpIp;
  doc["tcpPort"] = modbusTcpPort;
  
  JsonArray ta = doc.createNestedArray("types");
  JsonArray da = doc.createNestedArray("decimals");
  
  for (int i = 0; i < valueCount; i++) { 
    ta.add((uint8_t)regTypes[i]); 
    da.add(decimals[i]); 
  }
  
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void apiSetConfig() {
  addCORS();
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"JSON parse failed\"}");
    return;
  }

  uint8_t  ns = doc["slave"] | 0;
  uint8_t  nf = doc["func"]  | 0;
  uint16_t na = doc["start"] | 0;
  uint8_t  nv = doc["count"] | 0;
  uint8_t  np = doc["poll"]  | 1;

  if (ns < 1 || ns > 247) { server.send(400,"application/json","{\"error\":\"slaveId 1-247\"}"); return; }
  if (nf != 3 && nf != 4) { server.send(400,"application/json","{\"error\":\"funcCode 3 or 4\"}"); return; }
  if (nv < 1 || nv > MAX_VALUES) { 
    server.send(400,"application/json","{\"error\":\"count 1-30\"}"); 
    return; 
  }
  if (np < 1 || np > 60) { server.send(400,"application/json","{\"error\":\"poll 1-60s\"}"); return; }

  slaveId = ns; funcCode = nf; startAddr = na; valueCount = nv;
  regCount = nv;
  pollSec = np;

  if (doc.containsKey("tcpIp")) {
    strncpy(modbusTcpIp, doc["tcpIp"].as<const char*>(), sizeof(modbusTcpIp)-1);
    modbusTcpIp[sizeof(modbusTcpIp)-1] = '\0';
  }
  if (doc.containsKey("tcpPort")) modbusTcpPort = doc["tcpPort"].as<uint16_t>();

  if (doc.containsKey("types")) {
    JsonArray ta = doc["types"].as<JsonArray>(); 
    int i = 0;
    for (JsonVariant t : ta) { 
      if (i >= nv) break; 
      regTypes[i++] = (t.as<uint8_t>() <= RT_FLOAT32_BA) ? (RegType)t.as<uint8_t>() : RT_UINT16; 
    }
  }
  if (doc.containsKey("decimals")) {
    JsonArray da = doc["decimals"].as<JsonArray>(); 
    int i = 0;
    for (JsonVariant d : da) { 
      if (i >= nv) break; 
      decimals[i++] = (d.as<uint8_t>() <= 6) ? d.as<uint8_t>() : 2; 
    }
  }

  for (int i = 0; i < nv; i++) {
    if (regTypes[i] == RT_UINT16 || regTypes[i] == RT_INT16) {
      regsPerValue[i] = 1;
    } else {
      regsPerValue[i] = 2;
      if (regCount < nv * 2) regCount = nv * 2;
    }
  }

  saveConfig();
  modbusTcpClient.stop();
  server.send(200, "application/json", "{\"saved\":true}");
  delay(200);
  ESP.restart();
}

void apiSetLabels() {
  addCORS();
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"JSON parse failed\"}");
    return;
  }
  if (doc.containsKey("labels")) {
    JsonArray la = doc["labels"].as<JsonArray>(); 
    int i = 0;
    for (JsonVariant s : la) { 
      if (i >= valueCount) break; 
      String str = s.as<String>(); 
      str.trim(); 
      labels[i++] = str.substring(0, LABEL_SIZE-1); 
    }
  }
  if (doc.containsKey("units")) {
    JsonArray ua = doc["units"].as<JsonArray>(); 
    int i = 0;
    for (JsonVariant s : ua) { 
      if (i >= valueCount) break; 
      String str = s.as<String>(); 
      str.trim(); 
      units[i++] = str.substring(0, UNIT_SIZE-1); 
    }
  }
  saveConfig();
  server.send(200, "application/json", "{\"saved\":true}");
}

void apiGetWifi() {
  addCORS();
  DynamicJsonDocument doc(384);
  doc["staEnable"] = wifiStaEnable;
  doc["ssid"]      = wifiSsid;
  doc["connected"] = staConnected;
  doc["staIp"]     = staConnected ? WiFi.localIP().toString() : "";
  doc["rssi"]      = staConnected ? WiFi.RSSI() : 0;
  doc["rawPort"]   = RAW_TCP_PORT;
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void apiSetWifiConfig() {
  addCORS();
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"JSON parse failed\"}");
    return;
  }

  if (doc.containsKey("ssid")) {
    const char *s = doc["ssid"].as<const char*>();
    if (strlen(s) > 32) { server.send(400,"application/json","{\"error\":\"SSID max 32 chars\"}"); return; }
    strncpy(wifiSsid, s, sizeof(wifiSsid)-1); wifiSsid[sizeof(wifiSsid)-1] = '\0';
  }
  if (doc.containsKey("pass")) {
    const char *p = doc["pass"].as<const char*>();
    if (strlen(p) > 0 && strlen(p) < 8) { server.send(400,"application/json","{\"error\":\"Password min 8 chars\"}"); return; }
    if (strlen(p) >= 8) { strncpy(wifiPass, p, sizeof(wifiPass)-1); wifiPass[sizeof(wifiPass)-1] = '\0'; }
  }
  if (doc.containsKey("staEnable")) wifiStaEnable = doc["staEnable"].as<bool>();
  if (doc.containsKey("tcpIp")) { 
    strncpy(modbusTcpIp, doc["tcpIp"].as<const char*>(), sizeof(modbusTcpIp)-1); 
    modbusTcpIp[sizeof(modbusTcpIp)-1] = '\0'; 
  }
  if (doc.containsKey("tcpPort")) modbusTcpPort = doc["tcpPort"].as<uint16_t>();

  saveConfig();
  server.send(200, "application/json", "{\"saved\":true}");
  delay(200);
  ESP.restart();
}

void apiStatus() {
  addCORS();
  DynamicJsonDocument doc(256);
  doc["uptime"]    = millis() / 1000;
  doc["freeHeap"]  = ESP.getFreeHeap();
  doc["staConn"]   = staConnected;
  doc["staIp"]     = staConnected ? WiFi.localIP().toString() : "";
  doc["ok"]        = modbusOk;
  doc["reads"]     = readCount;
  doc["errors"]    = errorCount;
  doc["slave"]     = modbusTcpIp;
  doc["port"]      = modbusTcpPort;
  doc["fw"]        = FW_VERSION;
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

/* ================================================================
   DASHBOARD HTML - Fixed Save & Restart buttons
   ================================================================ */

const char DASHBOARD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Modbus TCP Dashboard</title>
<style>
:root{
  --primary:#3b82f6;--primary-dark:#1d4ed8;
  --bg:#0f172a;--card:#1e293b;--text:#f8fafc;--muted:#94a3b8;
  --ok:#10b981;--err:#ef4444;--warn:#f59e0b;--info:#6366f1;
  --border:#334155;--shadow:0 4px 6px -1px rgba(0,0,0,.35);
  --input-bg:#0f172a;--hover-bg:#2d3748;
}
body.day{
  --bg:#f1f5f9;--card:#fff;--text:#1e293b;--muted:#64748b;
  --border:#e2e8f0;--shadow:0 4px 6px -1px rgba(0,0,0,.1);
  --input-bg:#fff;--hover-bg:#f1f5f9;
}
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',system-ui,sans-serif}
body{background:var(--bg);color:var(--text);min-height:100vh;padding:20px;transition:background .3s,color .3s}
.wrap{max-width:1400px;margin:0 auto}
.hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:22px;padding-bottom:18px;border-bottom:1px solid var(--border);flex-wrap:wrap;gap:12px}
h1{font-size:1.8rem;font-weight:700;background:linear-gradient(135deg,var(--primary),#8b5cf6);-webkit-background-clip:text;-webkit-text-fill-color:transparent}
.ctrl{display:flex;gap:10px;flex-wrap:wrap}
.th-tog{background:var(--card);border:1px solid var(--border);border-radius:30px;padding:5px;display:flex;gap:6px;cursor:pointer}
.th-tog span{padding:5px 12px;border-radius:25px;font-size:.75rem;font-weight:600;transition:all .2s}
.th-tog span.on{background:var(--primary);color:#fff}
.th-tog span:not(.on){color:var(--muted)}
.badge{display:inline-flex;align-items:center;gap:6px;padding:5px 12px;border-radius:20px;font-size:.78rem;font-weight:600}
.badge.ok {background:rgba(16,185,129,.15);color:var(--ok);border:1px solid rgba(16,185,129,.3)}
.badge.err{background:rgba(239,68,68,.15);color:var(--err);border:1px solid rgba(239,68,68,.3)}
.badge.info{background:rgba(99,102,241,.15);color:var(--info);border:1px solid rgba(99,102,241,.3)}
.dot{width:8px;height:8px;border-radius:50%}
.ok .dot{background:var(--ok);animation:pulse 1.4s infinite}
.err .dot{background:var(--err)}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}
.btn{padding:9px 16px;border:none;border-radius:8px;font-weight:600;cursor:pointer;transition:all .2s;font-size:.85rem}
.btn-p{background:var(--primary);color:#fff}
.btn-p:hover{background:var(--primary-dark);transform:translateY(-1px)}
.btn-s{background:var(--card);color:var(--text);border:1px solid var(--border)}
.btn-s:hover{background:var(--hover-bg)}
.infobar{display:flex;flex-wrap:wrap;gap:16px;background:var(--card);border-radius:10px;padding:16px;border:1px solid var(--border);margin-bottom:20px}
.iitem{display:flex;flex-direction:column;gap:3px;min-width:90px}
.ilabel{font-size:.72rem;text-transform:uppercase;letter-spacing:.07em;color:var(--muted)}
.ivalue{font-size:.95rem;font-weight:600}
.netbar{display:flex;flex-wrap:wrap;gap:10px;margin-bottom:20px;align-items:center}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:16px;margin-bottom:22px}
.card{background:var(--card);border-radius:12px;padding:20px;box-shadow:var(--shadow);border:1px solid var(--border);transition:transform .2s,box-shadow .2s}
.card:hover{transform:translateY(-3px);box-shadow:0 12px 28px rgba(0,0,0,.35)}
.card-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px}
.card-title{font-size:.95rem;font-weight:600;word-break:break-word}
.card-edit{background:none;border:none;color:var(--primary);cursor:pointer;font-size:.8rem;display:flex;align-items:center;gap:3px}
.card-val{font-size:2rem;font-weight:700;color:var(--ok);text-align:center;margin:12px 0;font-family:'Monaco','Courier New',monospace;letter-spacing:-.5px}
.card-val.inv{color:var(--muted);font-size:1.3rem}
.card-ftr{display:flex;justify-content:space-between;font-size:.78rem;color:var(--muted);padding-top:8px;border-top:1px solid var(--border)}
.trend.up{color:var(--ok)}.trend.dn{color:var(--err)}.trend.fl{color:var(--muted)}
.modal{display:none;position:fixed;inset:0;background:rgba(0,0,0,.78);justify-content:center;align-items:center;z-index:900}
.modal.active{display:flex}
.mc{background:var(--card);padding:26px;border-radius:14px;width:92%;max-width:700px;max-height:88vh;overflow-y:auto}
.mt{font-size:1.25rem;font-weight:700;margin-bottom:20px}
.fg{margin-bottom:16px}
label{display:block;margin-bottom:5px;font-weight:600;font-size:.87rem}
input,select{width:100%;padding:10px;background:var(--input-bg);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:.93rem}
input:focus,select:focus{outline:none;border-color:var(--primary);box-shadow:0 0 0 3px rgba(59,130,246,.15)}
.frow{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.fa{display:flex;gap:10px;justify-content:flex-end;margin-top:8px}
.sl{font-size:.72rem;text-transform:uppercase;letter-spacing:.07em;color:var(--muted);margin:14px 0 8px;padding-bottom:5px;border-bottom:1px solid var(--border)}
.tog-row{display:flex;align-items:center;justify-content:space-between;padding:10px 0;border-bottom:1px solid var(--border)}
.tog{position:relative;width:44px;height:24px}
.tog input{opacity:0;width:0;height:0}
.sld{position:absolute;inset:0;background:#334155;border-radius:24px;cursor:pointer;transition:.3s}
.sld:before{content:'';position:absolute;width:18px;height:18px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.3s}
input:checked+.sld{background:var(--primary)}
input:checked+.sld:before{transform:translateX(20px)}
.toast{position:fixed;bottom:18px;right:18px;padding:11px 20px;border-radius:8px;box-shadow:var(--shadow);opacity:0;transform:translateY(14px);transition:all .3s;font-weight:600;font-size:.88rem;z-index:2000}
.toast.show{opacity:1;transform:translateY(0)}
.toast.ok{background:var(--ok);color:#fff}
.toast.err{background:var(--err);color:#fff}
.sp{width:16px;height:16px;border:2px solid rgba(255,255,255,.3);border-top-color:#fff;border-radius:50%;animation:spin .7s linear infinite;display:inline-block}
@keyframes spin{to{transform:rotate(360deg)}}
.reg-scroll{max-height:500px;overflow-y:auto;padding-right:10px}
@media(max-width:600px){.hdr{flex-direction:column;align-items:flex-start}.frow{grid-template-columns:1fr}}
</style>
</head>
<body>
<div class="wrap">
  <div class="hdr">
    <div style="display:flex;align-items:center;gap:14px;flex-wrap:wrap">
      <h1>📊 Modbus TCP Dashboard</h1>
      <span class="badge err" id="mbBadge"><span class="dot"></span><span id="mbText">Connecting...</span></span>
    </div>
    <div class="ctrl">
      <div class="th-tog" id="thTog">
        <span data-t="night">🌙 Night</span>
        <span data-t="day">☀️ Day</span>
      </div>
      <button class="btn btn-s" onclick="openModal('wifiM')">📶 WiFi</button>
      <button class="btn btn-s" onclick="openModal('cfgM')">⚙ Config</button>
      <button class="btn btn-p" onclick="openModal('lblM')">✏ Labels</button>
    </div>
  </div>
  <div class="netbar">
    <span class="badge info" id="wfBadge">📶 Loading...</span>
    <span class="badge info" id="tcpBadge">🖧 —</span>
    <span class="badge info">📡 JSON stream :8502</span>
    <span class="badge info" id="valueCountBadge">📊 -- values</span>
  </div>
  <div class="infobar">
    <div class="iitem"><span class="ilabel">Slave ID</span><span class="ivalue" id="iSlave">—</span></div>
    <div class="iitem"><span class="ilabel">Function</span><span class="ivalue" id="iFunc">—</span></div>
    <div class="iitem"><span class="ilabel">Start Addr</span><span class="ivalue" id="iStart">—</span></div>
    <div class="iitem"><span class="ilabel">Poll</span><span class="ivalue" id="iPoll">—</span></div>
    <div class="iitem"><span class="ilabel">Reads / Errors</span><span class="ivalue" id="iReads">—</span></div>
    <div class="iitem"><span class="ilabel">TCP Slave</span><span class="ivalue" id="iSlave2">—</span></div>
    <div class="iitem"><span class="ilabel">Last Update</span><span class="ivalue" id="iTime">—</span></div>
  </div>
  <div class="grid" id="grid">
    <div class="card" style="grid-column:1/-1;text-align:center;padding:40px">
      <div class="sp" style="margin:0 auto 12px"></div>
      <div style="color:var(--muted)">Connecting to Modbus TCP slave...</div>
    </div>
  </div>
</div>

<!-- WiFi Modal -->
<div class="modal" id="wifiM">
  <div class="mc">
    <div class="mt">📶 WiFi STA Configuration</div>
    <div class="sl">Modbus TCP Slave</div>
    <div class="frow">
      <div class="fg"><label>Slave IP Address</label><input type="text" id="wTcpIp" maxlength="15" placeholder="192.168.1.100"></div>
      <div class="fg"><label>Port</label><input type="number" id="wTcpPort" min="1" max="65535" value="502"></div>
    </div>
    <div class="sl">ESP8266 WiFi</div>
    <div class="tog-row"><span>Connect to WiFi</span><label class="tog"><input type="checkbox" id="wStaEn"><span class="sld"></span></label></div>
    <div><div class="fg"><label>SSID</label><input type="text" id="wSsid" maxlength="32"></div>
    <div class="fg"><label>Password</label><input type="password" id="wPass" maxlength="64"></div></div>
    <div class="fa"><button class="btn btn-s" onclick="closeModal('wifiM')">Cancel</button><button class="btn btn-p" id="saveWifiBtn">Save &amp; Restart</button></div>
  </div>
</div>

<!-- Config Modal -->
<div class="modal" id="cfgM">
  <div class="mc">
    <div class="mt">⚙ Modbus Configuration</div>
    <div class="frow"><div class="fg"><label>Slave ID (1-247)</label><input type="number" id="cSlave" min="1" max="247"></div>
    <div class="fg"><label>Function Code</label><select id="cFunc"><option value="3">FC3 - Holding (4xxxx)</option><option value="4">FC4 - Input (3xxxx)</option></select></div></div>
    <div class="frow"><div class="fg"><label>Start Address (offset)</label><input type="number" id="cStart" min="0" max="65535"></div>
    <div class="fg"><label>Number of Values (1-30)</label><input type="number" id="cCount" min="1" max="30"></div></div>
    <div class="fg"><label>Poll Interval (seconds)</label><input type="number" id="cPoll" min="1" max="60"></div>
    <div class="sl">Data Type for Each Value</div>
    <div id="perReg" class="reg-scroll"></div>
    <div class="fa"><button class="btn btn-s" onclick="closeModal('cfgM')">Cancel</button><button class="btn btn-p" id="saveCfgBtn">Save &amp; Restart</button></div>
  </div>
</div>

<!-- Labels Modal -->
<div class="modal" id="lblM">
  <div class="mc"><div class="mt">✏ Labels &amp; Units</div><div id="lblCont" class="reg-scroll"></div>
  <div class="fa"><button class="btn btn-s" onclick="closeModal('lblM')">Cancel</button><button class="btn btn-p" id="saveLabelsBtn">Save Labels</button></div></div>
</div>
<div class="toast" id="toast"></div>

<script>
const TYPES = ['UINT16', 'INT16', 'UINT32 AB', 'UINT32 BA', 'FLOAT32 AB', 'FLOAT32 BA'];
let cfg = {}, wfi = {}, regs = [], fails = 0;
let pollInterval = null;

function initTheme() {
  const t = localStorage.getItem('mbTheme') || 'night';
  document.body.classList.toggle('day', t === 'day');
  document.querySelectorAll('[data-t]').forEach(el => el.classList.toggle('on', el.dataset.t === t));
}

function setTheme(t) {
  document.body.classList.toggle('day', t === 'day');
  localStorage.setItem('mbTheme', t);
  document.querySelectorAll('[data-t]').forEach(el => el.classList.toggle('on', el.dataset.t === t));
}

document.getElementById('thTog').addEventListener('click', e => {
  const s = e.target.closest('[data-t]');
  if (s) setTheme(s.dataset.t);
});

async function loadData() {
  try {
    const r = await fetch('/api/data');
    if (!r.ok) { fails++; throw new Error('HTTP ' + r.status); }
    const d = await r.json();
    regs = d.registers || [];
    fails = 0;
    setBadge(d.ok, d.ok ? 'Modbus OK' : 'Modbus Fault');
    document.getElementById('iReads').textContent = d.reads + ' / ' + d.errors;
    document.getElementById('iSlave2').textContent = d.slave + ':' + d.port;
    document.getElementById('iTime').textContent = now();
    document.getElementById('valueCountBadge').innerHTML = '📊 ' + d.count + ' values';
    renderCards();
  } catch (e) {
    fails++;
    if (fails >= 3) { setBadge(false, 'No Response'); toast('Cannot reach device', 'warn'); }
  }
}

async function loadCfg() {
  try {
    const r = await fetch('/api/config');
    if (!r.ok) throw new Error();
    cfg = await r.json();
    document.getElementById('iSlave').textContent = cfg.slave;
    document.getElementById('iFunc').textContent = 'FC' + cfg.func;
    document.getElementById('iStart').textContent = '0x' + cfg.start.toString(16).toUpperCase().padStart(4, '0') + ' (' + (30000 + cfg.start) + ')';
    document.getElementById('iPoll').textContent = cfg.poll + 's';
    updTcpBadge();
  } catch (e) { toast('Config load failed', 'err'); }
}

async function loadWifi() {
  try {
    const r = await fetch('/api/wifi');
    if (!r.ok) throw new Error();
    wfi = await r.json();
    updWfBadge();
  } catch (e) { toast('WiFi status load failed', 'err'); }
}

function setBadge(ok, txt) {
  const b = document.getElementById('mbBadge');
  b.className = 'badge ' + (ok ? 'ok' : 'err');
  document.getElementById('mbText').textContent = txt;
}

function updWfBadge() {
  const b = document.getElementById('wfBadge');
  if (wfi.connected) {
    b.className = 'badge ok';
    b.innerHTML = '📶 STA: ' + wfi.staIp + ' (' + wfi.rssi + 'dBm)';
  } else {
    b.className = 'badge err';
    b.innerHTML = '📶 Not Connected';
  }
}

function updTcpBadge() {
  const b = document.getElementById('tcpBadge');
  b.className = 'badge ok';
  b.innerHTML = '🖧 TCP slave: ' + cfg.tcpIp + ':' + cfg.tcpPort;
}

function renderCards() {
  const g = document.getElementById('grid');
  g.innerHTML = '';
  if (!regs.length) {
    g.innerHTML = '<div class="card" style="grid-column:1/-1;text-align:center;padding:40px;color:var(--muted)">No registers configured</div>';
    return;
  }
  regs.forEach((r, i) => {
    const tr = trendOf(r.value, r.prev);
    const c = document.createElement('div');
    c.className = 'card';
    c.innerHTML = `
      <div class="card-hdr">
        <div class="card-title">${esc(r.label)}</div>
        <button class="card-edit" onclick="focusLabel(${i})">✏️ Edit</button>
      </div>
      <div class="card-val${r.valid ? '' : ' inv'}">${formatValue(r)}${r.unit ? '<span style="font-size:.9rem;margin-left:4px;color:var(--muted)">' + esc(r.unit) + '</span>' : ''}</div>
      <div class="card-ftr">
        <span>Addr ${r.addr} &middot; ${TYPES[r.type] || 'Unknown'}</span>
        <span class="trend ${tr.c}">${tr.s}</span>
      </div>`;
    g.appendChild(c);
  });
}

function formatValue(r) { return r.valid ? r.value.toFixed(r.dec ?? 2) : '---'; }
function trendOf(v, p) { const d = v - p; if (Math.abs(d) < 1e-6) return { s: '━', c: 'fl' }; return d > 0 ? { s: '▲', c: 'up' } : { s: '▼', c: 'dn' }; }

function openModal(id) {
  if (id === 'wifiM') {
    document.getElementById('wStaEn').checked = wfi.staEnable || false;
    document.getElementById('wSsid').value = wfi.ssid || '';
    document.getElementById('wPass').value = '';
    document.getElementById('wTcpIp').value = cfg.tcpIp || '192.168.20.41';
    document.getElementById('wTcpPort').value = cfg.tcpPort || 502;
  }
  if (id === 'cfgM') {
    document.getElementById('cSlave').value = cfg.slave || 50;
    document.getElementById('cFunc').value = cfg.func || 4;
    document.getElementById('cStart').value = cfg.start || 102;
    document.getElementById('cCount').value = cfg.count || 10;
    document.getElementById('cPoll').value = cfg.poll || 2;
    buildPerReg();
  }
  if (id === 'lblM') buildLabels();
  document.getElementById(id).classList.add('active');
}

function closeModal(id) { document.getElementById(id).classList.remove('active'); }

// Save WiFi function
document.getElementById('saveWifiBtn')?.addEventListener('click', async function() {
  const btn = this;
  const orig = btn.innerHTML;
  btn.innerHTML = '<span class="sp"></span>';
  btn.disabled = true;
  const payload = {
    staEnable: document.getElementById('wStaEn').checked,
    ssid: document.getElementById('wSsid').value.trim(),
    pass: document.getElementById('wPass').value,
    tcpIp: document.getElementById('wTcpIp').value.trim(),
    tcpPort: parseInt(document.getElementById('wTcpPort').value)
  };
  try {
    const r = await fetch('/api/wifi-config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    toast('Saved — restarting...', 'ok');
    setTimeout(() => closeModal('wifiM'), 1500);
    setTimeout(() => location.reload(), 4000);
  } catch (e) { toast('Error: ' + e.message, 'err'); btn.innerHTML = orig; btn.disabled = false; }
});

function buildPerReg() {
  const n = parseInt(document.getElementById('cCount').value) || 10;
  const c = document.getElementById('perReg');
  c.innerHTML = '';
  for (let i = 0; i < n; i++) {
    const d = document.createElement('div');
    d.style.marginBottom = '12px';
    d.innerHTML = `<div style="font-size:.8rem;color:var(--muted);margin-bottom:5px">Value ${i + 1}</div>
      <div class="frow">
        <div class="fg" style="margin:0"><label style="font-weight:normal">Data Type</label>
          <select id="pT${i}">
            <option value="0">UINT16 (16-bit unsigned)</option>
            <option value="1">INT16 (16-bit signed)</option>
            <option value="2">UINT32 AB (32-bit unsigned)</option>
            <option value="3">UINT32 BA (32-bit unsigned swapped)</option>
            <option value="4">FLOAT32 AB (32-bit float)</option>
            <option value="5">FLOAT32 BA (32-bit float swapped)</option>
          </select></div>
        <div class="fg" style="margin:0"><label style="font-weight:normal">Decimals</label>
          <input type="number" id="pD${i}" min="0" max="6" value="${(cfg.decimals || [])[i] ?? 2}"></div>
      </div>`;
    if (cfg.types && cfg.types[i] != null) d.querySelector('#pT' + i).value = cfg.types[i];
    c.appendChild(d);
  }
}

// Save Config function
document.getElementById('saveCfgBtn')?.addEventListener('click', async function() {
  const btn = this;
  const orig = btn.innerHTML;
  btn.innerHTML = '<span class="sp"></span>';
  btn.disabled = true;
  const n = parseInt(document.getElementById('cCount').value) || 10;
  const types = [], decs = [];
  for (let i = 0; i < n; i++) {
    types.push(parseInt(document.getElementById('pT' + i)?.value || 0));
    decs.push(parseInt(document.getElementById('pD' + i)?.value || 2));
  }
  const payload = {
    slave: parseInt(document.getElementById('cSlave').value),
    func: parseInt(document.getElementById('cFunc').value),
    start: parseInt(document.getElementById('cStart').value),
    count: n,
    poll: parseInt(document.getElementById('cPoll').value),
    tcpIp: cfg.tcpIp || '192.168.20.41',
    tcpPort: cfg.tcpPort || 502,
    types: types,
    decimals: decs
  };
  try {
    const r = await fetch('/api/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) });
    const d = await r.json();
    if (!r.ok) { toast(d.error || 'Error', 'err'); btn.innerHTML = orig; btn.disabled = false; return; }
    toast('Saved — restarting...', 'ok');
    setTimeout(() => closeModal('cfgM'), 1500);
    setTimeout(() => location.reload(), 4000);
  } catch (e) { toast('Could not reach device', 'err'); btn.innerHTML = orig; btn.disabled = false; }
});

function buildLabels() {
  const c = document.getElementById('lblCont');
  c.innerHTML = '';
  regs.forEach((r, i) => {
    const d = document.createElement('div');
    d.className = 'fg';
    d.innerHTML = `<div class="sl">Value ${i + 1} &middot; Addr ${r.addr}</div>
      <div class="frow">
        <div><label>Label</label><input type="text" id="lL${i}" value="${esc(r.label)}" maxlength="23"></div>
        <div><label>Unit</label><input type="text" id="lU${i}" value="${esc(r.unit || '')}" maxlength="7"></div>
      </div>`;
    c.appendChild(d);
  });
}

function focusLabel(i) { openModal('lblM'); setTimeout(() => { const e = document.getElementById('lL' + i); if (e) { e.focus(); e.select(); } }, 80); }

// Save Labels function
document.getElementById('saveLabelsBtn')?.addEventListener('click', async function() {
  const lbls = [], uns = [];
  regs.forEach((_, i) => { lbls.push(document.getElementById('lL' + i)?.value || ''); uns.push(document.getElementById('lU' + i)?.value || ''); });
  try {
    const r = await fetch('/api/labels', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ labels: lbls, units: uns }) });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    toast('Labels saved', 'ok'); closeModal('lblM'); await loadData();
  } catch (e) { toast('Error saving labels', 'err'); }
});

function toast(msg, type = 'ok') {
  const t = document.getElementById('toast');
  t.textContent = msg; t.className = 'toast ' + type + ' show';
  setTimeout(() => t.classList.remove('show'), 3500);
}

function now() { const d = new Date(); return [d.getHours(), d.getMinutes(), d.getSeconds()].map(n => String(n).padStart(2, '0')).join(':'); }
function esc(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#39;'); }

// Initialize
initTheme();
loadCfg();
loadWifi();
loadData();

function startPoll() {
  if (pollInterval) clearInterval(pollInterval);
  const interval = (cfg.poll || 2) * 1000;
  pollInterval = setInterval(loadData, interval);
}

const origLoadCfg = loadCfg;
window.loadCfg = async function() { await origLoadCfg(); startPoll(); };
loadCfg();

document.querySelectorAll('.modal').forEach(m => { m.addEventListener('click', e => { if (e.target === m) m.classList.remove('active'); }); });
</script>
</body>
</html>
)rawliteral";

/* ================================================================
   SETUP
   ================================================================ */

void setup() {
  Serial.begin(115200);
  
  loadConfig();
  initWiFi();

  server.on("/", HTTP_GET, []() { 
    server.send_P(200, "text/html", DASHBOARD);
  });
  server.on("/api/data", HTTP_GET, apiData);
  server.on("/api/config", HTTP_GET, apiGetConfig);
  server.on("/api/config", HTTP_POST, apiSetConfig);
  server.on("/api/labels", HTTP_POST, apiSetLabels);
  server.on("/api/wifi", HTTP_GET, apiGetWifi);
  server.on("/api/wifi-config", HTTP_POST, apiSetWifiConfig);
  server.on("/api/status", HTTP_GET, apiStatus);
  server.on("/api/data", HTTP_OPTIONS, handleOptions);
  server.on("/api/config", HTTP_OPTIONS, handleOptions);
  server.on("/api/labels", HTTP_OPTIONS, handleOptions);
  server.on("/api/wifi", HTTP_OPTIONS, handleOptions);
  server.on("/api/wifi-config", HTTP_OPTIONS, handleOptions);
  server.on("/api/status", HTTP_OPTIONS, handleOptions);

  server.begin();
  rawTcpServer.begin();
  rawTcpServer.setNoDelay(true);

  Serial.println("[HTTP] Web server started");
  if (staConnected) {
    Serial.println("\n========================================");
    Serial.println("  DASHBOARD ACCESS");
    Serial.println("========================================");
    Serial.print("  Web UI  : http://");
    Serial.println(WiFi.localIP());
    Serial.print("  JSON    : http://");
    Serial.print(WiFi.localIP());
    Serial.println("/api/data");
    Serial.print("  Raw TCP : ");
    Serial.print(WiFi.localIP());
    Serial.print(":");
    Serial.println(RAW_TCP_PORT);
    Serial.println("========================================\n");
    Serial.printf("  Configured to display %d values\n", valueCount);
  } else {
    Serial.println("[ERROR] No WiFi connection - device unreachable!");
  }

  lastReadMs = millis() - (uint32_t)pollSec * 1000UL - 1;
}

/* ================================================================
   LOOP
   ================================================================ */

void loop() {
  if ((millis() - lastReadMs) >= (uint32_t)pollSec * 1000UL) {
    readModbus();
  }
  server.handleClient();
  handleRawClients();
  maintainWiFi();
}