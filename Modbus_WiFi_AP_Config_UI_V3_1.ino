/*
 * Modbus WiFi Dashboard — V3.0 (Fixed)
 *
 * FIXES:
 * - Renamed WiFiMode_t to WifiOperMode_t to avoid conflict with ESP8266WiFi
 * - Replaced node.setResponseBuffer() with manual buffer storage
 * - Added <cmath> for isfinite()
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <ModbusMaster.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <cmath>  // FIX: Added for isfinite()

/* ================================================================
   COMPILE-TIME CONSTANTS
   ================================================================ */

#define EEPROM_SIZE          768
#define EEPROM_MAGIC         0xA5

#define MAX_FLOATS           6
#define LABEL_SIZE           24
#define UNIT_SIZE            8

// Soft-AP fallback credentials (used when STA fails or not configured)
#define AP_SSID              "SysCon"
#define AP_PASS              "12345678"

#define MODBUS_RETRIES       3
#define MODBUS_RETRY_MS      100

#define STA_CONNECT_TIMEOUT  15000    // ms to wait for STA connection
#define STA_RECONNECT_MS     30000    // ms between STA reconnect attempts

#define MODBUS_TCP_PORT      502
#define MODBUS_TCP_TIMEOUT   2000     // ms for TCP read timeout
#define RAW_TCP_PORT         8502     // JSON stream port
#define MAX_TCP_CLIENTS      3

/* ================================================================
   ENUMS
   ================================================================ */

enum RegType : uint8_t {
  RT_FLOAT32_AB = 0,
  RT_FLOAT32_BA = 1,
  RT_INT16      = 2,
  RT_UINT16     = 3
};

// FIX: Renamed to avoid conflict with ESP8266WiFi library
enum WifiOperMode_t : uint8_t {
  WMODE_AP_ONLY = 0,   // AP only (default / fallback)
  WMODE_STA     = 1    // STA with AP fallback
};

/* ================================================================
   GLOBAL STATE
   ================================================================ */

ESP8266WebServer  server(80);
WiFiServer        tcpServer(RAW_TCP_PORT);
WiFiClient        tcpClients[MAX_TCP_CLIENTS];
WiFiClient        modbusTcpClient;           // persistent Modbus TCP socket
ModbusMaster      node;                      // RTU node

// FIX: Add a buffer to store Modbus response registers
uint16_t modbusResponseBuffer[MAX_FLOATS * 2];

// Runtime Modbus data
float    values[MAX_FLOATS];
float    prevValues[MAX_FLOATS];
bool     valueValid[MAX_FLOATS];
bool     modbusOk    = false;
uint8_t  errorStreak = 0;
uint32_t lastReadMs  = 0;
uint32_t readCount   = 0;
uint32_t errorCount  = 0;
uint16_t mbapTxId    = 0;           // Modbus TCP transaction ID counter

// WiFi state
bool     staConnected   = false;
bool     staFallback    = false;     // true = tried STA, fell back to AP
uint32_t lastStaRetryMs = 0;

/* ----------------------------------------------------------------
   Persisted Modbus config (EEPROM 0-255)
   ---------------------------------------------------------------- */
uint8_t  slaveId;
uint8_t  funcCode;
uint16_t startAddr;
uint8_t  regCount;
uint8_t  pollSec;
RegType  regTypes[MAX_FLOATS];
String   labels[MAX_FLOATS];
String   units[MAX_FLOATS];
uint8_t  decimals[MAX_FLOATS];

/* ----------------------------------------------------------------
   Persisted WiFi config (EEPROM 256-354)
   ---------------------------------------------------------------- */
// offsets
#define EE_WIFI_BASE   256
#define EE_WIFI_SSID   (EE_WIFI_BASE + 0)    // 33 bytes
#define EE_WIFI_PASS   (EE_WIFI_BASE + 33)   // 65 bytes
#define EE_WIFI_FLAGS  (EE_WIFI_BASE + 98)   // 1 byte: bit0=STA enable

char     wifiSsid[33];
char     wifiPass[65];
bool     wifiStaEnable;

/* ----------------------------------------------------------------
   Persisted Modbus TCP config (EEPROM 355-373)
   ---------------------------------------------------------------- */
#define EE_MTCP_BASE   355
#define EE_MTCP_IP     (EE_MTCP_BASE + 0)    // 16 bytes (dotted string)
#define EE_MTCP_PORT   (EE_MTCP_BASE + 16)   // 2 bytes uint16
#define EE_MTCP_EN     (EE_MTCP_BASE + 18)   // 1 byte

char     modbusTcpIp[16];
uint16_t modbusTcpPort;
bool     modbusTcpEnable;

/* ================================================================
   EEPROM helpers (label/unit region is same as V2.0)
   ================================================================ */

#define EEPROM_LABEL_BASE  32
#define EEPROM_UNIT_BASE   (EEPROM_LABEL_BASE + MAX_FLOATS * LABEL_SIZE)

static void eepromWriteStr(int base, const char *s, int maxLen) {
  for (int i = 0; i < maxLen; i++)
    EEPROM.write(base + i, (i < (int)strlen(s)) ? (uint8_t)s[i] : 0);
}

static void eepromWriteStr(int base, const String &s, int maxLen) {
  eepromWriteStr(base, s.c_str(), maxLen);
}

static void eepromReadStr(int base, char *dst, int maxLen) {
  for (int i = 0; i < maxLen; i++) {
    char c = (char)EEPROM.read(base + i);
    dst[i] = (c >= 0x20 && c <= 0x7E) ? c : (c == '\0' ? '\0' : '?');
  }
  dst[maxLen - 1] = '\0';
}

