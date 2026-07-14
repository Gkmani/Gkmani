#pragma once
// ============================================================
//  TFTDisplay.h  –  ILI9341 2.8" 320×240 TFT (CYD ESP32-2432S028)
//  Fixed layout with proper centering for rotation 0
// ============================================================
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ── Rotation ────────────────────────────────────────────────
// Try 0 or 2 for landscape on CYD
#define TFT_ROTATION  2

// ── Pin definitions ────────────────────────────────────────────
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_MOSI 13
#define TFT_CLK  14
#define TFT_MISO 12
#define TFT_BL   21

// ── Colour palette ──────────────────────────────────────────────
#define C_BG        0x0000
#define C_PANEL     0x10A2
#define C_ACCENT    0x04FF
#define C_ACCENT2   0xFD20
#define C_GREEN     0x07E0
#define C_RED       0xF800
#define C_WHITE     0xFFFF
#define C_LTGRAY    0xC618
#define C_DKGRAY    0x4208
#define C_YELLOW    0xFFE0
#define C_HEADER_BG 0x0168

// ── Layout constants ──────────────────────────────────────────
#define HDR_H       28
#define ROW_H       30
#define FOOTER_H    20
#define MARGIN      4

struct TFTRegSnapshot {
    String   name;
    double   value;
    String   unit;
    bool     commOk;
    uint32_t timestamp;
};

class TFTDisplay {
public:
    void begin() {
        pinMode(TFT_BL, OUTPUT);
        digitalWrite(TFT_BL, HIGH);

        pinMode(TFT_CS, OUTPUT);  
        digitalWrite(TFT_CS, LOW);
        delay(10);
        digitalWrite(TFT_CS, HIGH);
        delay(5);

        _spi.begin(TFT_CLK, TFT_MISO, TFT_MOSI, TFT_CS);
        _spi.setClockDivider(SPI_CLOCK_DIV8);
        
        _tft = new Adafruit_ILI9341(&_spi, TFT_DC, TFT_CS, TFT_RST);
        _tft->begin(10000000UL);
        _tft->setRotation(TFT_ROTATION);
        
        // Get actual dimensions
        _screenW = _tft->width();
        _screenH = _tft->height();
        
        Serial.printf("[TFT] Rotation %d: width=%d, height=%d\n", 
                      TFT_ROTATION, _screenW, _screenH);
        
        // If we got 240x320 (portrait), swap to landscape
        if (_screenW == 240 && _screenH == 320) {
            _screenW = 320;
            _screenH = 240;
            Serial.println(F("[TFT] Adjusted to landscape 320x240"));
        }
        
        _tft->fillScreen(C_BG);
        _tft->setSPISpeed(20000000UL);
        
        _showSplash();
        delay(1500);
        _tft->fillScreen(C_BG);
        Serial.printf("[TFT] Ready - using %dx%d\n", _screenW, _screenH);
    }

    void reinitAfterWiFi() {
        digitalWrite(TFT_CS, LOW);
        delay(5);
        digitalWrite(TFT_CS, HIGH);
        delay(5);
        _spi.end();
        delay(10);
        _spi.begin(TFT_CLK, TFT_MISO, TFT_MOSI, TFT_CS);
        _spi.setClockDivider(SPI_CLOCK_DIV8);
        _tft->begin(10000000UL);
        _tft->setRotation(TFT_ROTATION);
        _screenW = _tft->width();
        _screenH = _tft->height();
        if (_screenW == 240 && _screenH == 320) {
            _screenW = 320;
            _screenH = 240;
        }
        _tft->fillScreen(C_BG);
        _tft->setSPISpeed(20000000UL);
        _forceRedraw = true;
    }

