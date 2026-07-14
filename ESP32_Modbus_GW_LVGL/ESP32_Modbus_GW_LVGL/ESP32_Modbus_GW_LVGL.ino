/*
 * ============================================================
 *  ESP32_Modbus_Gateway_CYD.ino
 *  Modbus RTU → TCP Gateway  v2.2.0  (CYD Edition)
 *  Target board: ESP32-2432S028 (Cheap Yellow Display)
 * ============================================================
 *
 *  ─── CYD Pin Assignments ──────────────────────────────────
 *
 *  ILI9341 TFT  (HSPI)
 *    MISO → GPIO12   MOSI → GPIO13
 *    SCK  → GPIO14   CS   → GPIO15
 *    DC   → GPIO2    RST  → tied HIGH (no SW reset)
 *    BL   → GPIO21   (HIGH = on)
 *
 *  SD Card  (VSPI  – shared bus, touch not used)
 *    MISO → GPIO19   MOSI → GPIO23
 *    SCK  → GPIO18   CS   → GPIO5
 *
 *  RS485 TTL  (UART2 – same as WROOM build)
 *    RO   → GPIO16   DI   → GPIO17
 *    DE/RE→ GPIO4    (optional; set RS485_DE_PIN -1 if auto-dir)
 *    NOTE: GPIO16/17 are also RGB-LED G/B on CYD.
 *          They float when UART2 idles – LED may flicker faintly.
 *          Drive GPIO27 (R) HIGH if you want RGB fully off.
 *
 *  I²C (CN1 header on CYD)
 *    SCL → GPIO22    SDA → GPIO27
 *    (No OLED fitted on CYD; CN1 exposed for external sensors)
 *
 *  ─── Library dependencies ─────────────────────────────────
 *  Built-in (ESP32 Arduino core):
 *    WiFi, WebServer, WiFiServer, Wire, LittleFS, ArduinoOTA
 *
 *  Install once via Library Manager:
 *    ArduinoJson         bblanchon        v6.x
 *    WebSockets          links2004        v2.4.x
 *    Adafruit ILI9341    Adafruit         v1.6.x
 *    Adafruit GFX        Adafruit         v1.11.x
 *    SD                  Arduino          (built-in)
 *
 * ============================================================
 *  FreeRTOS core assignment
 *    Core 0 : RTU polling task + TFT display task
 *    Core 1 : WebServer + WebSocket + Modbus TCP + SD logging
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <WebSocketsServer.h>

// ── Project modules ───────────────────────────────────────────
#include "src/Config.h"
#include "src/ModbusRTUManager.h"
#include "src/ModbusTCPGateway.h"
#include "src/WebDashboard.h"
#include "src/RestAPI.h"
#include "src/TFTDisplay.h"     // replaces OLEDDisplay.h
#include "src/SDLogger.h"       // new

// ── Global instances ──────────────────────────────────────────
WebServer        webServer(80);
WebSocketsServer wsServer(81);
ModbusRTUManager rtuManager;
ModbusTCPGateway tcpGateway;
TFTDisplay       tftDisplay;
SDLogger         sdLogger;
Config           config;

// ── FreeRTOS task handles ─────────────────────────────────────
TaskHandle_t rtuTaskHandle = NULL;
TaskHandle_t tftTaskHandle = NULL;

// ── Forward declarations ──────────────────────────────────────
void rtuTask(void *pv);
void tftTask(void *pv);
void setupWiFi();
void broadcastWS();
void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len);
std::vector<TFTRegSnapshot> buildTFTSnapshot();
std::vector<SDLogEntry>     buildSDEntries();

// ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println(F("\n[BOOT] ESP32 Modbus Gateway v2.2.0 (CYD)"));

    // ── Silence CYD RGB LED (GPIO4=R, GPIO16=G, GPIO17=B, active-LOW)
    // GPIO16/17 used by UART2; leave them to the peripheral.
    // Just drive the RED channel HIGH (off) to avoid red glow.
    pinMode(4, OUTPUT);  digitalWrite(4, HIGH);

    // ── TFT: start first so we can show boot progress ─────────
    tftDisplay.begin();
    tftDisplay.showMsg("Booting...", "Loading config");

    // ── LittleFS ──────────────────────────────────────────────
    if (!LittleFS.begin(true)) {
        LittleFS.format(); LittleFS.begin();
    }
    Serial.println(F("[FS] OK"));

    // ── Config ────────────────────────────────────────────────
    config.load();
    tftDisplay.showMsg("Config loaded", "Starting WiFi...");

    // ── SD Card ───────────────────────────────────────────────
    sdLogger.begin();   // non-fatal if absent

    // ── Wi-Fi ─────────────────────────────────────────────────
    setupWiFi();

    // ── Modbus RTU  (UART2: RX=16 TX=17 DE=4) ─────────────────
    rtuManager.begin(config.baudRate, config.parity, config.stopBits,
                     16, 17, -1);   // pass DE pin if you have it wired

    // ── Modbus TCP ────────────────────────────────────────────
    tcpGateway.begin(config.tcpPort);

    // ── Web + REST ────────────────────────────────────────────
    const char *hdrs[] = {"X-Token", "Content-Type"};
    webServer.collectHeaders(hdrs, 2);
    WebDashboard::setup(webServer);
    RestAPI::setup(webServer);

    // ── Add SD log download endpoint ──────────────────────────
    webServer.on("/sd/log", HTTP_GET, []() {
        // Basic token auth reuse
        String tok = webServer.header("X-Token");
        if (!config.validateToken(tok)) {
            webServer.send(401, "text/plain", "Unauthorised");
            return;
        }
        webServer.send(200, "text/csv", sdLogger.lastLines(50));
    });

    webServer.begin();

    // ── WebSocket ─────────────────────────────────────────────
    wsServer.begin();
    wsServer.onEvent(onWsEvent);

    // ── mDNS ──────────────────────────────────────────────────
    if (MDNS.begin("modbus-gw")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println(F("[mDNS] modbus-gw.local"));
    }

    // ── OTA ───────────────────────────────────────────────────
    ArduinoOTA.setHostname("modbus-gw");
    ArduinoOTA.setPassword(config.otaPassword.c_str());
    ArduinoOTA.onStart([]() {
        tftDisplay.showMsg("OTA Update", "Do not power off!");
    });
    ArduinoOTA.onEnd([]() {
        tftDisplay.showMsg("OTA Complete", "Rebooting...");
    });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("[OTA] Error[%u]\n", e);
    });
    ArduinoOTA.begin();

    // ── FreeRTOS tasks  (Core 0) ──────────────────────────────
    xTaskCreatePinnedToCore(rtuTask, "RTU",  8192, NULL, 2,
                            &rtuTaskHandle, 0);
    xTaskCreatePinnedToCore(tftTask, "TFT",  6144, NULL, 1,
                            &tftTaskHandle, 0);

    Serial.println(F("[BOOT] Ready"));
}

// ─────────────────────────────────────────────────────────────
void loop() {
    // Core 1 – networking + SD
    webServer.handleClient();
    wsServer.loop();
    tcpGateway.loop();
    ArduinoOTA.handle();

    // WebSocket broadcast every 1 s
    static uint32_t lastWS = 0;
    if (millis() - lastWS >= 1000) {
        lastWS = millis();
        broadcastWS();
    }

    // SD log flush every SD_FLUSH_MS (handled inside sdLogger)
    static uint32_t lastSD = 0;
    if (millis() - lastSD >= 1000) {
        lastSD = millis();
        sdLogger.log(millis() / 1000, buildSDEntries());
    }

    yield();
}

// ─── Core 0 – RTU polling ─────────────────────────────────────
void rtuTask(void *pv) {
    for (;;) {
        rtuManager.poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ─── Core 0 – TFT cycling ─────────────────────────────────────
// Runs at 200 ms tick; TFTDisplay.update() throttles actual redraws
void tftTask(void *pv) {
    for (;;) {
        auto regs = buildTFTSnapshot();

        String ip   = (WiFi.getMode() == WIFI_STA)
                        ? WiFi.localIP().toString()
                        : WiFi.softAPIP().toString();
        String mode = (WiFi.getMode() == WIFI_STA) ? "STA" : "AP";
        int    rssi = (WiFi.getMode() == WIFI_STA) ? WiFi.RSSI() : 0;

        tftDisplay.update(
            ip, mode,
            tcpGateway.connectedClients(), config.tcpPort,
            rtuManager.lastPolledDevice(), rtuManager.lastPollOk(),
            regs,
            millis() / 1000,
            ESP.getFreeHeap(),
            sdLogger.isMounted(), sdLogger.logCount(),
            rssi
        );

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ─── WiFi setup ───────────────────────────────────────────────
void setupWiFi() {
    if (config.wifiMode == WIFI_STA && config.ssid.length() > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(config.ssid.c_str(), config.password.c_str());
        Serial.printf("[WiFi] Connecting to %s", config.ssid.c_str());
        tftDisplay.showMsg("Connecting WiFi...", config.ssid.c_str());
        uint8_t tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries < 20) {
            delay(500); Serial.print('.'); tries++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("\n[WiFi] STA IP: %s\n",
                          WiFi.localIP().toString().c_str());
            tftDisplay.showMsg("WiFi Connected",
                               WiFi.localIP().toString().c_str());
            tftDisplay.forceRedraw();
            return;
        }
        Serial.println(F("\n[WiFi] STA failed – AP fallback"));
    }
    WiFi.mode(WIFI_AP);
    WiFi.softAP(config.apSSID.c_str(), config.apPassword.c_str());
    Serial.printf("[WiFi] AP IP: %s\n",
                  WiFi.softAPIP().toString().c_str());
    tftDisplay.showMsg("AP Mode",
                       WiFi.softAPIP().toString().c_str());
    tftDisplay.forceRedraw();
}

// ─── WebSocket broadcast ──────────────────────────────────────
void onWsEvent(uint8_t num, WStype_t type,
               uint8_t *payload, size_t len) {
    if (type == WStype_CONNECTED)
        Serial.printf("[WS] Client %d connected\n", num);
    else if (type == WStype_DISCONNECTED)
        Serial.printf("[WS] Client %d disconnected\n", num);
}

void broadcastWS() {
    if (wsServer.connectedClients() == 0) return;
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.createNestedArray("registers");
    for (auto &r : rtuManager.getRegisters()) {
        JsonObject o = arr.createNestedObject();
        o["name"]    = r.name;
        o["slaveId"] = r.slaveId;
        o["address"] = r.address;
        o["value"]   = serialized(String(r.value, 4));
        o["ts"]      = r.timestamp;
        o["ok"]      = r.commOk;
    }
    doc["tcpPort"]    = config.tcpPort;
    doc["uptime"]     = millis() / 1000;
    doc["freeHeap"]   = ESP.getFreeHeap();
    doc["tcpClients"] = tcpGateway.connectedClients();
    doc["rssi"]       = (WiFi.getMode() == WIFI_STA) ? WiFi.RSSI() : 0;
    doc["sdOk"]       = sdLogger.isMounted();
    doc["sdLogs"]     = sdLogger.logCount();
    String json;
    serializeJson(doc, json);
    wsServer.broadcastTXT(json);
}

// ─── Build TFT snapshot from live registers ───────────────────
std::vector<TFTRegSnapshot> buildTFTSnapshot() {
    std::vector<TFTRegSnapshot> snap;
    auto live = rtuManager.getRegisters();
    for (auto &r : live) {
        TFTRegSnapshot s;
        s.name      = r.name;
        s.value     = r.value;
        s.commOk    = r.commOk;
        s.timestamp = r.timestamp;
        // Pull unit from mappings (match by name)
        for (auto &m : config.mappings)
            if (m.name == r.name) { s.unit = m.unit; break; }
        snap.push_back(s);
    }
    return snap;
}

// ─── Build SD log entries ─────────────────────────────────────
std::vector<SDLogEntry> buildSDEntries() {
    std::vector<SDLogEntry> entries;
    auto live = rtuManager.getRegisters();
    for (auto &r : live) {
        SDLogEntry e;
        e.name    = r.name;
        e.value   = r.value;
        e.slaveId = r.slaveId;
        e.address = r.address;
        e.commOk  = r.commOk;
        for (auto &m : config.mappings)
            if (m.name == r.name) { e.unit = m.unit; break; }
        entries.push_back(e);
    }
    return entries;
}