static String eepromReadString(int base, int maxLen) {
  char buf[maxLen + 1];
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

  /* ---- Modbus RTU config ---- */
  if (fresh) {
    slaveId   = 1;
    funcCode  = 4;
    pollSec   = 2;
    startAddr = 0;
    regCount  = MAX_FLOATS * 2;
    for (int i = 0; i < MAX_FLOATS; i++) {
      regTypes[i] = RT_FLOAT32_AB;
      decimals[i] = 3;
      labels[i]   = "Reg " + String(i * 2);
      units[i]    = "";
    }
  } else {
    slaveId   = EEPROM.read(1);
    funcCode  = EEPROM.read(2);
    pollSec   = EEPROM.read(3);
    EEPROM.get(4, startAddr);
    regCount  = EEPROM.read(6);

    if (slaveId  == 0 || slaveId  > 247)             slaveId  = 1;
    if (funcCode != 3 && funcCode != 4)               funcCode = 4;
    if (pollSec  == 0)                                pollSec  = 2;
    if (regCount == 0 || regCount > MAX_FLOATS * 2)   regCount = MAX_FLOATS * 2;
    if (regCount % 2 != 0) regCount++;

    for (int i = 0; i < MAX_FLOATS; i++) {
      uint8_t rt  = EEPROM.read(7 + i);
      regTypes[i] = (rt <= RT_UINT16) ? (RegType)rt : RT_FLOAT32_AB;
      decimals[i] = EEPROM.read(13 + i);
      if (decimals[i] > 6) decimals[i] = 3;
      labels[i]   = eepromReadString(EEPROM_LABEL_BASE + i * LABEL_SIZE, LABEL_SIZE);
      units[i]    = eepromReadString(EEPROM_UNIT_BASE  + i * UNIT_SIZE,  UNIT_SIZE);
      if (labels[i].length() == 0)
        labels[i] = "Reg " + String(startAddr + i * 2);
    }
  }

  /* ---- WiFi STA config ---- */
  eepromReadStr(EE_WIFI_SSID, wifiSsid, sizeof(wifiSsid));
  eepromReadStr(EE_WIFI_PASS, wifiPass, sizeof(wifiPass));
  uint8_t wflags = EEPROM.read(EE_WIFI_FLAGS);
  wifiStaEnable  = (wflags & 0x01) && (strlen(wifiSsid) > 0);

  /* ---- Modbus TCP config ---- */
  eepromReadStr(EE_MTCP_IP, modbusTcpIp, sizeof(modbusTcpIp));
  EEPROM.get(EE_MTCP_PORT, modbusTcpPort);
  modbusTcpEnable = (bool)EEPROM.read(EE_MTCP_EN);

  if (modbusTcpPort == 0 || modbusTcpPort == 0xFFFF) modbusTcpPort = MODBUS_TCP_PORT;
  if (strlen(modbusTcpIp) == 0) {
    strncpy(modbusTcpIp, "192.168.1.100", sizeof(modbusTcpIp));
    modbusTcpEnable = false;
  }

  if (fresh) saveConfig();
}

void saveConfig() {
  EEPROM.write(0, EEPROM_MAGIC);
  EEPROM.write(1, slaveId);
  EEPROM.write(2, funcCode);
  EEPROM.write(3, pollSec);
  EEPROM.put(4, startAddr);
  EEPROM.write(6, regCount);

  for (int i = 0; i < MAX_FLOATS; i++) {
    EEPROM.write(7  + i, (uint8_t)regTypes[i]);
    EEPROM.write(13 + i, decimals[i]);
    eepromWriteStr(EEPROM_LABEL_BASE + i * LABEL_SIZE, labels[i], LABEL_SIZE);
    eepromWriteStr(EEPROM_UNIT_BASE  + i * UNIT_SIZE,  units[i],  UNIT_SIZE);
  }

  // WiFi
  eepromWriteStr(EE_WIFI_SSID, wifiSsid, sizeof(wifiSsid));
  eepromWriteStr(EE_WIFI_PASS, wifiPass, sizeof(wifiPass));
  EEPROM.write(EE_WIFI_FLAGS, wifiStaEnable ? 0x01 : 0x00);

  // Modbus TCP
  eepromWriteStr(EE_MTCP_IP, modbusTcpIp, sizeof(modbusTcpIp));
  EEPROM.put(EE_MTCP_PORT, modbusTcpPort);
  EEPROM.write(EE_MTCP_EN, (uint8_t)modbusTcpEnable);

  EEPROM.commit();
}

/* ================================================================
   WIFI INIT  (STA with AP fallback)
   ================================================================ */

void initWiFi() {
  // Always start the AP so the config UI is reachable during STA setup
  WiFi.softAP(AP_SSID, AP_PASS);

  if (!wifiStaEnable || strlen(wifiSsid) == 0) {
    WiFi.mode(WIFI_AP);
    staFallback  = false;
    staConnected = false;
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(wifiSsid, wifiPass);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < STA_CONNECT_TIMEOUT) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    staConnected = true;
    staFallback  = false;
  } else {
    // STA failed → fall back to AP-only
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    staConnected = false;
    staFallback  = true;
  }
}

/* Periodic STA reconnect check (called from loop) */
void maintainWiFi() {
  if (!wifiStaEnable) return;
  if (staConnected && WiFi.status() == WL_CONNECTED) return;

  // Already connected — just update flag
  if (WiFi.status() == WL_CONNECTED) {
    staConnected = true;
    staFallback  = false;
    return;
  }

  // Not connected — throttle retry
  if (millis() - lastStaRetryMs < STA_RECONNECT_MS) return;
  lastStaRetryMs = millis();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.begin(wifiSsid, wifiPass);
  // Result checked next iteration
  staConnected = false;
}

/* ================================================================
   MODBUS TCP  (manual MBAP framing, no extra library)
   ================================================================ */

/*
 *  Modbus TCP ADU layout:
 *  [0-1] Transaction ID
 *  [2-3] Protocol ID = 0x0000
 *  [4-5] Length (bytes following)
 *  [6]   Unit ID (slave)
 *  [7]   Function code
 *  [8-9] Start address
 *  [10-11] Quantity
 */

bool modbusTcpConnect() {
  if (modbusTcpClient.connected()) return true;
  modbusTcpClient.stop();
  IPAddress ip;
  if (!ip.fromString(modbusTcpIp)) return false;
  return modbusTcpClient.connect(ip, modbusTcpPort);
}

/* Returns true and fills response buffer on success */
bool modbusTcpRead(uint8_t fc, uint16_t addr, uint8_t qty) {
  if (!modbusTcpConnect()) return false;

  mbapTxId++;
  uint8_t req[12];
  req[0]  = mbapTxId >> 8;
  req[1]  = mbapTxId & 0xFF;
  req[2]  = 0x00; req[3] = 0x00;   // protocol
  req[4]  = 0x00; req[5] = 0x06;   // length
  req[6]  = slaveId;
  req[7]  = fc;
  req[8]  = addr >> 8;
  req[9]  = addr & 0xFF;
  req[10] = 0x00;
  req[11] = qty;

  if (modbusTcpClient.write(req, sizeof(req)) != sizeof(req)) {
    modbusTcpClient.stop();
    return false;
  }

  // Wait for response header (9 bytes: MBAP 7 + FC 1 + byte-count 1)
  uint32_t t0 = millis();
  while (modbusTcpClient.available() < 9) {
    if (millis() - t0 > MODBUS_TCP_TIMEOUT) {
      modbusTcpClient.stop();
      return false;
    }
    delay(1);
  }

  uint8_t hdr[9];
  modbusTcpClient.read(hdr, 9);

  // Validate transaction ID and protocol
  if (((hdr[0] << 8) | hdr[1]) != mbapTxId) { modbusTcpClient.stop(); return false; }
  if (((hdr[2] << 8) | hdr[3]) != 0x0000)   { modbusTcpClient.stop(); return false; }
  if (hdr[7] & 0x80) { modbusTcpClient.stop(); return false; } // exception

  uint8_t byteCount = hdr[8];
  uint8_t data[byteCount];

  t0 = millis();
  while (modbusTcpClient.available() < byteCount) {
    if (millis() - t0 > MODBUS_TCP_TIMEOUT) { modbusTcpClient.stop(); return false; }
    delay(1);
  }
  modbusTcpClient.read(data, byteCount);

  // FIX: Store into our buffer instead of using setResponseBuffer
  for (int i = 0; i < byteCount / 2 && i < qty; i++) {
    uint16_t reg = ((uint16_t)data[i * 2] << 8) | data[i * 2 + 1];
    if (i < MAX_FLOATS * 2) {
      modbusResponseBuffer[i] = reg;
    }
  }
  return true;
}