    void update(const String &ip, const String &wifiMode,
                uint8_t tcpClients, uint16_t tcpPort,
                const String &lastRTUDev, bool lastRTUOk,
                const std::vector<TFTRegSnapshot> &regs,
                uint32_t uptimeSec, uint32_t freeHeap,
                bool sdOk, uint32_t sdLogCount,
                int rssi)
    {
        uint32_t now = millis();
        
        if (now - _lastSwitch >= _screenMs) {
            _screen = (_screen + 1) % 4;
            _rowOff = 0;
            _lastSwitch = now;
            _forceRedraw = true;
        }

        if (_screen == 2 && regs.size() > 5) {
            if (now - _lastScroll >= 4000) {
                _rowOff = (_rowOff + 5) % regs.size();
                _lastScroll = now;
                _forceRedraw = true;
            }
        }

        if (!_forceRedraw) return;
        _forceRedraw = false;
        
        _tft->fillScreen(C_BG);
        _drawHeader(ip, wifiMode, rssi, uptimeSec);

        switch (_screen) {
            case 0: _drawNetwork(ip, wifiMode, rssi, tcpClients, tcpPort); break;
            case 1: _drawTCPScreen(tcpClients, tcpPort); break;
            case 2: _drawRegisters(regs); break;
            case 3: _drawSystem(freeHeap, sdOk, sdLogCount, uptimeSec, lastRTUDev, lastRTUOk); break;
        }

        _drawFooter(_screen);
    }

    void forceRedraw() { _forceRedraw = true; }
    void setScreenMs(uint32_t ms) { _screenMs = ms; }

    void showMsg(const char *l1, const char *l2 = "") {
        _tft->fillScreen(C_BG);
        _tft->setTextColor(C_ACCENT);
        _tft->setTextSize(2);
        _tft->setCursor(MARGIN, 80);
        _tft->println(l1);
        _tft->setTextColor(C_WHITE);
        _tft->setTextSize(1);
        _tft->setCursor(MARGIN, 110);
        _tft->println(l2);
    }

private:
    SPIClass              _spi = SPIClass(HSPI);
    Adafruit_ILI9341     *_tft = nullptr;
    uint8_t               _screen = 0;
    uint32_t              _lastSwitch = 0;
    uint32_t              _lastScroll = 0;
    uint32_t              _screenMs = 6000;
    bool                  _forceRedraw = true;
    uint8_t               _rowOff = 0;
    int                   _screenW = 320;
    int                   _screenH = 240;

    void _showSplash() {
        _tft->fillScreen(C_BG);
        // Gradient header
        for (int y = 0; y < 8; y++) {
            uint16_t c = _tft->color565(0, y*12, y*20+80);
            _tft->drawFastHLine(0, y, _screenW, c);
        }
        _tft->fillRect(0, 8, _screenW, 56, C_HEADER_BG);
        
        _tft->setTextColor(C_ACCENT);
        _tft->setTextSize(3);
        _centreText("MODBUS", 14, 3);
        _tft->setTextColor(C_WHITE);
        _centreText("RTU → TCP GATEWAY", 44, 1);
        _tft->drawFastHLine(10, 68, _screenW-20, C_ACCENT);
        _tft->setTextColor(C_LTGRAY);
        _tft->setTextSize(1);
        _centreText("CET Power Solutions", 80, 1);
        _centreText("ESP32-2432S028  |  v2.2.0", 96, 1);
        _tft->setTextColor(C_DKGRAY);
        _centreText("Initialising...", 120, 1);
        _tft->fillRect(0, _screenH-24, _screenW, 24, C_HEADER_BG);
        _tft->setTextColor(C_ACCENT2);
        _centreText("ILI9341 TFT  |  HSPI  |  SD", _screenH-16, 1);
    }

    void _drawHeader(const String &ip, const String &mode, int rssi, uint32_t uptimeSec) {
        _tft->fillRect(0, 0, _screenW, HDR_H, C_HEADER_BG);
        _tft->drawFastHLine(0, HDR_H, _screenW, C_ACCENT);

        _tft->setTextColor(C_ACCENT);
        _tft->setTextSize(1);
        _tft->setCursor(MARGIN, 6);
        _tft->print(F("MODBUS GW"));

        _tft->setTextColor(C_WHITE);
        _tft->setCursor(85, 6);
        String displayIP = ip;
        if (displayIP.length() > 15) displayIP = displayIP.substring(0, 15);
        _tft->print(mode + " " + displayIP);

        _tft->setCursor(_screenW - 50, 6);
        if (mode == "STA") {
            _tft->setTextColor(rssi > -70 ? C_GREEN : C_YELLOW);
            _tft->printf("RSSI:%d", rssi);
        } else {
            _tft->setTextColor(C_ACCENT2);
            _tft->print(F("AP"));
        }

        _tft->setTextColor(C_LTGRAY);
        _tft->setTextSize(1);
        _tft->setCursor(MARGIN, 18);
        uint32_t h = uptimeSec/3600, m=(uptimeSec%3600)/60, s=uptimeSec%60;
        _tft->printf("UP %02u:%02u:%02u", h, m, s);
    }

