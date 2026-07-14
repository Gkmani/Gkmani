#pragma once
// ============================================================
//  ModbusRTUManager.h
//  UART2 RS485 polling engine – Core 0
//  GPIO22=RX  GPIO27=TX  (CYD-safe pins — see note below)
//  NOTE: GPIO16/17 must NEVER be used for UART2 on CYD boards.
//  They can be reserved by the ESP32 core for PSRAM access, and
//  attaching a peripheral there corrupts FreeRTOS internal state,
//  producing: "assert failed: xQueueSemaphoreTake queue.c:1709"
// ============================================================
#include <Arduino.h>
#include "Config.h"

#define RS485_RX_PIN  22
#define RS485_TX_PIN  27
#define RS485_DE_PIN   -1
#define RS485_SERIAL  Serial2

#define RTU_TIMEOUT_MS   300
#define RTU_FRAME_GAP_MS  10
#define RTU_MAX_RETRIES    2

// ─── CRC16 ────────────────────────────────────────────────────
static uint16_t crc16(const uint8_t *b, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= b[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
    }
    return crc;
}

// ─── Live register snapshot ───────────────────────────────────
struct LiveRegister {
    String   name;
    uint8_t  slaveId;
    uint16_t address;
    double   value;
    uint32_t timestamp;
    bool     commOk;
};

extern Config config;

class ModbusRTUManager {
public:
    SemaphoreHandle_t mutex = NULL;

    void begin(uint32_t baud, char parity, uint8_t stopBits,
               int rxPin, int txPin, int dePin) {
		Serial.printf("[RTU] begin() called: RX=GPIO%d TX=GPIO%d DE=GPIO%d baud=%u\n",
		              rxPin, txPin, dePin, baud);

		if (dePin >= 0) {
			_dePin = dePin;
			pinMode(_dePin, OUTPUT);
			Serial.println(F("[RTU] DE pin configured"));
			_deIdle();
			Serial.println(F("[RTU] DE idle set"));
		}

        uint32_t cfg;
        if      (parity == 'E') cfg = (stopBits == 2) ? SERIAL_8E2 : SERIAL_8E1;
        else if (parity == 'O') cfg = (stopBits == 2) ? SERIAL_8O2 : SERIAL_8O1;
        else                    cfg = (stopBits == 2) ? SERIAL_8N2 : SERIAL_8N1;

        Serial.println(F("[RTU] About to call Serial2.begin()..."));
        RS485_SERIAL.begin(baud, cfg, rxPin, txPin);
        Serial.println(F("[RTU] Serial2.begin() returned OK"));

        mutex = xSemaphoreCreateMutex();
        if (mutex == NULL) {
            Serial.println(F("[RTU] FATAL: mutex creation failed!"));
        } else {
            Serial.println(F("[RTU] Mutex created OK"));
        }

        _syncCache();
        Serial.println(F("[RTU] Cache synced"));
        Serial.printf("[RTU] UART2 baud=%u parity=%c stop=%d DE=GPIO%d\n",
                      baud, parity, stopBits, dePin);
    }

    // ── Called from Core 0 rtuTask ────────────────────────────
    void poll() {
        if (config.mappings.empty()) return;
        if (millis() - _lastPoll < config.pollRate) return;
        _lastPoll = millis();

        if (_idx >= config.mappings.size()) _idx = 0;
        RegisterMap &m = config.mappings[_idx];

        bool   ok  = false;
        double val = 0.0;
        for (uint8_t t = 0; t <= RTU_MAX_RETRIES; t++) {
            ok = _readReg(m, val);
            if (ok) break;
            delay(RTU_FRAME_GAP_MS);
        }

        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (_idx < _cache.size()) {
                _cache[_idx].value     = val;
                _cache[_idx].timestamp = millis() / 1000;
                _cache[_idx].commOk    = ok;
            }
            xSemaphoreGive(mutex);
        }

        _lastDevice = m.name;
        _lastOk     = ok;
        _idx        = (_idx + 1) % config.mappings.size();
    }

    // ── Thread-safe snapshot (Core 1) ────────────────────────
    std::vector<LiveRegister> getRegisters() {
        std::vector<LiveRegister> snap;
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            snap = _cache;
            xSemaphoreGive(mutex);
        }
        return snap;
    }

    String lastPolledDevice() { return _lastDevice; }
    bool   lastPollOk()       { return _lastOk; }

    bool writeSingleRegister(uint8_t slaveId, uint16_t addr, uint16_t val) {
        uint8_t req[8];
        req[0]=slaveId; req[1]=0x06;
        req[2]=addr>>8; req[3]=addr&0xFF;
        req[4]=val>>8;  req[5]=val&0xFF;
        uint16_t c=crc16(req,6); req[6]=c&0xFF; req[7]=c>>8;
        _deTx();
        RS485_SERIAL.write(req,8);
        RS485_SERIAL.flush();
        _deIdle();
        uint8_t resp[8];
        return _waitResp(resp,8);
    }

    void syncRegisters() { _syncCache(); }