/* ================================================================
   MODBUS RTU READ with buffer storage
   ================================================================ */

bool modbusRtuRead() {
  uint8_t res;
  if (funcCode == 3)
    res = node.readHoldingRegisters(startAddr, regCount);
  else
    res = node.readInputRegisters(startAddr, regCount);
  
  if (res == node.ku8MBSuccess) {
    // Copy from ModbusMaster to our buffer
    for (int i = 0; i < regCount && i < MAX_FLOATS * 2; i++) {
      modbusResponseBuffer[i] = node.getResponseBuffer(i);
    }
    return true;
  }
  return false;
}

/* ================================================================
   MODBUS READ  (RTU + TCP with retry, shared decode)
   ================================================================ */

void decodeRegisters() {
  int numValues = min((int)(regCount / 2), MAX_FLOATS);
  for (int i = 0; i < numValues; i++) {
    prevValues[i] = values[i];
    switch (regTypes[i]) {
      case RT_FLOAT32_AB: {
        uint32_t raw = ((uint32_t)modbusResponseBuffer[i * 2] << 16)
                     | (uint32_t)modbusResponseBuffer[i * 2 + 1];
        memcpy(&values[i], &raw, sizeof(float));
        break;
      }
      case RT_FLOAT32_BA: {
        uint32_t raw = ((uint32_t)modbusResponseBuffer[i * 2 + 1] << 16)
                     | (uint32_t)modbusResponseBuffer[i * 2];
        memcpy(&values[i], &raw, sizeof(float));
        break;
      }
      case RT_INT16:
        values[i] = (float)(int16_t)modbusResponseBuffer[i * 2];
        break;
      case RT_UINT16:
        values[i] = (float)modbusResponseBuffer[i * 2];
        break;
    }
    // FIX: isfinite is now available via <cmath>
    if (!std::isfinite(values[i])) values[i] = 0.0f;
    valueValid[i] = true;
  }
}

void readModbus() {
  bool success = false;
  lastReadMs = millis();
  readCount++;

  /* ---- Try Modbus TCP first if enabled ---- */
  if (modbusTcpEnable) {
    for (int attempt = 0; attempt < MODBUS_RETRIES && !success; attempt++) {
      success = modbusTcpRead(funcCode, startAddr, regCount);
      if (!success && attempt < MODBUS_RETRIES - 1) delay(MODBUS_RETRY_MS);
    }
  }

  /* ---- Fall back to RTU if TCP failed or disabled ---- */
  if (!success) {
    for (int attempt = 0; attempt < MODBUS_RETRIES && !success; attempt++) {
      success = modbusRtuRead();
      if (!success && attempt < MODBUS_RETRIES - 1) delay(MODBUS_RETRY_MS);
    }
  }

  if (success) {
    modbusOk    = true;
    errorStreak = 0;
    decodeRegisters();
    broadcastTcpClients();   // push to any connected raw-socket clients
  } else {
    errorStreak++;
    errorCount++;
    if (errorStreak >= MODBUS_RETRIES) modbusOk = false;
  }
}

/* ================================================================
   RAW TCP SOCKET SERVER  (JSON stream on port 8502)
   ================================================================ */

void broadcastTcpClients() {
  // Build compact JSON line
  String line = "{\"t\":" + String(millis())
              + ",\"ok\":" + (modbusOk ? "true" : "false")
              + ",\"v\":[";

  int numValues = min((int)(regCount / 2), MAX_FLOATS);
  for (int i = 0; i < numValues; i++) {
    if (i) line += ',';
    if (valueValid[i]) line += String(values[i], 4);
    else               line += "null";
  }
  line += "]}\n";

  for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
    if (tcpClients[i] && tcpClients[i].connected()) {
      tcpClients[i].print(line);
    }
  }
}

void handleTcpClients() {
  // Accept new connections into free slots
  if (tcpServer.hasClient()) {
    WiFiClient newClient = tcpServer.available();
    bool accepted = false;
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
      if (!tcpClients[i] || !tcpClients[i].connected()) {
        tcpClients[i].stop();
        tcpClients[i] = newClient;
        accepted = true;
        break;
      }
    }
    if (!accepted) newClient.stop(); // no slot available
  }
}

/* ================================================================
   CORS + OPTIONS
   ================================================================ */

void addCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin",  "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleOptions() {
  addCORSHeaders();
  server.send(204);
}

/* ================================================================
   HTTP API HANDLERS
   ================================================================ */