    void _drawNetwork(const String &ip, const String &mode, int rssi, uint8_t clients, uint16_t port) {
        int y = HDR_H + 8;
        _sectionTitle("  NETWORK STATUS", y); y += 22;
        _rowPair("Mode", mode, y, C_ACCENT); y += 20;
        _rowPair("IP Addr", ip, y, C_WHITE); y += 20;
        _rowPair("TCP Port", String(port), y, C_WHITE); y += 20;
        _rowPair("TCP Clients", String(clients), y, clients > 0 ? C_GREEN : C_LTGRAY); y += 20;
        _tft->drawFastHLine(MARGIN, y+2, _screenW-MARGIN*2, C_DKGRAY);
        y += 10;
        _tft->setTextColor(C_LTGRAY);
        _tft->setTextSize(1);
        _tft->setCursor(MARGIN, y);
        _tft->print(F("mDNS: modbus-gw.local"));
    }

    void _drawTCPScreen(uint8_t clients, uint16_t port) {
        int y = HDR_H + 10;
        _sectionTitle("  MODBUS TCP", y); y += 24;
        
        // Big "Connected clients" text
        _tft->setTextColor(C_ACCENT);
        _tft->setTextSize(2);
        _tft->setCursor(20, y);
        _tft->print(F("Connected clients"));
        y += 40;
        
        // Big number
        _tft->setTextSize(6);
        _tft->setTextColor(clients > 0 ? C_GREEN : C_LTGRAY);
        _tft->setCursor(60, y + 10);
        _tft->print(clients);
        y += 65;
        
        _tft->setTextSize(1);
        _tft->setTextColor(C_DKGRAY);
        _tft->drawFastHLine(MARGIN, y, _screenW-MARGIN*2, C_DKGRAY);
        y += 10;
        
        _rowPair("Port", String(port), y, C_WHITE); y += 20;
        _rowPair("Max clients", "10", y, C_LTGRAY); y += 20;
        _rowPair("Idle timeout", "5000 ms", y, C_LTGRAY);
    }

    void _drawRegisters(const std::vector<TFTRegSnapshot> &regs) {
        if (regs.empty()) {
            int y = HDR_H + 40;
            _tft->setTextColor(C_LTGRAY);
            _tft->setTextSize(1);
            _tft->setCursor(MARGIN, y);
            _tft->print(F("No registers configured."));
            return;
        }

        int y = HDR_H + 2;
        uint8_t visible = min((size_t)5, regs.size());
        
        for (uint8_t i = 0; i < visible; i++) {
            size_t idx = (_rowOff + i) % regs.size();
            const auto &r = regs[idx];
            
            if (i % 2 == 0) {
                _tft->fillRect(0, y, _screenW, ROW_H, C_PANEL);
            }
            
            _tft->fillCircle(_screenW - 12, y + ROW_H/2, 4, r.commOk ? C_GREEN : C_RED);
            
            _tft->setTextColor(C_WHITE);
            _tft->setTextSize(1);
            _tft->setCursor(MARGIN + 2, y + 4);
            String name = r.name;
            if (name.length() > 14) name = name.substring(0, 13) + ".";
            _tft->print(name);
            
            _tft->setTextColor(C_LTGRAY);
            _tft->setTextSize(1);
            _tft->setCursor(MARGIN + 2, y + 16);
            _tft->print("ID:1");
            
            _tft->setTextColor(r.commOk ? C_ACCENT : C_LTGRAY);
            _tft->setTextSize(2);
            _tft->setCursor(140, y + 5);
            
            char vbuf[20];
            if (abs(r.value) < 1) {
                snprintf(vbuf, sizeof(vbuf), "%.3f", r.value);
            } else if (abs(r.value) < 100) {
                snprintf(vbuf, sizeof(vbuf), "%.2f", r.value);
            } else if (abs(r.value) < 10000) {
                snprintf(vbuf, sizeof(vbuf), "%.1f", r.value);
            } else {
                snprintf(vbuf, sizeof(vbuf), "%.0f", r.value);
            }
            _tft->print(vbuf);
            
            _tft->setTextSize(1);
            _tft->setTextColor(C_LTGRAY);
            String unit = r.unit;
            if (unit.length() > 4) unit = unit.substring(0, 4);
            if (unit.length() > 0) {
                int16_t x1, y1;
                uint16_t w, h;
                _tft->getTextBounds(vbuf, 140, y+5, &x1, &y1, &w, &h);
                _tft->setCursor(140 + w + 4, y + 10);
                _tft->print(unit);
            }
            
            _tft->drawFastHLine(0, y + ROW_H - 1, _screenW, C_DKGRAY);
            y += ROW_H;
        }

        if (regs.size() > 5) {
            _tft->setTextColor(C_LTGRAY);
            _tft->setTextSize(1);
            _tft->setCursor(MARGIN, _screenH - FOOTER_H - 2);
            _tft->printf("Showing %u-%u of %u",
                         (unsigned)(_rowOff + 1),
                         (unsigned)min((size_t)(_rowOff + 5), regs.size()),
                         (unsigned)regs.size());
        }
    }