private:
    int      _dePin     = RS485_DE_PIN;
    size_t   _idx       = 0;
    uint32_t _lastPoll  = 0;
    String   _lastDevice;
    bool     _lastOk    = false;
    std::vector<LiveRegister> _cache;

    void _deTx()   { if (_dePin >= 0) { digitalWrite(_dePin, HIGH); delayMicroseconds(150); } }
	void _deIdle() { RS485_SERIAL.flush(); delayMicroseconds(150); if (_dePin >= 0) digitalWrite(_dePin, LOW); }

    bool _readReg(const RegisterMap &m, double &out) {
        uint8_t cnt = _regCount(m.dataType);
        uint8_t req[8];
        req[0]=m.slaveId; req[1]=m.fc;
        req[2]=m.address>>8; req[3]=m.address&0xFF;
        req[4]=0x00; req[5]=cnt;
        uint16_t c=crc16(req,6); req[6]=c&0xFF; req[7]=c>>8;

        while (RS485_SERIAL.available()) RS485_SERIAL.read();

        _deTx();
        RS485_SERIAL.write(req,8);
        RS485_SERIAL.flush();
        _deIdle();

        uint8_t rxLen = 5 + cnt*2;
        uint8_t resp[32];
        if (!_waitResp(resp, rxLen)) return false;

        uint16_t rxCRC = resp[rxLen-2] | ((uint16_t)resp[rxLen-1]<<8);
        if (crc16(resp, rxLen-2) != rxCRC) return false;
        if (resp[1] & 0x80) return false;

        out = _decode(resp+3, m.dataType);
        return true;
    }

    bool _waitResp(uint8_t *buf, uint8_t need) {
        uint32_t t = millis();
        uint8_t  i = 0;
        while (millis()-t < RTU_TIMEOUT_MS && i < need)
            if (RS485_SERIAL.available()) buf[i++] = RS485_SERIAL.read();
        return i == need;
    }

    double _decode(const uint8_t *d, const String &type) {
        if (type=="UInt16") return (double)((uint16_t)d[0]<<8|d[1]);
        if (type=="Int16")  return (double)(int16_t)((uint16_t)d[0]<<8|d[1]);
        if (type=="UInt32") {
            uint32_t v=((uint32_t)d[0]<<24)|((uint32_t)d[1]<<16)|((uint32_t)d[2]<<8)|d[3];
            return (double)v;
        }
        if (type=="Int32") {
            int32_t v=(int32_t)(((uint32_t)d[0]<<24)|((uint32_t)d[1]<<16)|((uint32_t)d[2]<<8)|d[3]);
            return (double)v;
        }
        if (type=="Float") {
            uint32_t raw=((uint32_t)d[0]<<24)|((uint32_t)d[1]<<16)|((uint32_t)d[2]<<8)|d[3];
            float f; memcpy(&f,&raw,4); return (double)f;
        }
        if (type=="FloatSwap") {
            uint32_t raw=((uint32_t)d[2]<<24)|((uint32_t)d[3]<<16)|((uint32_t)d[0]<<8)|d[1];
            float f; memcpy(&f,&raw,4); return (double)f;
        }
        if (type=="Double") {
            uint64_t raw=0;
            for(int i=0;i<8;i++) raw=(raw<<8)|d[i];
            double v; memcpy(&v,&raw,8); return v;
        }
        return 0.0;
    }

    uint8_t _regCount(const String &type) {
        if (type=="UInt32"||type=="Int32"||type=="Float"||type=="FloatSwap") return 2;
        if (type=="Double") return 4;
        return 1;
    }

    void _syncCache() {
        if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100))==pdTRUE) {
            _cache.clear();
            for (auto &m : config.mappings) {
                LiveRegister r;
                r.name=m.name; r.slaveId=m.slaveId;
                r.address=m.address; r.value=0;
                r.timestamp=0; r.commOk=false;
                _cache.push_back(r);
            }
            xSemaphoreGive(mutex);
        }
    }
};
