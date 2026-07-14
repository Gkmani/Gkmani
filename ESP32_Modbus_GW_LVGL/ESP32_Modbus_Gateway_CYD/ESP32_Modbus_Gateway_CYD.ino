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
 *  RS485 TTL  (UART2 – CYD-safe pins, NOT GPIO16/17)
 *    RO   → GPIO22   DI   → GPIO27
 *    DE/RE→ not wired (set RS485_DE_PIN -1, auto-direction module assumed)
 *    NOTE: GPIO16/17 must NOT be used for UART2 on CYD boards.
 *          The ESP32 Arduino core reserves them when PSRAM is
 *          detected, and Serial2.begin() on those pins corrupts
 *          internal FreeRTOS queue state, causing a fatal assert:
 *          "assert failed: xQueueSemaphoreTake queue.c:1709"
 *          GPIO22/27 (CN1 header) are free and safe on all CYD revisions.
 *          This repurposes the I2C CN1 header — fine since this build
 *          has no I2C peripherals (no OLED, no external I2C sensors).
 *
 *  I²C (CN1 header on CYD) — NOT AVAILABLE in this build
 *    SCL/SDA pins (GPIO22/27) are now used by RS485 UART2 above.
 *    If you need I2C later, move RS485 to other free GPIOs instead.
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
#include "src/TFTDisplay.h"
#include "src/SDLogger.h"

// ── Diagnostic toggle: set to 0 to confirm SD is the white-screen cause ──
#define ENABLE_SD_LOGGER  1
// ── Diagnostic toggle: set to 0 to confirm WiFi radio is the cause ──
#define ENABLE_WIFI       1

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

    // ── Diagnostic: print WHY the ESP32 last reset ─────────────
    esp_reset_reason_t reason = esp_reset_reason();
    Serial.printf("[BOOT] Reset reason code: %d ", (int)reason);
    switch (reason) {
        case ESP_RST_POWERON:   Serial.println(F("(POWERON - clean boot)")); break;
        case ESP_RST_BROWNOUT:  Serial.println(F("(BROWNOUT - power dip detected!)")); break;
        case ESP_RST_PANIC:     Serial.println(F("(PANIC - crash/assert)")); break;
        case ESP_RST_INT_WDT:   Serial.println(F("(INT WATCHDOG)")); break;
        case ESP_RST_TASK_WDT:  Serial.println(F("(TASK WATCHDOG)")); break;
        case ESP_RST_WDT:       Serial.println(F("(OTHER WATCHDOG)")); break;
        case ESP_RST_SW:        Serial.println(F("(SW reset / reboot)")); break;
        default:                Serial.println(F("(other)")); break;
    }

    // ── Silence CYD RGB LED (GPIO4=R, GPIO16=G, GPIO17=B, active-LOW)
    // GPIO16/17 used by UART2; leave them to the peripheral.
    // Just drive the RED channel HIGH (off) to avoid red glow.
    pinMode(4, OUTPUT);  digitalWrite(4, HIGH);

    // ── CRITICAL FIX: Initialize SPI bus before anything else ──
    // The TFT uses HSPI. Ensure it's properly initialized before WiFi.
    // NOTE: TFT_CLK, TFT_MISO, TFT_MOSI, TFT_CS are defined in TFTDisplay.h
    SPI.begin(TFT_CLK, TFT_MISO, TFT_MOSI, TFT_CS);

    // ── TFT: start first so we can show boot progress ─────────
    tftDisplay.begin();
    tftDisplay.showMsg("Booting...", "Loading config");

    // ── LittleFS ──────────────────────────────────────────────
    if (!LittleFS.begin(true)) {
        LittleFS.format(); 
        LittleFS.begin();
    }
    Serial.println(F("[FS] OK"));

    // ── Config ────────────────────────────────────────────────
    config.load();
    tftDisplay.showMsg("Config loaded", "Starting WiFi...");

    // ── CRITICAL FIX: Force display refresh before WiFi ──────
    tftDisplay.forceRedraw();

    // ── Wi-Fi ─────────────────────────────────────────────────
#if ENABLE_WIFI
    setupWiFi();
    
    // ── CRITICAL FIX: Re-initialize TFT AFTER WiFi starts ────
    // WiFi may have corrupted SPI settings
    delay(100);
    tftDisplay.reinitAfterWiFi();
    tftDisplay.showMsg("WiFi Ready", WiFi.softAPIP().toString().c_str());
#else
    Serial.println(F("[WiFi] Disabled via ENABLE_WIFI=0 (diagnostic mode)"));
    tftDisplay.showMsg("WiFi DISABLED", "Diagnostic mode");
    tftDisplay.forceRedraw();