    void _drawSystem(uint32_t freeHeap, bool sdOk, uint32_t logCount,
                     uint32_t uptimeSec, const String &lastDev, bool lastOk) {
        int y = HDR_H + 8;
        _sectionTitle("  SYSTEM STATUS", y); y += 22;
        _rowPair("Free Heap", String(freeHeap/1024) + " kB", y, C_WHITE); y += 20;
        _rowPair("Uptime", _fmtUptime(uptimeSec), y, C_WHITE); y += 20;
        _tft->drawFastHLine(MARGIN, y, _screenW-MARGIN*2, C_DKGRAY); y += 10;
        _sectionTitle("  SD CARD", y); y += 22;
        _rowPair("SD Status", sdOk ? "Mounted" : "Not found", y, sdOk ? C_GREEN : C_RED); y += 20;
        if (sdOk) {
            _rowPair("Log entries", String(logCount), y, C_ACCENT); y += 20;
        }
        _tft->drawFastHLine(MARGIN, y, _screenW-MARGIN*2, C_DKGRAY); y += 10;
        _sectionTitle("  RS485 RTU", y); y += 22;
        _rowPair("Last device", lastDev.isEmpty() ? "---" : lastDev, y, C_WHITE); y += 20;
        _rowPair("Last poll", lastOk ? "OK" : "FAIL", y, lastOk ? C_GREEN : C_RED);
    }

    void _drawFooter(uint8_t active) {
        int y = _screenH - FOOTER_H;
        _tft->fillRect(0, y, _screenW, FOOTER_H, C_BG);
        _tft->drawFastHLine(0, y, _screenW, C_DKGRAY);
        y += 4;
        
        static const char *labels[] = {"NET", "TCP", "REG", "SYS"};
        int spacing = _screenW / 4;
        for (int i = 0; i < 4; i++) {
            int x = i * spacing + spacing/2;
            if (i == active) {
                _tft->fillRoundRect(x-16, y, 32, 12, 4, C_ACCENT);
                _tft->setTextColor(C_BG);
            } else {
                _tft->drawRoundRect(x-16, y, 32, 12, 4, C_DKGRAY);
                _tft->setTextColor(C_DKGRAY);
            }
            _tft->setTextSize(1);
            _tft->setCursor(x-11, y+2);
            _tft->print(labels[i]);
        }
    }

    void _sectionTitle(const char *t, int y) {
        _tft->fillRect(0, y, _screenW, 14, C_PANEL);
        _tft->drawFastHLine(0, y, 3, C_ACCENT2);
        _tft->drawFastVLine(3, y, 14, C_ACCENT2);
        _tft->setTextColor(C_ACCENT);
        _tft->setTextSize(1);
        _tft->setCursor(8, y+3);
        _tft->print(t);
    }

    void _rowPair(const char *lbl, const String &val, int y, uint16_t valCol) {
        _tft->setTextColor(C_LTGRAY);
        _tft->setTextSize(1);
        _tft->setCursor(MARGIN+6, y);
        _tft->print(lbl);
        _tft->setTextColor(valCol);
        _tft->setCursor(130, y);
        _tft->print(val);
    }

    void _centreText(const char *t, int y, uint8_t sz) {
        _tft->setTextSize(sz);
        int16_t x1, y1; 
        uint16_t w, h;
        _tft->getTextBounds(t, 0, y, &x1, &y1, &w, &h);
        _tft->setCursor((_screenW - w) / 2, y);
        _tft->print(t);
    }

    String _fmtUptime(uint32_t s) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                 (unsigned)(s/3600), (unsigned)((s%3600)/60), (unsigned)(s%60));
        return String(buf);
    }
};