#pragma once
// ============================================================
//  RestAPI.h – JSON REST API using ESP32 built-in WebServer
//
//  FIX: ESP32 synchronous WebServer does NOT dispatch PUT/DELETE
//  by method on the same URL path. Solution: separate URL suffixes.
//
//   GET    /api/device          → list all devices
//   POST   /api/device          → add new device
//   POST   /api/device/update   → update device  (?idx=N)
//   POST   /api/device/delete   → delete device  (?idx=N)
//
//   GET    /api/mappings        → list all mappings
//   POST   /api/mappings        → add new mapping
//   POST   /api/mappings/delete → delete mapping  (?idx=N)
// ============================================================
#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <Update.h>
#include "Config.h"
#include "ModbusRTUManager.h"
#include "ModbusTCPGateway.h"

extern Config           config;
extern ModbusRTUManager rtuManager;
extern ModbusTCPGateway tcpGateway;

namespace RestAPI {

// ── Auth helper ───────────────────────────────────────────────
static bool _auth(WebServer &s) {
    if (s.hasHeader("X-Token") &&
        config.validateToken(s.header("X-Token"))) return true;
    s.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
    return false;
}

// ── JSON response helper ──────────────────────────────────────
static void _json(WebServer &s, int code, DynamicJsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    s.send(code, "application/json", out);
}

// ── Body helper ───────────────────────────────────────────────
static String _body(WebServer &s) {
    if (s.hasArg("plain")) return s.arg("plain");
    return "{}";
}

void setup(WebServer &server) {

    //const char* headerKeys[] = {"X-Token", "Content-Type"};
    //server.collectHeaders(headerKeys, 2);

    // ── POST /api/login ──────────────────────────────────────
    server.on("/api/login", HTTP_POST, [&server]() {
        DynamicJsonDocument in(256), out(256);
        deserializeJson(in, _body(server));
		String raw = _body(server);
		Serial.printf("[LOGIN] body='%s'\n", raw.c_str());   // ← add this
		deserializeJson(in, raw);
        String u = in["user"] | "";
        String p = in["pass"] | "";
		Serial.printf("[LOGIN] u='%s' p='%s'\n", u.c_str(), p.c_str());  // ← and this
        if (u == config.webUser && p == config.webPass) {
            out["ok"]    = true;
            out["token"] = config.generateToken();
			Serial.println("[LOGIN] Success, token: " + out["token"].as<String>());  // ← Add this
        } else {
            out["ok"] = false;
			Serial.println("[LOGIN] Failed");  // ← Add this
        }
        _json(server, 200, out);
    });

    // ── GET /api/status ──────────────────────────────────────
    server.on("/api/status", HTTP_GET, [&server]() {
        if (!_auth(server)) return;
        DynamicJsonDocument doc(512);
        doc["ip"]         = (WiFi.getMode() == WIFI_STA)
                                ? WiFi.localIP().toString()
                                : WiFi.softAPIP().toString();
        doc["rssi"]       = WiFi.RSSI();
        doc["mode"]       = WiFi.getMode() == WIFI_STA ? "STA" : "AP";
        doc["tcpPort"]    = config.tcpPort;
        doc["tcpClients"] = tcpGateway.connectedClients();
        doc["freeHeap"]   = ESP.getFreeHeap();
        doc["uptime"]     = millis() / 1000;
        doc["devCount"]   = config.devices.size();
        doc["mapCount"]   = config.mappings.size();
        _json(server, 200, doc);
    });

    // ── GET /api/registers ───────────────────────────────────
    server.on("/api/registers", HTTP_GET, [&server]() {
        if (!_auth(server)) return;
        DynamicJsonDocument doc(8192);
        JsonArray arr = doc.createNestedArray("registers");
        for (auto &r : rtuManager.getRegisters()) {
            JsonObject o = arr.createNestedObject();
            o["name"]    = r.name;
            o["slaveId"] = r.slaveId;
            o["address"] = r.address;
            o["value"]   = r.value;
            o["ts"]      = r.timestamp;
            o["ok"]      = r.commOk;
        }
        _json(server, 200, doc);
    });

    // ── GET /api/device ──────────────────────────────────────
    server.on("/api/device", HTTP_GET, [&server]() {
        if (!_auth(server)) return;
        DynamicJsonDocument doc(8192);
        JsonArray arr = doc.createNestedArray("devices");
        for (auto &d : config.devices) {
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
        _json(server, 200, doc);
    });

    // ── POST /api/device  (ADD) ──────────────────────────────
    server.on("/api/device", HTTP_POST, [&server]() {
        if (!_auth(server)) return;
        if (config.devices.size() >= MAX_DEVICES) {
            server.send(400, "application/json", "{\"error\":\"max devices reached\"}");
            return;
        }
        DynamicJsonDocument in(512);
        if (deserializeJson(in, _body(server)) != DeserializationError::Ok) {
            server.send(400, "application/json", "{\"error\":\"bad JSON\"}");
            return;
        }
        ModbusDevice d;
        d.name         = in["name"]         | "Device";
        d.slaveId      = in["slaveId"]       | 1;
        d.baudRate     = in["baudRate"]      | 9600;
        String p       = in["parity"]        | "N";
        d.parity       = p[0];
        d.stopBits     = in["stopBits"]      | 1;
        d.fc           = in["fc"]            | 3;
        d.regAddress   = in["regAddress"]    | 0;
        d.regCount     = in["regCount"]      | 1;
        d.dataType     = in["dataType"]      | "UInt16";
        d.pollInterval = in["pollInterval"]  | 1000;
        d.enabled      = in["enabled"]       | true;
        config.devices.push_back(d);
        config.saveDevices();
        DynamicJsonDocument out(128);
        out["ok"]  = true;
        out["idx"] = config.devices.size() - 1;
        _json(server, 200, out);
    });

    // ── POST /api/device/update  (EDIT — replaces PUT) ───────
    // ?idx=N in query string
    server.on("/api/device/update", HTTP_POST, [&server]() {
        if (!_auth(server)) return;
        int idx = server.hasArg("idx") ? server.arg("idx").toInt() : -1;
        if (idx < 0 || idx >= (int)config.devices.size()) {
            server.send(400, "application/json", "{\"error\":\"bad idx\"}");
            return;
        }
        DynamicJsonDocument in(512);
        if (deserializeJson(in, _body(server)) != DeserializationError::Ok) {
            server.send(400, "application/json", "{\"error\":\"bad JSON\"}");
            return;
        }
        auto &d        = config.devices[idx];
        d.name         = in["name"]         | d.name;
        d.slaveId      = in["slaveId"]       | d.slaveId;
        d.baudRate     = in["baudRate"]      | d.baudRate;
        String p       = in["parity"]        | String(d.parity);
        d.parity       = p[0];
        d.stopBits     = in["stopBits"]      | d.stopBits;
        d.fc           = in["fc"]            | d.fc;
        d.regAddress   = in["regAddress"]    | d.regAddress;
        d.regCount     = in["regCount"]      | d.regCount;
        d.dataType     = in["dataType"]      | d.dataType;
        d.pollInterval = in["pollInterval"]  | d.pollInterval;
        d.enabled      = in["enabled"]       | d.enabled;
        config.saveDevices();
        server.send(200, "application/json", "{\"ok\":true}");
    });

    // ── POST /api/device/delete  (DELETE — replaces DELETE) ──
    // ?idx=N in query string
    server.on("/api/device/delete", HTTP_POST, [&server]() {
        if (!_auth(server)) return;
        int idx = server.hasArg("idx") ? server.arg("idx").toInt() : -1;
        if (idx < 0 || idx >= (int)config.devices.size()) {
            server.send(400, "application/json", "{\"error\":\"bad idx\"}");
            return;
        }
        config.devices.erase(config.devices.begin() + idx);
        config.saveDevices();
        server.send(200, "application/json", "{\"ok\":true}");
    });

    // ── GET /api/mappings ────────────────────────────────────
    server.on("/api/mappings", HTTP_GET, [&server]() {
        if (!_auth(server)) return;
        DynamicJsonDocument doc(16384);
        JsonArray arr = doc.createNestedArray("mappings");
        for (auto &m : config.mappings) {
            JsonObject o = arr.createNestedObject();
            o["name"]     = m.name;
            o["slaveId"]  = m.slaveId;
            o["fc"]       = m.fc;
            o["address"]  = m.address;
            o["dataType"] = m.dataType;
            o["unit"]     = m.unit;
        }
        _json(server, 200, doc);
    });

    // ── POST /api/mappings  (ADD) ────────────────────────────
    server.on("/api/mappings", HTTP_POST, [&server]() {
        if (!_auth(server)) return;
        if (config.mappings.size() >= MAX_MAPPINGS) {
            server.send(400, "application/json", "{\"error\":\"max mappings reached\"}");
            return;
        }
        DynamicJsonDocument in(256);
        if (deserializeJson(in, _body(server)) != DeserializationError::Ok) {
            server.send(400, "application/json", "{\"error\":\"bad JSON\"}");
            return;
        }
        RegisterMap m;
        m.name     = in["name"]     | "Register";
        m.slaveId  = in["slaveId"]  | 1;
        m.fc       = in["fc"]       | 3;
        m.address  = in["address"]  | 0;
        m.dataType = in["dataType"] | "UInt16";
        m.unit     = in["unit"]     | "";
        config.mappings.push_back(m);
        config.saveMappings();
        rtuManager.syncRegisters();
        DynamicJsonDocument out(128);
        out["ok"]  = true;
        out["idx"] = config.mappings.size() - 1;
        _json(server, 200, out);
    });

    // ── POST /api/mappings/delete  (DELETE — replaces DELETE)
    server.on("/api/mappings/delete", HTTP_POST, [&server]() {
        if (!_auth(server)) return;
        int idx = server.hasArg("idx") ? server.arg("idx").toInt() : -1;
        if (idx < 0 || idx >= (int)config.mappings.size()) {
            server.send(400, "application/json", "{\"error\":\"bad idx\"}");
            return;
        }
        config.mappings.erase(config.mappings.begin() + idx);
        config.saveMappings();
        rtuManager.syncRegisters();
        server.send(200, "application/json", "{\"ok\":true}");
    });

    // ── GET /api/system ──────────────────────────────────────
    server.on("/api/system", HTTP_GET, [&server]() {
        if (!_auth(server)) return;
        DynamicJsonDocument doc(512);
        doc["tcpPort"]    = config.tcpPort;
        doc["maxClients"] = config.maxTcpClients;
        doc["tcpTimeout"] = config.tcpTimeout;
        doc["pollRate"]   = config.pollRate;
        doc["webUser"]    = config.webUser;
        doc["ssid"]       = config.ssid;
        doc["apSSID"]     = config.apSSID;
        doc["baudRate"]   = config.baudRate;
        doc["parity"]     = String(config.parity);
        doc["stopBits"]   = config.stopBits;
        _json(server, 200, doc);
    });

    // ── POST /api/system/update  (replaces PUT /api/system) ──
    server.on("/api/system/update", HTTP_POST, [&server]() {
        if (!_auth(server)) return;
        DynamicJsonDocument in(512);
        if (deserializeJson(in, _body(server)) != DeserializationError::Ok) {
            server.send(400, "application/json", "{\"error\":\"bad JSON\"}");
            return;
        }
        if (in.containsKey("tcpPort"))    config.tcpPort       = in["tcpPort"];
        if (in.containsKey("maxClients")) config.maxTcpClients = in["maxClients"];
        if (in.containsKey("tcpTimeout")) config.tcpTimeout    = in["tcpTimeout"];
        if (in.containsKey("pollRate"))   config.pollRate      = in["pollRate"];
        if (in.containsKey("webUser"))    config.webUser       = in["webUser"].as<String>();
        if (in.containsKey("webPass") && in["webPass"].as<String>().length() > 0)
            config.webPass = in["webPass"].as<String>();
        if (in.containsKey("ssid"))       config.ssid     = in["ssid"].as<String>();
        if (in.containsKey("wifiPass"))   config.password = in["wifiPass"].as<String>();
        if (in.containsKey("apSSID"))     config.apSSID   = in["apSSID"].as<String>();
        if (in.containsKey("apPass"))     config.apPassword = in["apPass"].as<String>();
        if (in.containsKey("baudRate"))   config.baudRate = in["baudRate"];
        if (in.containsKey("parity")) {
            String p = in["parity"].as<String>();
            if (p.length() > 0) config.parity = p[0];
        }
        if (in.containsKey("stopBits"))   config.stopBits = in["stopBits"];
        config.saveMain();
        server.send(200, "application/json", "{\"ok\":true}");
    });

    // ── GET /api/history ────────────────────────────────────
    server.on("/api/history", HTTP_GET, [&server]() {
        if (!_auth(server)) return;
        String param = server.hasArg("param") ? server.arg("param") : "";
        DynamicJsonDocument doc(4096);
        doc["param"] = param;
        JsonArray lbl = doc.createNestedArray("labels");
        JsonArray val = doc.createNestedArray("values");
        for (auto &r : rtuManager.getRegisters()) {
            if (r.name == param) {
                lbl.add(String(r.timestamp));
                val.add(r.value);
                break;
            }
        }
        _json(server, 200, doc);
    });

    // ── GET /api/history/csv ─────────────────────────────────
    server.on("/api/history/csv", HTTP_GET, [&server]() {
        if (!_auth(server)) return;
        String csv = "timestamp,name,value\n";
        for (auto &r : rtuManager.getRegisters())
            csv += String(r.timestamp) + "," + r.name + "," + String(r.value, 4) + "\n";
        server.send(200, "text/csv", csv);
    });

    // ── GET /api/backup ──────────────────────────────────────
    server.on("/api/backup", HTTP_GET, [&server]() {
        if (!_auth(server)) return;
        DynamicJsonDocument doc(32768);
        auto rd = [&](const char *path, const char *key) {
            File f = LittleFS.open(path, "r");
            if (!f) return;
            DynamicJsonDocument sub(16384);
            deserializeJson(sub, f);
            f.close();
            doc[key] = sub;
        };
        rd(CFG_FILE, "main");
        rd(DEV_FILE, "devices");
        rd(MAP_FILE, "mappings");
        String out;
        serializeJsonPretty(doc, out);
        server.sendHeader("Content-Disposition",
                          "attachment; filename=\"gateway_backup.json\"");
        server.send(200, "application/json", out);
    });

    // ── POST /api/reboot ─────────────────────────────────────
    server.on("/api/reboot", HTTP_POST, [&server]() {
        if (!_auth(server)) return;
        server.send(200, "application/json", "{\"ok\":true}");
        delay(300);
        ESP.restart();
    });

    // ── POST /api/factory ────────────────────────────────────
    server.on("/api/factory", HTTP_POST, [&server]() {
        if (!_auth(server)) return;
        LittleFS.remove(CFG_FILE);
        LittleFS.remove(DEV_FILE);
        LittleFS.remove(MAP_FILE);
        server.send(200, "application/json", "{\"ok\":true}");
        delay(300);
        ESP.restart();
    });

    // ── POST /api/ota (multipart firmware upload) ─────────────
    server.on("/api/ota", HTTP_POST,
        [&server]() {
            bool ok = !Update.hasError();
            server.sendHeader("Connection", "close");
            server.send(200, "application/json",
                        ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"Update failed\"}");
            if (ok) { delay(500); ESP.restart(); }
        },
        [&server]() {
            HTTPUpload &up = server.upload();
            if (up.status == UPLOAD_FILE_START) {
                Serial.printf("[OTA] Start: %s\n", up.filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN))
                    Update.printError(Serial);
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (Update.write(up.buf, up.currentSize) != up.currentSize)
                    Update.printError(Serial);
            } else if (up.status == UPLOAD_FILE_END) {
                if (!Update.end(true)) Update.printError(Serial);
                else Serial.println(F("[OTA] Success"));
            }
        }
    );

    // ── 404 handler ──────────────────────────────────────────
    server.onNotFound([&server]() {
        server.send(404, "application/json", "{\"error\":\"Not found\"}");
    });

    Serial.println(F("[API] REST endpoints registered"));
}

} // namespace RestAPI
