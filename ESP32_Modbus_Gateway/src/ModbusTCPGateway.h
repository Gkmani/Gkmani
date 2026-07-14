#pragma once
// ============================================================
//  ModbusTCPGateway.h – Native WiFiServer Modbus TCP (port 502)
//  Bridges incoming MBAP requests to RTU register cache.
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"
#include "ModbusRTUManager.h"

#define MAX_TCP_CLIENTS  10
#define TCP_IDLE_TIMEOUT 5000   // ms

extern Config           config;
extern ModbusRTUManager rtuManager;

class ModbusTCPGateway {
public:
    void begin(uint16_t port = 502) {
        _port = port;
        _server = new WiFiServer(port);
        _server->begin();
        _server->setNoDelay(true);
        Serial.printf("[TCP] Modbus TCP listening on port %d\n", port);
    }

    void loop() {
        if (!_server) return;

        // ── Accept new connections ────────────────────────────
        if (_server->hasClient()) {
            WiFiClient nc = _server->accept();
            bool placed = false;
            for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
                if (!_clients[i] || !_clients[i].connected()) {
                    _clients[i]     = nc;
                    _clientTs[i]    = millis();
                    placed = true;
                    Serial.printf("[TCP] Client[%d] %s connected\n",
                                  i, nc.remoteIP().toString().c_str());
                    break;
                }
            }
            if (!placed) { nc.stop(); }
        }

        // ── Service connected clients ─────────────────────────
        for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
            if (!_clients[i]) continue;
            if (!_clients[i].connected()) {
                _clients[i].stop(); continue;
            }
            if (millis() - _clientTs[i] > TCP_IDLE_TIMEOUT) {
                _clients[i].stop();
                Serial.printf("[TCP] Client[%d] timeout\n", i);
                continue;
            }
            if (_clients[i].available() >= 6) {
                _clientTs[i] = millis();
                _handle(_clients[i]);
            }
        }
    }

    uint8_t connectedClients() {
        uint8_t n = 0;
        for (int i = 0; i < MAX_TCP_CLIENTS; i++)
            if (_clients[i] && _clients[i].connected()) n++;
        return n;
    }

private:
    WiFiServer *_server = nullptr;
    uint16_t    _port   = 502;
    WiFiClient  _clients[MAX_TCP_CLIENTS];
    uint32_t    _clientTs[MAX_TCP_CLIENTS] = {};

    void _handle(WiFiClient &c) {
        // ── Read MBAP header (6 bytes) ────────────────────────
        uint8_t hdr[6];
        c.readBytes(hdr, 6);
        uint16_t txId   = (uint16_t)(hdr[0]<<8|hdr[1]);
        uint16_t proto  = (uint16_t)(hdr[2]<<8|hdr[3]);
        uint16_t pduLen = (uint16_t)(hdr[4]<<8|hdr[5]);

        if (proto != 0 || pduLen < 2 || pduLen > 250) {
            // flush and bail
            uint8_t dump[256]; c.readBytes(dump, min((int)pduLen, 250));
            return;
        }

        uint8_t pdu[256];
        c.readBytes(pdu, pduLen);

        uint8_t  unitId  = pdu[0];
        uint8_t  fc      = pdu[1];
        uint16_t addr    = (uint16_t)(pdu[2]<<8|pdu[3]);
        uint16_t qty     = (uint16_t)(pdu[4]<<8|pdu[5]);
        uint16_t writeVal= (pduLen >= 6) ? (uint16_t)(pdu[4]<<8|pdu[5]) : 0;

        uint8_t  resp[260];
        uint16_t rLen = _buildResp(txId, unitId, fc, addr, qty, writeVal, resp);
        c.write(resp, rLen);
    }

    uint16_t _buildResp(uint16_t txId, uint8_t unitId, uint8_t fc,
                         uint16_t addr, uint16_t qty, uint16_t writeVal,
                         uint8_t *r) {
        auto regs = rtuManager.getRegisters();

        // ── FC03 / FC04 ───────────────────────────────────────
        if (fc == 0x03 || fc == 0x04) {
            uint8_t bc = (uint8_t)(qty * 2);
            _mbapHdr(r, txId, unitId, 3 + bc);
            r[7] = fc; r[8] = bc;
            for (uint16_t i = 0; i < qty; i++) {
                uint16_t raw = _lookupU16(regs, unitId, addr + i);
                r[9+i*2]   = raw >> 8;
                r[9+i*2+1] = raw & 0xFF;
            }
            return (uint16_t)(9 + bc);
        }

        // ── FC01 / FC02 ───────────────────────────────────────
        if (fc == 0x01 || fc == 0x02) {
            uint8_t bc = (qty + 7) / 8;
            _mbapHdr(r, txId, unitId, 3 + bc);
            r[7] = fc; r[8] = bc;
            memset(r+9, 0, bc);
            for (uint16_t i = 0; i < qty; i++) {
                if (_lookupU16(regs, unitId, addr+i))
                    r[9+i/8] |= (1<<(i%8));
            }
            return (uint16_t)(9 + bc);
        }

        // ── FC06 Write Single Register ────────────────────────
        if (fc == 0x06) {
            rtuManager.writeSingleRegister(unitId, addr, writeVal);
            _mbapHdr(r, txId, unitId, 6);
            r[7]=fc; r[8]=addr>>8; r[9]=addr&0xFF;
            r[10]=writeVal>>8; r[11]=writeVal&0xFF;
            return 12;
        }

        // ── Exception ─────────────────────────────────────────
        _mbapHdr(r, txId, unitId, 3);
        r[7] = fc | 0x80; r[8] = 0x01; // Illegal Function
        return 9;
    }

    void _mbapHdr(uint8_t *r, uint16_t txId, uint8_t unitId, uint16_t len) {
        r[0]=txId>>8; r[1]=txId&0xFF;
        r[2]=0; r[3]=0;
        r[4]=len>>8; r[5]=len&0xFF;
        r[6]=unitId;
    }

    uint16_t _lookupU16(const std::vector<LiveRegister> &regs,
                         uint8_t slaveId, uint16_t address) {
        for (auto &r : regs)
            if (r.slaveId == slaveId && r.address == address)
                return (uint16_t)(r.value);
        return 0;
    }
};
