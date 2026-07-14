#pragma once
// ============================================================
//  Config.h – Persistent configuration via LittleFS JSON
// ============================================================
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>

#define CFG_FILE      "/config.json"
#define DEV_FILE      "/devices.json"
#define MAP_FILE      "/mappings.json"
#define MAX_DEVICES   32
#define MAX_MAPPINGS  500

// ─── Modbus device descriptor ─────────────────────────────────
struct ModbusDevice {
    String   name;
    uint8_t  slaveId      = 1;
    uint32_t baudRate     = 19200;
    char     parity       = 'E';
    uint8_t  stopBits     = 1;
    uint8_t  fc           = 3;
    uint16_t regAddress   = 0;
    uint16_t regCount     = 1;
    String   dataType     = "UInt16";
    uint32_t pollInterval = 1000;
    bool     enabled      = true;
};

// ─── Register mapping entry ───────────────────────────────────
struct RegisterMap {
    String   name;
    uint8_t  slaveId   = 1;
    uint8_t  fc        = 3;
    uint16_t address   = 0;
    String   dataType  = "UInt16";
    String   unit;
    double   value     = 0.0;
    uint32_t timestamp = 0;
    bool     commOk    = false;
};

// ─── Config class ─────────────────────────────────────────────
class Config {
public:
    // Wi-Fi
    String      ssid;
    String      password;
    String      apSSID     = "ModbusGW-ESP32";
    String      apPassword = "Admin12345";
    wifi_mode_t wifiMode   = WIFI_AP;

    // RS485
    uint32_t baudRate = 9600;
    char     parity   = 'N';
    uint8_t  stopBits = 1;

    // Modbus TCP
    uint16_t tcpPort       = 502;
    uint16_t maxTcpClients = 10;
    uint32_t tcpTimeout    = 5000;
    uint32_t pollRate      = 1000;

    // Security
    String webUser     = "admin";
    String webPass     = "Admin12345";
    String otaPassword = "otaAdmin12345";
    String sessionToken;

    // Lists
    std::vector<ModbusDevice> devices;
    std::vector<RegisterMap>  mappings;

    // ── Load all ──────────────────────────────────────────────
    void load() {
        _loadMain();
        _loadDevices();
        _loadMappings();
    }

    // ── Save helpers ──────────────────────────────────────────
    void saveMain() {
        DynamicJsonDocument doc(1024);
        doc["ssid"]        = ssid;
        doc["password"]    = password;
        doc["apSSID"]      = apSSID;
        doc["apPassword"]  = apPassword;
        doc["wifiMode"]    = (int)wifiMode;
        doc["baudRate"]    = baudRate;
        doc["parity"]      = String(parity);
        doc["stopBits"]    = stopBits;
        doc["tcpPort"]     = tcpPort;
        doc["maxClients"]  = maxTcpClients;
        doc["tcpTimeout"]  = tcpTimeout;
        doc["pollRate"]    = pollRate;
        doc["webUser"]     = webUser;
        doc["webPass"]     = webPass;
        doc["otaPassword"] = otaPassword;
        _write(CFG_FILE, doc);
    }

    void saveDevices() {
        DynamicJsonDocument doc(8192);
        JsonArray arr = doc.createNestedArray("devices");
        for (auto &d : devices) {
            JsonObject o = arr.createNestedObject();
            o["name"]         = d.name;
            o["slaveId"]      = d.slaveId;
            o["baudRate"]     = d.baudRate;
            o["parity"]       = String(d.parity);
            o["stopBits"]     = d.stopBits;
            o["fc"]           = d.fc;
            o["regAddress"]   = d.regAddress;
            o["regCount"]     = d.regCount;
            o["dataType"]     = d.dataType;
            o["pollInterval"] = d.pollInterval;
            o["enabled"]      = d.enabled;
        }
        _write(DEV_FILE, doc);
    }