#endif

    // ── SD Card ───────────────────────────────────────────────
#if ENABLE_SD_LOGGER
    // FIX: Initialize SD AFTER WiFi to avoid SPI conflicts
    delay(100);
    sdLogger.begin();   // non-fatal if absent
#else
    Serial.println(F("[SD] Disabled via ENABLE_SD_LOGGER=0 (diagnostic mode)"));
#endif

    // ── Modbus RTU  (UART2: RX=22 TX=27 — CYD-safe pins) ──────
    rtuManager.begin(config.baudRate, config.parity, config.stopBits,
                     22, 27, -1);   // RX=22, TX=27, no DE pin wired

    // ── Modbus TCP ────────────────────────────────────────────
#if ENABLE_WIFI
    tcpGateway.begin(config.tcpPort);
#else
    Serial.println(F("[TCP] Skipped — requires WiFi/lwIP stack (diagnostic mode)"));
#endif

    // ── Web + REST ────────────────────────────────────────────
    const char *hdrs[] = {"X-Token", "Content-Type"};
    webServer.collectHeaders(hdrs, 2);
    WebDashboard::setup(webServer);
    RestAPI::setup(webServer);

    // ── Add SD log download endpoint ──────────────────────────
    webServer.on("/sd/log", HTTP_GET, []() {
        String tok = webServer.header("X-Token");
        if (!config.validateToken(tok)) {
            webServer.send(401, "text/plain", "Unauthorised");
            return;
        }
        webServer.send(200, "text/csv", sdLogger.lastLines(50));
    });

#if ENABLE_WIFI
    webServer.begin();

    // ── WebSocket ─────────────────────────────────────────────
    wsServer.begin();
    wsServer.onEvent(onWsEvent);
#else
    Serial.println(F("[Web] webServer/wsServer begin() skipped — requires WiFi/lwIP (diagnostic mode)"));
#endif

    // ── mDNS ──────────────────────────────────────────────────
#if ENABLE_WIFI
    if (MDNS.begin("modbus-gw")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println(F("[mDNS] modbus-gw.local"));
    }
#else
    Serial.println(F("[mDNS] Skipped — requires WiFi (diagnostic mode)"));
#endif

    // ── OTA ───────────────────────────────────────────────────
#if ENABLE_WIFI
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
#else
    Serial.println(F("[OTA] Skipped — requires WiFi/lwIP (diagnostic mode)"));
#endif

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
#if ENABLE_WIFI
    webServer.handleClient();
    wsServer.loop();
    tcpGateway.loop();
    ArduinoOTA.handle();
#endif

    // WebSocket broadcast every 1 s
#if ENABLE_WIFI
    static uint32_t lastWS = 0;
    if (millis() - lastWS >= 1000) {
        lastWS = millis();
        broadcastWS();
    }
#endif

    // SD log flush every SD_FLUSH_MS (handled inside sdLogger)
#if ENABLE_SD_LOGGER
    static uint32_t lastSD = 0;
    if (millis() - lastSD >= 1000) {
        lastSD = millis();
        sdLogger.log(millis() / 1000, buildSDEntries());
    }
#endif

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

        // AP mode only — no STA branch needed
        String ip   = WiFi.softAPIP().toString();
        String mode = "AP";
        int    rssi = 0;   // not applicable in AP mode

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

// ─── WiFi setup – AP MODE ONLY ──────────────────────────────────
// STA mode removed: it added a router-dependent scan/retry cycle
// (up to 10s of delay(500) loops) plus an extra radio mode switch,
// both of which increased exposure to the brownout that corrupts
// the TFT's GRAM. AP mode starts the radio once and is done.
void setupWiFi() {
    // ── Brownout mitigation ────────────────────────────────────
    // Lower TX power BEFORE the radio starts transmitting beacons.
    // Default is 19.5dBm; lower = smaller current spike on 3.3V rail.
    WiFi.setTxPower(WIFI_POWER_11dBm);

    WiFi.mode(WIFI_AP);
    delay(50);   // let radio mode settle before softAP() call

    bool apOk = WiFi.softAP(config.apSSID.c_str(), config.apPassword.c_str());
    if (!apOk) {
        Serial.println(F("[WiFi] softAP() FAILED"));
        tftDisplay.showMsg("AP FAILED", "Check config");
        tftDisplay.forceRedraw();
        return;
    }

    Serial.printf("[WiFi] AP SSID: %s\n", config.apSSID.c_str());
    Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    tftDisplay.showMsg("AP Mode Ready", WiFi.softAPIP().toString().c_str());
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