/* GET /api/data */
void apiData() {
  addCORSHeaders();
  DynamicJsonDocument doc(768);
  int numValues = min((int)(regCount / 2), MAX_FLOATS);
  doc["ok"]     = modbusOk;
  doc["count"]  = numValues;
  doc["reads"]  = readCount;
  doc["errors"] = errorCount;
  doc["source"] = modbusTcpEnable ? "tcp" : "rtu";

  JsonArray arr = doc.createNestedArray("registers");
  for (int i = 0; i < numValues; i++) {
    JsonObject obj = arr.createNestedObject();
    obj["label"] = labels[i];
    obj["unit"]  = units[i];
    obj["value"] = valueValid[i] ? values[i]    : 0.0f;
    obj["prev"]  = valueValid[i] ? prevValues[i] : 0.0f;
    obj["valid"] = valueValid[i];
    obj["dec"]   = decimals[i];
    obj["type"]  = (uint8_t)regTypes[i];
    obj["addr"]  = startAddr + i * 2;
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

/* GET /api/config */
void apiGetConfig() {
  addCORSHeaders();
  DynamicJsonDocument doc(640);
  doc["slave"]       = slaveId;
  doc["func"]        = funcCode;
  doc["start"]       = startAddr;
  doc["count"]       = regCount;
  doc["poll"]        = pollSec;
  doc["tcpEnable"]   = modbusTcpEnable;
  doc["tcpIp"]       = modbusTcpIp;
  doc["tcpPort"]     = modbusTcpPort;
  JsonArray ta = doc.createNestedArray("types");
  JsonArray da = doc.createNestedArray("decimals");
  for (int i = 0; i < MAX_FLOATS; i++) { ta.add((uint8_t)regTypes[i]); da.add(decimals[i]); }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

/* POST /api/config */
void apiSetConfig() {
  addCORSHeaders();
  DynamicJsonDocument doc(640);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"JSON parse failed\"}"); return;
  }

  uint8_t  ns = doc["slave"]  | 0;
  uint8_t  nf = doc["func"]   | 0;
  uint16_t na = doc["start"]  | 0;
  uint8_t  nc = doc["count"]  | 0;
  uint8_t  np = doc["poll"]   | 1;

  if (ns < 1 || ns > 247) { server.send(400,"application/json","{\"error\":\"slaveId 1-247\"}"); return; }
  if (nf != 3 && nf != 4) { server.send(400,"application/json","{\"error\":\"funcCode 3 or 4\"}"); return; }
  if (nc == 0 || nc > MAX_FLOATS*2 || nc%2!=0) { server.send(400,"application/json","{\"error\":\"count: 2-12 even\"}"); return; }
  if (np < 1 || np > 60) { server.send(400,"application/json","{\"error\":\"poll 1-60\"}"); return; }

  slaveId   = ns; funcCode = nf; startAddr = na; regCount = nc; pollSec = np;

  if (doc.containsKey("tcpEnable")) modbusTcpEnable = doc["tcpEnable"].as<bool>();
  if (doc.containsKey("tcpIp"))     strncpy(modbusTcpIp, doc["tcpIp"].as<const char*>(), sizeof(modbusTcpIp)-1);
  if (doc.containsKey("tcpPort"))   modbusTcpPort = doc["tcpPort"].as<uint16_t>();

  if (doc.containsKey("types")) {
    JsonArray ta = doc["types"].as<JsonArray>(); int i=0;
    for (uint8_t t : ta) { if(i>=MAX_FLOATS)break; regTypes[i++]=(t<=RT_UINT16)?(RegType)t:RT_FLOAT32_AB; }
  }
  if (doc.containsKey("decimals")) {
    JsonArray da = doc["decimals"].as<JsonArray>(); int i=0;
    for (uint8_t d : da) { if(i>=MAX_FLOATS)break; decimals[i++]=(d<=6)?d:3; }
  }

  saveConfig();
  node.begin(slaveId, Serial);
  modbusTcpClient.stop();   // force reconnect with new settings

  server.send(200, "application/json", "{\"saved\":true}");
  delay(200);
  ESP.restart();
}

/* POST /api/labels */
void apiSetLabels() {
  addCORSHeaders();
  DynamicJsonDocument doc(640);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"JSON parse failed\"}"); return;
  }
  if (doc.containsKey("labels")) {
    JsonArray la = doc["labels"].as<JsonArray>(); int i=0;
    for (String s : la) { if(i>=MAX_FLOATS)break; s.trim(); labels[i++]=s.substring(0,LABEL_SIZE-1); }
  }
  if (doc.containsKey("units")) {
    JsonArray ua = doc["units"].as<JsonArray>(); int i=0;
    for (String s : ua) { if(i>=MAX_FLOATS)break; s.trim(); units[i++]=s.substring(0,UNIT_SIZE-1); }
  }
  saveConfig();
  server.send(200, "application/json", "{\"saved\":true}");
}

/* GET /api/wifi */
void apiGetWifi() {
  addCORSHeaders();
  DynamicJsonDocument doc(384);
  doc["staEnable"]  = wifiStaEnable;
  doc["ssid"]       = wifiSsid;
  doc["connected"]  = staConnected;
  doc["fallback"]   = staFallback;
  doc["staIp"]      = staConnected ? WiFi.localIP().toString() : "";
  doc["apIp"]       = WiFi.softAPIP().toString();
  doc["rssi"]       = staConnected ? WiFi.RSSI() : 0;
  doc["apClients"]  = WiFi.softAPgetStationNum();
  doc["rawPort"]    = RAW_TCP_PORT;
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

/* POST /api/wifi  { "ssid":"...", "pass":"...", "enable": true } */
void apiSetWifi() {
  addCORSHeaders();
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"JSON parse failed\"}"); return;
  }

  if (doc.containsKey("ssid")) {
    const char *s = doc["ssid"].as<const char*>();
    if (strlen(s) > 32) { server.send(400,"application/json","{\"error\":\"SSID max 32 chars\"}"); return; }
    strncpy(wifiSsid, s, sizeof(wifiSsid)-1);
  }
  if (doc.containsKey("pass")) {
    const char *p = doc["pass"].as<const char*>();
    if (strlen(p) > 0 && strlen(p) < 8) { server.send(400,"application/json","{\"error\":\"Password min 8 chars\"}"); return; }
    strncpy(wifiPass, p, sizeof(wifiPass)-1);
  }
  if (doc.containsKey("enable")) wifiStaEnable = doc["enable"].as<bool>();

  saveConfig();
  server.send(200, "application/json", "{\"saved\":true}");
  delay(200);
  ESP.restart();
}

