/*
 * ============================================================
 *  ESP32-WROOM  Modbus RTU → TCP Gateway  v2.1.0
 *  NO async libraries – 100% ESP32 built-in stack
 * ============================================================
 *  Dependencies (all available in Arduino Library Manager)
 *  ─────────────────────────────────────────────────────────
 *  Built-in (ESP32 core, zero install needed):
 *    WiFi.h, WebServer.h, WiFiServer.h
 *    Wire.h, LittleFS.h, ArduinoOTA.h, Update.h
 *
 *  Install once via Library Manager:
 *    ArduinoJson       by bblanchon          v6.x
 *    WebSockets        by links2004           v2.4.x
 *    Adafruit SSD1306  by Adafruit            v2.5.x
 *    Adafruit GFX      by Adafruit            v1.11.x
 *
 * ============================================================
 *  Pin Map  (ESP32-WROOM-32)
 *  ─────────────────────────────────────────────────────────
 *  RS485 MAX485
 *    RO  → GPIO16  (UART2 RX)
 *    DI  → GPIO17  (UART2 TX)
 *    DE+RE→GPIO4
 *
 *  OLED SSD1306 128×64
 *    SDA → GPIO21
 *    SCL → GPIO22
 *
 * ============================================================
 *  FreeRTOS Core Assignment
 *    Core 0 : RTU polling task + OLED task
 *    Core 1 : WebServer + WebSocket + Modbus TCP (loop)
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

#include "src/Config.h"
#include "src/ModbusRTUManager.h"
#include "src/ModbusTCPGateway.h"
#include "src/OLEDDisplay.h"
#include "src/WebDashboard.h"
#include "src/RestAPI.h"

// ─── Global instances ─────────────────────────────────────────
WebServer        webServer(80);
WebSocketsServer wsServer(81);
ModbusRTUManager rtuManager;
ModbusTCPGateway tcpGateway;
OLEDDisplay      oledDisplay;
Config           config;

// ─── FreeRTOS task handles ────────────────────────────────────
TaskHandle_t rtuTaskHandle  = NULL;
TaskHandle_t oledTaskHandle = NULL;

// ─── Forward declarations ─────────────────────────────────────
void rtuTask(void *pv);
void oledTask(void *pv);
void setupWiFi();
void broadcastWS();
void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);

// ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println(F("\n[BOOT] ESP32 Modbus Gateway v2.1.0"));

    // ── LittleFS ──────────────────────────────────────────────
    if (!LittleFS.begin(true)) {
        Serial.println(F("[FS] Format + mount"));
        LittleFS.format();
        LittleFS.begin();
    }
    Serial.println(F("[FS] OK"));

    // ── Config ────────────────────────────────────────────────
    config.load();

    // ── OLED ──────────────────────────────────────────────────
    Wire.begin(21, 22);
    oledDisplay.begin();
    oledDisplay.showBoot("Modbus GW", "v2.1.0");

    // ── Wi-Fi ─────────────────────────────────────────────────
    setupWiFi();

    // ── Modbus RTU ────────────────────────────────────────────
    rtuManager.begin(config.baudRate, config.parity, config.stopBits,
                     16, 17, -1);

    // ── Modbus TCP ────────────────────────────────────────────
    tcpGateway.begin(config.tcpPort);

    // ── Web + REST ────────────────────────────────────────────
    const char* hdrs[] = {"X-Token", "Content-Type"};
    webServer.collectHeaders(hdrs, 2);          // ← ADD THIS LINE

    WebDashboard::setup(webServer);
    RestAPI::setup(webServer);
    // Temporary diagnostic — remove after confirming
    Serial.printf("[AUTH DEBUG] webUser='%s' webPass='%s'\n",
    config.webUser.c_str(), config.webPass.c_str());
    webServer.begin();

    // ── WebSocket ─────────────────────────────────────────────
    wsServer.begin();
    wsServer.onEvent(onWsEvent);
    Serial.println(F("[WS] WebSocket server started on port 81"));

    // ── mDNS ─────────────────────────────────────────────────
    if (MDNS.begin("modbus-gw")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println(F("[mDNS] modbus-gw.local"));
    }

    // ── OTA ───────────────────────────────────────────────────
    ArduinoOTA.setHostname("modbus-gw");
    ArduinoOTA.setPassword(config.otaPassword.c_str());
    ArduinoOTA.onStart([]() { Serial.println(F("[OTA] Start")); });
    ArduinoOTA.onEnd([]()   { Serial.println(F("[OTA] End")); });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("[OTA] Error[%u]\n", e);
    });
    ArduinoOTA.begin();

    // ── FreeRTOS tasks (both pinned to Core 0) ────────────────
    xTaskCreatePinnedToCore(rtuTask,  "RTU",  8192, NULL, 2,
                            &rtuTaskHandle,  0);
    xTaskCreatePinnedToCore(oledTask, "OLED", 4096, NULL, 1,
                            &oledTaskHandle, 0);

    Serial.println(F("[BOOT] Ready"));
}

// ─────────────────────────────────────────────────────────────
void loop() {
    // Core 1 — networking
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

    yield();
}

// ─────────────────────────────────────────────────────────────
// Core 0 – RTU polling
void rtuTask(void *pv) {
    for (;;) {
        rtuManager.poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Core 0 – OLED cycling (3 screens × 3 s each)
void oledTask(void *pv) {
    uint8_t screen = 0;
    for (;;) {
        switch (screen % 3) {
            case 0:
                oledDisplay.showIP(
                    WiFi.getMode() == WIFI_STA
                        ? WiFi.localIP().toString().c_str()
                        : WiFi.softAPIP().toString().c_str(),
                    WiFi.getMode() == WIFI_STA ? "STA" : "AP");
                break;
            case 1:
                oledDisplay.showTCPClients(
                    tcpGateway.connectedClients(),
                    config.tcpPort);
                break;
            case 2:
                oledDisplay.showRTUStatus(
                    rtuManager.lastPolledDevice(),
                    rtuManager.lastPollOk());
                break;
        }
        screen++;
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// ─────────────────────────────────────────────────────────────
void setupWiFi() {
    if (config.wifiMode == WIFI_STA && config.ssid.length() > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(config.ssid.c_str(), config.password.c_str());
        Serial.printf("[WiFi] Connecting to %s", config.ssid.c_str());
        oledDisplay.showMsg("Connecting WiFi", config.ssid.c_str());
        uint8_t tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries < 20) {
            delay(500); Serial.print('.'); tries++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("\n[WiFi] STA IP: %s\n",
                          WiFi.localIP().toString().c_str());
            oledDisplay.showIP(WiFi.localIP().toString().c_str(), "STA");
            return;
        }
        Serial.println(F("\n[WiFi] STA failed – AP fallback"));
    }
    WiFi.mode(WIFI_AP);
    WiFi.softAP(config.apSSID.c_str(), config.apPassword.c_str());
    Serial.printf("[WiFi] AP IP: %s\n",
                  WiFi.softAPIP().toString().c_str());
    oledDisplay.showIP(WiFi.softAPIP().toString().c_str(), "AP");
}

// ─────────────────────────────────────────────────────────────
void onWsEvent(uint8_t num, WStype_t type,
               uint8_t *payload, size_t length) {
    if (type == WStype_CONNECTED)
        Serial.printf("[WS] Client %d connected\n", num);
    else if (type == WStype_DISCONNECTED)
        Serial.printf("[WS] Client %d disconnected\n", num);
}

// ─────────────────────────────────────────────────────────────
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
    doc["tcpPort"]    = config.tcpPort;   // ← add this line
    doc["uptime"]     = millis() / 1000;
    doc["freeHeap"]   = ESP.getFreeHeap();
    doc["tcpClients"] = tcpGateway.connectedClients();
    doc["rssi"]       = (WiFi.getMode() == WIFI_STA) ? WiFi.RSSI() : 0;
    String json;
    serializeJson(doc, json);
    wsServer.broadcastTXT(json);
}
