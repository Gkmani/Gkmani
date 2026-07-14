#pragma once
// ============================================================
//  SDLogger.h  –  SD card CSV logging for Modbus register data
//  Explicit VSPI bus: MISO=19 MOSI=23 SCK=18 CS=5
//  Fully isolated from TFT's HSPI bus (MISO=12 MOSI=13 SCK=14 CS=15)
//
//  Log file : /modbus_log.csv   (appended per flush interval)
//  Max file size auto-rolls to /modbus_log_old.csv at 4 MB
//  Header row written once on mount or after roll
// ============================================================
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#define SD_CS_PIN     5
#define SD_MISO_PIN  19
#define SD_MOSI_PIN  23
#define SD_SCK_PIN   18
#define SD_LOG_FILE   "/modbus_log.csv"
#define SD_OLD_FILE   "/modbus_log_old.csv"
#define SD_MAX_BYTES  (4UL * 1024 * 1024)   // 4 MB roll threshold
#define SD_FLUSH_MS   5000                   // write interval ms

struct SDLogEntry {
    String   name;
    double   value;
    String   unit;
    uint8_t  slaveId;
    uint16_t address;
    bool     commOk;
};

class SDLogger {
public:
    // ── Mount and prepare file ────────────────────────────────
    bool begin() {
        // Delay to let everything stabilize
        delay(50);
        
        // Explicit VSPI bus – isolated from TFT's HSPI instance.
        _vspi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
        // Use lower speed for stability
        _vspi.setClockDivider(SPI_CLOCK_DIV8);  // ~2.5 MHz

        pinMode(SD_CS_PIN, OUTPUT);
        digitalWrite(SD_CS_PIN, HIGH);   // deselect before any transaction
        delay(10);

        // Try at lower speed first
        if (!SD.begin(SD_CS_PIN, _vspi, 4000000UL)) {
            // Retry with even lower speed
            delay(100);
            if (!SD.begin(SD_CS_PIN, _vspi, 1000000UL)) {
                Serial.println(F("[SD] Card mount FAILED – logging disabled"));
                _ok = false;
                return false;
            }
        }
        _ok = true;
        Serial.printf("[SD] Card mounted on VSPI. Size: %llu MB\n",
                      SD.cardSize() / (1024 * 1024));
        _ensureHeader();
        return true;
    }

    bool isMounted()     { return _ok; }
    uint32_t logCount()  { return _logCount; }

    // ── Queue one snapshot; flushes at SD_FLUSH_MS interval ──
    void log(uint32_t epochSec, const std::vector<SDLogEntry> &entries) {
        if (!_ok) return;
        uint32_t now = millis();
        if (now - _lastFlush < SD_FLUSH_MS) return;
        _lastFlush = now;

        // Roll file if too large
        if (_fileSize() > SD_MAX_BYTES) _roll();

        File f = SD.open(SD_LOG_FILE, FILE_APPEND);
        if (!f) {
            Serial.println(F("[SD] Cannot open log for append"));
            return;
        }

        for (auto &e : entries) {
            // epoch,name,slaveId,address,value,unit,ok
            f.printf("%lu,%s,%d,%d,%.4f,%s,%d\n",
                     (unsigned long)epochSec,
                     e.name.c_str(),
                     e.slaveId,
                     e.address,
                     e.value,
                     e.unit.c_str(),
                     e.commOk ? 1 : 0);
            _logCount++;
        }
        f.close();
    }

    // ── Direct single-line write (for events/alerts) ──────────
    void logEvent(uint32_t epochSec, const char *msg) {
        if (!_ok) return;
        File f = SD.open(SD_LOG_FILE, FILE_APPEND);
        if (!f) return;
        f.printf("%lu,EVENT,0,0,0,%s,0\n", (unsigned long)epochSec, msg);
        f.close();
        _logCount++;
    }

    // ── Expose last N lines for web download (up to 512 B) ───
    String lastLines(uint8_t n = 10) {
        if (!_ok) return "SD not mounted";
        File f = SD.open(SD_LOG_FILE, FILE_READ);
        if (!f) return "Cannot open";
        String content;
        uint32_t sz = f.size();
        if (sz > 1024) f.seek(sz - 1024);
        while (f.available()) content += (char)f.read();
        f.close();
        // Return last n lines
        int idx = content.length();
        for (int i = 0; i < n; i++) {
            int p = content.lastIndexOf('\n', idx - 1);
            if (p < 0) { idx = 0; break; }
            idx = p;
        }
        return content.substring(idx);
    }

private:
    SPIClass _vspi      = SPIClass(VSPI);
    bool     _ok        = false;
    uint32_t _lastFlush = 0;
    uint32_t _logCount  = 0;

    void _ensureHeader() {
        // Write CSV header only if file does not exist or is empty
        if (!SD.exists(SD_LOG_FILE)) {
            File f = SD.open(SD_LOG_FILE, FILE_WRITE);
            if (f) {
                f.println(F("epoch_sec,name,slave_id,address,value,unit,comm_ok"));
                f.close();
                Serial.println(F("[SD] Created new log file with header"));
            }
        }
    }

    void _roll() {
        Serial.println(F("[SD] Rolling log file"));
        if (SD.exists(SD_OLD_FILE)) SD.remove(SD_OLD_FILE);
        SD.rename(SD_LOG_FILE, SD_OLD_FILE);
        _ensureHeader();
        _logCount = 0;
    }

    uint32_t _fileSize() {
        if (!SD.exists(SD_LOG_FILE)) return 0;
        File f = SD.open(SD_LOG_FILE, FILE_READ);
        uint32_t sz = f.size();
        f.close();
        return sz;
    }
};