    void saveMappings() {
        DynamicJsonDocument doc(16384);
        JsonArray arr = doc.createNestedArray("mappings");
        for (auto &m : mappings) {
            JsonObject o = arr.createNestedObject();
            o["name"]     = m.name;
            o["slaveId"]  = m.slaveId;
            o["fc"]       = m.fc;
            o["address"]  = m.address;
            o["dataType"] = m.dataType;
            o["unit"]     = m.unit;
        }
        _write(MAP_FILE, doc);
    }

    // ── Token auth ────────────────────────────────────────────
    String generateToken() {
        sessionToken = String(esp_random(), HEX) + String(esp_random(), HEX);
        return sessionToken;
    }
    bool validateToken(const String &t) {
        return t.length() > 0 && t == sessionToken;
    }

private:
    void _loadMain() {
        File f = LittleFS.open(CFG_FILE, "r");
        if (!f) { Serial.println(F("[CFG] No config – defaults used")); return; }
        DynamicJsonDocument doc(1024);
        if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
        f.close();
        ssid          = doc["ssid"]        | "PSI_Corp";
        password      = doc["password"]    | "Pass1234";
        apSSID        = doc["apSSID"]      | "ModbusGW-ESP32";
        apPassword    = doc["apPassword"]  | "Admin12345";
        wifiMode      = (wifi_mode_t)(int)(doc["wifiMode"] | (int)WIFI_AP);
        baudRate      = doc["baudRate"]    | 9600;
        String p      = doc["parity"]      | "N";
        parity        = p[0];
        stopBits      = doc["stopBits"]    | 1;
        tcpPort       = doc["tcpPort"]     | 502;
        maxTcpClients = doc["maxClients"]  | 10;
        tcpTimeout    = doc["tcpTimeout"]  | 5000;
        pollRate      = doc["pollRate"]    | 1000;
        webUser       = doc["webUser"]     | "admin";
        webPass       = doc["webPass"]     | "Admin12345";
        otaPassword   = doc["otaPassword"] | "otaAdmin12345";
        Serial.println(F("[CFG] Loaded config.json"));
    }

    void _loadDevices() {
        File f = LittleFS.open(DEV_FILE, "r");
        if (!f) return;
        DynamicJsonDocument doc(8192);
        if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
        f.close();
        devices.clear();
        for (JsonObject o : doc["devices"].as<JsonArray>()) {
            ModbusDevice d;
            d.name         = o["name"]         | "Device";
            d.slaveId      = o["slaveId"]       | 1;
            d.baudRate     = o["baudRate"]      | 9600;
            String p       = o["parity"]        | "N";
            d.parity       = p[0];
            d.stopBits     = o["stopBits"]      | 1;
            d.fc           = o["fc"]            | 3;
            d.regAddress   = o["regAddress"]    | 0;
            d.regCount     = o["regCount"]      | 1;
            d.dataType     = o["dataType"]      | "UInt16";
            d.pollInterval = o["pollInterval"]  | 1000;
            d.enabled      = o["enabled"]       | true;
            devices.push_back(d);
        }
        Serial.printf("[CFG] %d devices loaded\n", devices.size());
    }

    void _loadMappings() {
        File f = LittleFS.open(MAP_FILE, "r");
        if (!f) return;
        DynamicJsonDocument doc(16384);
        if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
        f.close();
        mappings.clear();
        for (JsonObject o : doc["mappings"].as<JsonArray>()) {
            RegisterMap m;
            m.name     = o["name"]     | "Register";
            m.slaveId  = o["slaveId"]  | 1;
            m.fc       = o["fc"]       | 3;
            m.address  = o["address"]  | 0;
            m.dataType = o["dataType"] | "UInt16";
            m.unit     = o["unit"]     | "";
            mappings.push_back(m);
        }
        Serial.printf("[CFG] %d mappings loaded\n", mappings.size());
    }

    void _write(const char *path, DynamicJsonDocument &doc) {
        File f = LittleFS.open(path, "w");
        if (!f) { Serial.printf("[CFG] Cannot write %s\n", path); return; }
        serializeJson(doc, f);
        f.close();
        Serial.printf("[CFG] Saved %s\n", path);
    }
};