/* GET /api/status */
void apiStatus() {
  addCORSHeaders();
  DynamicJsonDocument doc(256);
  doc["uptime"]    = millis() / 1000;
  doc["freeHeap"]  = ESP.getFreeHeap();
  doc["apClients"] = WiFi.softAPgetStationNum();
  doc["staConn"]   = staConnected;
  doc["ok"]        = modbusOk;
  doc["reads"]     = readCount;
  doc["errors"]    = errorCount;
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

/* ================================================================
   DASHBOARD HTML (PROGMEM) - same as original
   ================================================================ */

const char DASHBOARD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Modbus Dashboard</title>
<style>
:root{
  --primary:#3b82f6;--primary-dark:#1d4ed8;
  --bg:#0f172a;--card:#1e293b;
  --text:#f8fafc;--muted:#94a3b8;
  --ok:#10b981;--err:#ef4444;--warn:#f59e0b;--info:#6366f1;
  --border:#334155;--shadow:0 4px 6px -1px rgba(0,0,0,.35);
}
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',system-ui,sans-serif}
body{background:var(--bg);color:var(--text);min-height:100vh;padding:20px}
.wrap{max-width:1200px;margin:0 auto}

/* Header */
.hdr{display:flex;justify-content:space-between;align-items:center;
  margin-bottom:22px;padding-bottom:18px;border-bottom:1px solid var(--border);flex-wrap:wrap;gap:12px}
h1{font-size:1.8rem;font-weight:700;
  background:linear-gradient(135deg,var(--primary),#8b5cf6);
  -webkit-background-clip:text;-webkit-text-fill-color:transparent}
.ctrl{display:flex;gap:10px;flex-wrap:wrap}

/* Badges */
.badge{display:inline-flex;align-items:center;gap:6px;
  padding:5px 12px;border-radius:20px;font-size:.78rem;font-weight:600}
.badge.ok  {background:rgba(16,185,129,.15);color:var(--ok);border:1px solid rgba(16,185,129,.3)}
.badge.err {background:rgba(239,68,68,.15);color:var(--err);border:1px solid rgba(239,68,68,.3)}
.badge.warn{background:rgba(245,158,11,.15);color:var(--warn);border:1px solid rgba(245,158,11,.3)}
.badge.info{background:rgba(99,102,241,.15);color:var(--info);border:1px solid rgba(99,102,241,.3)}
.dot{width:8px;height:8px;border-radius:50%}
.ok  .dot{background:var(--ok);animation:pulse 1.4s ease-in-out infinite}
.err .dot{background:var(--err)}
.warn.dot{background:var(--warn)}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}

/* Buttons */
.btn{padding:9px 16px;border:none;border-radius:8px;font-weight:600;
  cursor:pointer;transition:all .2s;font-size:.85rem}
.btn-p{background:var(--primary);color:#fff}
.btn-p:hover{background:var(--primary-dark);transform:translateY(-1px)}
.btn-s{background:var(--card);color:var(--text);border:1px solid var(--border)}
.btn-s:hover{background:#2d3748}
.btn-d{background:#ef4444;color:#fff}
.btn-d:hover{background:#dc2626}

/* Info bar */
.infobar{display:flex;flex-wrap:wrap;gap:16px;background:var(--card);
  border-radius:10px;padding:16px;border:1px solid var(--border);margin-bottom:20px}
.iitem{display:flex;flex-direction:column;gap:3px;min-width:90px}
.ilabel{font-size:.72rem;text-transform:uppercase;letter-spacing:.07em;color:var(--muted)}
.ivalue{font-size:.95rem;font-weight:600}

/* Network bar */
.netbar{display:flex;flex-wrap:wrap;gap:10px;margin-bottom:20px;align-items:center}

/* Grid */
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(268px,1fr));
  gap:16px;margin-bottom:22px}
.card{background:var(--card);border-radius:12px;padding:20px;
  box-shadow:var(--shadow);border:1px solid var(--border);transition:transform .2s,box-shadow .2s}
.card:hover{transform:translateY(-3px);box-shadow:0 12px 28px rgba(0,0,0,.35)}
.card-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px}
.card-title{font-size:.95rem;font-weight:600}
.card-edit{background:none;border:none;color:var(--primary);cursor:pointer;font-size:.8rem;display:flex;align-items:center;gap:3px}
.card-val{font-size:2rem;font-weight:700;color:var(--ok);text-align:center;
  margin:12px 0;font-family:'Monaco','Courier New',monospace;letter-spacing:-.5px}
.card-val.invalid{color:var(--muted);font-size:1.3rem}
.card-ftr{display:flex;justify-content:space-between;font-size:.78rem;
  color:var(--muted);padding-top:8px;border-top:1px solid var(--border)}
.trend.up{color:var(--ok)}.trend.down{color:var(--err)}.trend.flat{color:var(--muted)}

/* Modals */
.modal{display:none;position:fixed;inset:0;background:rgba(0,0,0,.78);
  justify-content:center;align-items:center;z-index:900}
.modal.active{display:flex}
.mc{background:var(--card);padding:26px;border-radius:14px;
  width:92%;max-width:540px;max-height:88vh;overflow-y:auto}
.mt{font-size:1.25rem;font-weight:700;margin-bottom:20px}
.fg{margin-bottom:16px}
label{display:block;margin-bottom:5px;font-weight:600;font-size:.87rem}
input,select{width:100%;padding:10px;background:#0f172a;border:1px solid var(--border);
  border-radius:8px;color:var(--text);font-size:.93rem}
input:focus,select:focus{outline:none;border-color:var(--primary);
  box-shadow:0 0 0 3px rgba(59,130,246,.15)}
.frow{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.fa{display:flex;gap:10px;justify-content:flex-end;margin-top:8px}
.sl{font-size:.72rem;text-transform:uppercase;letter-spacing:.07em;color:var(--muted);
  margin:14px 0 8px;padding-bottom:5px;border-bottom:1px solid var(--border)}
.toggle-row{display:flex;align-items:center;justify-content:space-between;
  padding:10px 0;border-bottom:1px solid var(--border)}
.toggle{position:relative;width:44px;height:24px}
.toggle input{opacity:0;width:0;height:0}
.slider{position:absolute;inset:0;background:#334155;border-radius:24px;cursor:pointer;transition:.3s}
.slider:before{content:'';position:absolute;width:18px;height:18px;left:3px;bottom:3px;
  background:white;border-radius:50%;transition:.3s}
input:checked+.slider{background:var(--primary)}
input:checked+.slider:before{transform:translateX(20px)}

/* Toast */
.toast{position:fixed;bottom:18px;right:18px;padding:11px 20px;border-radius:8px;
  box-shadow:var(--shadow);opacity:0;transform:translateY(14px);
  transition:all .3s;font-weight:600;font-size:.88rem;z-index:2000}
.toast.show{opacity:1;transform:translateY(0)}
.toast.ok{background:var(--ok);color:#fff}
.toast.err{background:var(--err);color:#fff}
.toast.warn{background:var(--warn);color:#000}

/* Spinner */
.sp{width:16px;height:16px;border:2px solid rgba(255,255,255,.3);
  border-top-color:#fff;border-radius:50%;animation:spin .7s linear infinite;display:inline-block}
@keyframes spin{to{transform:rotate(360deg)}}

@media(max-width:600px){
  .hdr{flex-direction:column;align-items:flex-start}
  .frow{grid-template-columns:1fr}
}
</style>
</head>
<body>
<div class="wrap">

  <!-- Header -->
  <div class="hdr">
    <div style="display:flex;align-items:center;gap:14px;flex-wrap:wrap">
      <h1>&#x1F4CA; Modbus Dashboard</h1>
      <span class="badge err" id="mbBadge"><span class="dot"></span><span id="mbText">Connecting…</span></span>
    </div>
    <div class="ctrl">
      <button class="btn btn-s" onclick="openModal('wifiModal')">&#x1F4F6; WiFi</button>
      <button class="btn btn-s" onclick="openModal('cfgModal')">&#x2699; Config</button>
      <button class="btn btn-p" onclick="openModal('lblModal')">&#x270F; Labels</button>
    </div>
  </div>

  <!-- Network bar -->
  <div class="netbar" id="netbar">
    <span class="badge info" id="wifiBadge">&#x1F4F6; Loading…</span>
    <span class="badge info" id="tcpBadge">&#x1F5A7; RTU</span>
    <span class="badge info" id="streamBadge">&#x1F4E1; Stream :8502</span>
  </div>

  <!-- System info bar -->
  <div class="infobar">
    <div class="iitem"><span class="ilabel">Slave ID</span><span class="ivalue" id="iSlave">—</span></div>
    <div class="iitem"><span class="ilabel">Function</span><span class="ivalue" id="iFunc">—</span></div>
    <div class="iitem"><span class="ilabel">Start Addr</span><span class="ivalue" id="iStart">—</span></div>
    <div class="iitem"><span class="ilabel">Poll</span><span class="ivalue" id="iPoll">—</span></div>
    <div class="iitem"><span class="ilabel">Reads / Errors</span><span class="ivalue" id="iReads">—</span></div>
    <div class="iitem"><span class="ilabel">Source</span><span class="ivalue" id="iSource">—</span></div>
    <div class="iitem"><span class="ilabel">Last Update</span><span class="ivalue" id="iTime">—</span></div>
  </div>

  <!-- Data cards -->
  <div class="grid" id="grid">
    <div class="card" style="grid-column:1/-1;text-align:center;padding:40px">
      <div class="sp" style="margin:0 auto 12px"></div>
      <div style="color:var(--muted)">Connecting to device…</div>
    </div>
  </div>
</div>

<!-- ===== WIFI MODAL ===== -->
<div class="modal" id="wifiModal">
  <div class="mc">
    <div class="mt">&#x1F4F6; WiFi / Network Settings</div>

    <div class="toggle-row">
      <span style="font-weight:600">Connect to existing WiFi (STA mode)</span>
      <label class="toggle"><input type="checkbox" id="wStaEn"><span class="slider"></span></label>
    </div>

    <div id="staFields" style="margin-top:14px">
      <div class="fg">
        <label>Network SSID</label>
        <input type="text" id="wSsid" maxlength="32" placeholder="Your router SSID">
      </div>
      <div class="fg">
        <label>Password <span style="color:var(--muted);font-weight:normal">(leave blank to keep current)</span></label>
        <input type="password" id="wPass" maxlength="64" placeholder="Min 8 characters">
      </div>
      <div style="background:#0f172a;border-radius:8px;padding:12px;font-size:.82rem;color:var(--muted);margin-bottom:14px">
        &#x2139;&#xFE0F; If STA fails, device falls back to AP mode automatically.<br>
        AP: <strong>SysCon</strong> / <strong>12345678</strong> &nbsp;|&nbsp;
        AP IP: <strong id="wApIp">192.168.4.1</strong>
      </div>
    </div>

    <div class="sl">Modbus TCP Slave</div>
    <div class="toggle-row">
      <span style="font-weight:600">Enable Modbus TCP</span>
      <label class="toggle"><input type="checkbox" id="wTcpEn"><span class="slider"></span></label>
    </div>
    <div style="margin-top:12px">
      <div class="frow">
        <div class="fg">
          <label>TCP Slave IP</label>
          <input type="text" id="wTcpIp" maxlength="15" placeholder="192.168.1.100">
        </div>
        <div class="fg">
          <label>TCP Port</label>
          <input type="number" id="wTcpPort" min="1" max="65535" value="502">
        </div>
      </div>
      <div style="font-size:.8rem;color:var(--muted);margin-top:-8px;margin-bottom:12px">
        TCP is tried first; RTU Serial is used as fallback when TCP is enabled.
      </div>
    </div>

    <div class="sl">Raw JSON Stream (TCP port 8502)</div>
    <div style="font-size:.82rem;color:var(--muted);margin-bottom:16px">
      Connect any TCP client (e.g. Node-RED, Python, SCADA) to port 8502.<br>
      Each poll pushes one JSON line: <code style="color:var(--ok)">{"t":ms,"ok":bool,"v":[...values]}</code>
    </div>

    <div class="fa">
      <button class="btn btn-s" onclick="closeModal('wifiModal')">Cancel</button>
      <button class="btn btn-p" onclick="saveWifi()" id="saveWifiBtn">Save &amp; Restart</button>
    </div>
  </div>
</div>

<!-- ===== CONFIG MODAL ===== -->
<div class="modal" id="cfgModal">
  <div class="mc">
    <div class="mt">&#x2699; Modbus Configuration</div>
    <div class="frow">
      <div class="fg"><label>Slave ID (1-247)</label><input type="number" id="cSlave" min="1" max="247"></div>
      <div class="fg"><label>Function Code</label>
        <select id="cFunc">
          <option value="3">3 – Holding Regs</option>
          <option value="4">4 – Input Regs</option>
        </select>
      </div>
    </div>
    <div class="frow">
      <div class="fg"><label>Start Address</label><input type="number" id="cStart" min="0" max="65535"></div>
      <div class="fg"><label>Register Count (even, ≤12)</label><input type="number" id="cCount" min="2" max="12" step="2"></div>
    </div>
    <div class="fg"><label>Poll Interval (s)</label><input type="number" id="cPoll" min="1" max="60"></div>
    <div class="sl">Per-Register Settings</div>
    <div id="perReg"></div>
    <div class="fa">
      <button class="btn btn-s" onclick="closeModal('cfgModal')">Cancel</button>
      <button class="btn btn-p" onclick="saveCfg()" id="saveCfgBtn">Save &amp; Restart</button>
    </div>
  </div>
</div>

<!-- ===== LABELS MODAL ===== -->
<div class="modal" id="lblModal">
  <div class="mc">
    <div class="mt">&#x270F; Labels &amp; Units</div>
    <div id="lblCont"></div>
    <div class="fa">
      <button class="btn btn-s" onclick="closeModal('lblModal')">Cancel</button>
      <button class="btn btn-p" onclick="saveLabels()">Save Labels</button>
    </div>
  </div>
</div>

<div class="toast" id="toast"></div>

<script>
/* ===================== STATE ===================== */
const TYPES = ['Float32 AB','Float32 BA','Int16','Uint16'];
let config  = {};
let wifiInfo= {};
let regData = [];
let consecutiveFails = 0;

/* ===================== BOOT ===================== */
(async()=>{
  await Promise.all([loadConfig(), loadWifi()]);
  await loadData();
  scheduleNextPoll();
})();

/* ===================== POLL LOOP ===================== */
function scheduleNextPoll(){
  const ms = (config.poll||2)*1000;
  setTimeout(async()=>{ await loadData(); scheduleNextPoll(); }, ms);
}

async function loadData(){
  try{
    const r = await fetch('/api/data');
    if(!r.ok) throw new Error(r.status);
    const d = await r.json();
    regData = d.registers||[];
    consecutiveFails = 0;
    setMbBadge(d.ok, d.ok?'Modbus OK':'Modbus Fault');
    document.getElementById('iReads').textContent = d.reads+' / '+d.errors;
    document.getElementById('iSource').textContent = (d.source==='tcp')?'Modbus TCP':'RTU Serial';
    document.getElementById('iTime').textContent = timeNow();
    renderCards();
  }catch(e){
    consecutiveFails++;
    if(consecutiveFails>=3){ setMbBadge(false,'No Response'); showToast('Device not responding','warn'); }
  }
}

async function loadConfig(){
  try{
    const r = await fetch('/api/config');
    config = await r.json();
    document.getElementById('iSlave').textContent = config.slave;
    document.getElementById('iFunc').textContent  = 'FC'+config.func;
    document.getElementById('iStart').textContent = '0x'+config.start.toString(16).toUpperCase().padStart(4,'0');
    document.getElementById('iPoll').textContent  = config.poll+'s';
    updateTcpBadge();
  }catch(e){}
}

async function loadWifi(){
  try{
    const r = await fetch('/api/wifi');
    wifiInfo = await r.json();
    updateWifiBadge();
    document.getElementById('wApIp').textContent = wifiInfo.apIp||'192.168.4.1';
  }catch(e){}
}

/* ===================== BADGES ===================== */
function setMbBadge(ok, text){
  const b=document.getElementById('mbBadge');
  b.className='badge '+(ok?'ok':'err');
  document.getElementById('mbText').textContent=text;
}

function updateWifiBadge(){
  const b=document.getElementById('wifiBadge');
  if(wifiInfo.connected){
    b.className='badge ok';
    b.innerHTML='&#x1F4F6; STA: '+wifiInfo.staIp+' ('+wifiInfo.rssi+'dBm)';
  } else if(wifiInfo.fallback){
    b.className='badge warn';
    b.innerHTML='&#x1F4F6; AP Fallback: '+wifiInfo.apIp;
  } else {
    b.className='badge info';
    b.innerHTML='&#x1F4F6; AP: '+(wifiInfo.apIp||'192.168.4.1');
  }
}

function updateTcpBadge(){
  const b=document.getElementById('tcpBadge');
  if(config.tcpEnable){
    b.className='badge ok';
    b.innerHTML='&#x1F5A7; TCP: '+config.tcpIp+':'+config.tcpPort;
  } else {
    b.className='badge info';
    b.innerHTML='&#x1F5A7; RTU Serial';
  }
}

/* ===================== CARDS ===================== */
function renderCards(){
  const g=document.getElementById('grid');
  g.innerHTML='';
  if(!regData.length){
    g.innerHTML='<div class="card" style="grid-column:1/-1;text-align:center;padding:40px;color:var(--muted)">No registers configured</div>';
    return;
  }
  regData.forEach((reg,i)=>{
    const tr=trend(reg.value,reg.prev);
    const card=document.createElement('div');
    card.className='card';
    card.innerHTML=`
      <div class="card-hdr">
        <div class="card-title">${esc(reg.label)}</div>
        <button class="card-edit" onclick="focusLabel(${i})">
          <svg width="13" height="13" viewBox="0 0 24 24" fill="currentColor">
            <path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zm20.71-12.67c.39-.39.39-1.02 0-1.41l-2.34-2.34a1 1 0 0 0-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/>
          </svg>Edit
        </button>
      </div>
      <div class="card-val${!reg.valid?' invalid':''}">${fmtVal(reg)}${reg.unit?'<span style="font-size:.9rem;margin-left:4px;color:var(--muted)">'+esc(reg.unit)+'</span>':''}</div>
      <div class="card-ftr">
        <span>0x${reg.addr.toString(16).toUpperCase().padStart(4,'0')} · ${TYPES[reg.type]||'Float32'}</span>
        <span class="trend ${tr.cls}">${tr.sym}</span>
      </div>`;
    g.appendChild(card);
  });
}

function fmtVal(r){ return r.valid?r.value.toFixed(r.dec??3):'---'; }
function trend(v,p){ const d=v-p; if(Math.abs(d)<1e-6)return{sym:'&#x2500;',cls:'flat'}; return d>0?{sym:'&#x25B2;',cls:'up'}:{sym:'&#x25BC;',cls:'down'}; }

/* ===================== WIFI MODAL ===================== */
function openModal(id){
  if(id==='wifiModal'){
    document.getElementById('wStaEn').checked  = wifiInfo.staEnable||false;
    document.getElementById('wSsid').value     = wifiInfo.ssid||'';
    document.getElementById('wPass').value     = '';
    document.getElementById('wTcpEn').checked  = config.tcpEnable||false;
    document.getElementById('wTcpIp').value    = config.tcpIp||'192.168.1.100';
    document.getElementById('wTcpPort').value  = config.tcpPort||502;
  }
  if(id==='cfgModal'){
    document.getElementById('cSlave').value = config.slave||1;
    document.getElementById('cFunc').value  = config.func||4;
    document.getElementById('cStart').value = config.start||0;
    document.getElementById('cCount').value = config.count||6;
    document.getElementById('cPoll').value  = config.poll||2;
    buildPerReg();
  }
  if(id==='lblModal') buildLabels();
  document.getElementById(id).classList.add('active');
}
function closeModal(id){ document.getElementById(id).classList.remove('active'); }

async function saveWifi(){
  const btn=document.getElementById('saveWifiBtn');
  btn.innerHTML='<span class="sp"></span>'; btn.disabled=true;

  // Save WiFi credentials
  const wPayload={
    enable: document.getElementById('wStaEn').checked,
    ssid:   document.getElementById('wSsid').value.trim(),
    pass:   document.getElementById('wPass').value
  };
  // Save TCP config as part of /api/config
  const cPayload={
    slave:     config.slave, func: config.func, start: config.start,
    count:     config.count, poll: config.poll,
    tcpEnable: document.getElementById('wTcpEn').checked,
    tcpIp:     document.getElementById('wTcpIp').value.trim(),
    tcpPort:   parseInt(document.getElementById('wTcpPort').value),
    types:     config.types||[], decimals: config.decimals||[]
  };

  try{
    const [r1,r2] = await Promise.all([
      fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(wPayload)}),
      fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cPayload)})
    ]);
    if(!r1.ok||!r2.ok) throw new Error('Server error');
    showToast('Saved — device restarting…','ok');
    setTimeout(()=>closeModal('wifiModal'),2500);
  }catch(e){
    showToast('Error: '+e.message,'err');
  }finally{
    btn.innerHTML='Save &amp; Restart'; btn.disabled=false;
  }
}

/* ===================== CONFIG MODAL ===================== */
function buildPerReg(){
  const n=Math.min((config.count||6)/2,6);
  const c=document.getElementById('perReg'); c.innerHTML='';
  for(let i=0;i<n;i++){
    const d=document.createElement('div');
    d.style.marginBottom='12px';
    d.innerHTML=`<div style="font-size:.8rem;color:var(--muted);margin-bottom:5px">Register ${i+1} (addr ${(config.start||0)+i*2})</div>
      <div class="frow">
        <div class="fg" style="margin:0"><label style="font-weight:normal">Data Type</label>
          <select id="pT${i}"><option value="0">Float32 AB</option><option value="1">Float32 BA</option>
            <option value="2">Int16</option><option value="3">Uint16</option></select></div>
        <div class="fg" style="margin:0"><label style="font-weight:normal">Decimals (0-6)</label>
          <input type="number" id="pD${i}" min="0" max="6" value="${(config.decimals||[])[i]??3}"></div>
      </div>`;
    if(config.types&&config.types[i]!=null) d.querySelector('#pT'+i).value=config.types[i];
    c.appendChild(d);
  }
}

async function saveCfg(){
  const btn=document.getElementById('saveCfgBtn');
  btn.innerHTML='<span class="sp"></span>'; btn.disabled=true;
  const n=Math.min(parseInt(document.getElementById('cCount').value)||6,12)/2;
  const types=[],decs=[];
  for(let i=0;i<n;i++){
    types.push(parseInt(document.getElementById('pT'+i)?.value||0));
    decs.push(parseInt(document.getElementById('pD'+i)?.value||3));
  }
  const payload={
    slave:parseInt(document.getElementById('cSlave').value),
    func:parseInt(document.getElementById('cFunc').value),
    start:parseInt(document.getElementById('cStart').value),
    count:parseInt(document.getElementById('cCount').value),
    poll:parseInt(document.getElementById('cPoll').value),
    tcpEnable:config.tcpEnable||false,
    tcpIp:config.tcpIp||'192.168.1.100',
    tcpPort:config.tcpPort||502,
    types, decimals:decs
  };
  try{
    const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
    const d=await r.json();
    if(!r.ok){showToast(d.error||'Error','err');return;}
    showToast('Saved — device restarting…','ok');
    setTimeout(()=>closeModal('cfgModal'),2500);
  }catch(e){showToast('Could not reach device','err');}
  finally{btn.innerHTML='Save &amp; Restart';btn.disabled=false;}
}

/* ===================== LABELS MODAL ===================== */
function buildLabels(){
  const c=document.getElementById('lblCont'); c.innerHTML='';
  regData.forEach((reg,i)=>{
    const d=document.createElement('div');
    d.className='fg';
    d.innerHTML=`<div class="sl">Reg ${i+1} · 0x${reg.addr.toString(16).toUpperCase().padStart(4,'0')}</div>
      <div class="frow">
        <div><label>Label</label><input type="text" id="lL${i}" value="${esc(reg.label)}" maxlength="23"></div>
        <div><label>Unit</label><input type="text" id="lU${i}" value="${esc(reg.unit||'')}" maxlength="7" placeholder="°C, bar…"></div>
      </div>`;
    c.appendChild(d);
  });
}

function focusLabel(i){ openModal('lblModal'); setTimeout(()=>{ const el=document.getElementById('lL'+i); if(el){el.focus();el.select();} },80); }

async function saveLabels(){
  const lbls=[],uns=[];
  regData.forEach((_,i)=>{ lbls.push(document.getElementById('lL'+i)?.value||''); uns.push(document.getElementById('lU'+i)?.value||''); });
  try{
    const r=await fetch('/api/labels',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({labels:lbls,units:uns})});
    if(!r.ok)throw new Error();
    showToast('Labels saved','ok'); closeModal('lblModal'); await loadData();
  }catch(e){ showToast('Error saving labels','err'); }
}

/* ===================== HELPERS ===================== */
function showToast(msg,type='ok'){
  const t=document.getElementById('toast');
  t.textContent=msg; t.className='toast '+type+' show';
  setTimeout(()=>t.classList.remove('show'),3500);
}
function timeNow(){ const d=new Date(); return [d.getHours(),d.getMinutes(),d.getSeconds()].map(n=>String(n).padStart(2,'0')).join(':'); }
function esc(s){ return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }

document.querySelectorAll('.modal').forEach(m=>{
  m.addEventListener('click',e=>{ if(e.target===m)m.classList.remove('active'); });
});
</script>
</body>
</html>
)rawliteral";

/* ================================================================
   SETUP
   ================================================================ */

void setup() {
  loadConfig();

  Serial.begin(9600);
  node.begin(slaveId, Serial);

  initWiFi();

  // HTTP routes
  server.on("/",           HTTP_GET,     []() { server.send_P(200, "text/html", DASHBOARD); });
  server.on("/api/data",   HTTP_GET,     apiData);
  server.on("/api/config", HTTP_GET,     apiGetConfig);
  server.on("/api/config", HTTP_POST,    apiSetConfig);
  server.on("/api/labels", HTTP_POST,    apiSetLabels);
  server.on("/api/wifi",   HTTP_GET,     apiGetWifi);
  server.on("/api/wifi",   HTTP_POST,    apiSetWifi);
  server.on("/api/status", HTTP_GET,     apiStatus);

  // CORS pre-flight
  server.on("/api/data",   HTTP_OPTIONS, handleOptions);
  server.on("/api/config", HTTP_OPTIONS, handleOptions);
  server.on("/api/labels", HTTP_OPTIONS, handleOptions);
  server.on("/api/wifi",   HTTP_OPTIONS, handleOptions);
  server.on("/api/status", HTTP_OPTIONS, handleOptions);

  server.begin();
  tcpServer.begin();
  tcpServer.setNoDelay(true);

  // Force immediate first poll
  lastReadMs = millis() - (uint32_t)pollSec * 1000UL - 1;
}

/* ================================================================
   LOOP
   ================================================================ */

void loop() {
  // Modbus poll
  if ((millis() - lastReadMs) >= (uint32_t)pollSec * 1000UL) {
    readModbus();
  }

  // HTTP server
  server.handleClient();

  // Raw TCP socket management
  handleTcpClients();

  // WiFi STA maintenance
  maintainWiFi();
}