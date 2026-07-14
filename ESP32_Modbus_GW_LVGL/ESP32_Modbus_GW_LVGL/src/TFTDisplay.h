#pragma once
// ============================================================
//  TFTDisplay.h  –  ILI9341 2.8" 320×240 TFT (CYD ESP32-2432S028)
//  HSPI bus:  MISO=12  MOSI=13  SCK=14  CS=15  DC=2  RST=tied-HIGH(-1)
//  Backlight: GPIO21 (active-HIGH PWM or pull HIGH)
//  No touch.  4-screen auto-cycle (3 s each, overridden on data change)
//
//  Screens
//    0 – Header  + Network info
//    1 – Modbus TCP status
//    2 – RTU register live values (auto-scrolls if > 4 rows)
//    3 – SD card / heap / uptime
// ============================================================
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ── Pin definitions ────────────────────────────────────────────
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1   // RST tied HIGH on CYD board
#define TFT_MOSI 13
#define TFT_CLK  14
#define TFT_MISO 12
#define TFT_BL   21   // Backlight – GPIO21 (P3 header / VSPI_HD)

// ── Colour palette  (16-bit 565) ──────────────────────────────
#define C_BG        0x0841   // #080808 near-black
#define C_PANEL     0x10A2   // #102050 deep navy panel
#define C_ACCENT    0x04FF   // #0099FF CET blue
#define C_ACCENT2   0xFD20   // #FF6400 amber alert
#define C_GREEN     0x07E0   // pure green – OK status
#define C_RED       0xF800   // pure red  – FAIL
#define C_WHITE     0xFFFF
#define C_LTGRAY    0xC618   // light grey text
#define C_DKGRAY    0x4208   // dark separator line
#define C_YELLOW    0xFFE0
#define C_HEADER_BG 0x0168   // #012C48 header bar

// ── Layout constants ──────────────────────────────────────────
#define SCREEN_W   320
#define SCREEN_H   240
#define HDR_H       30   // top header bar height
#define ROW_H       36   // register row height
#define COL_LBL_W  148   // label column width
#define COL_VAL_W  100   // value column width
#define COL_UNIT_W  72   // unit column width
#define MARGIN       4

struct TFTRegSnapshot {
    String   name;
    double   value;
    String   unit;
    bool     commOk;
    uint32_t timestamp;
};

class TFTDisplay {
public:
    // ── begin ─────────────────────────────────────────────────
    void begin() {
        // Backlight on
        pinMode(TFT_BL, OUTPUT);
        digitalWrite(TFT_BL, HIGH);

        // HSPI bus (separate from VSPI used by SD)
        _spi.begin(TFT_CLK, TFT_MISO, TFT_MOSI, TFT_CS);
        _tft = new Adafruit_ILI9341(&_spi, TFT_DC, TFT_CS, TFT_RST);
        _tft->begin();
        _tft->setRotation(1);          // landscape, USB-left
        _tft->fillScreen(C_BG);
        _tft->setSPISpeed(40000000UL); // 40 MHz

        _showSplash();
        delay(2000);
        _tft->fillScreen(C_BG);
        Serial.println(F("[TFT] ILI9341 CYD ready"));
    }

    // ── Call every loop / task tick ───────────────────────────
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
            _screen   = (_screen + 1) % 4;
            _rowOff   = 0;
            _lastSwitch = now;
            _forceRedraw = true;
        }

        // scroll register screen every 2s if > 4 rows
        if (_screen == 2 && regs.size() > 4) {
            if (now - _lastScroll >= 2000) {
                _rowOff = (_rowOff + 4) % regs.size();
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
            case 1: _drawTCPScreen(tcpClients, tcpPort, uptimeSec);        break;
            case 2: _drawRegisters(regs);                                   break;
            case 3: _drawSystem(freeHeap, sdOk, sdLogCount, uptimeSec,
                                lastRTUDev, lastRTUOk);                    break;
        }

        _drawFooter(_screen);
    }

    // ── Force immediate redraw ────────────────────────────────
    void forceRedraw() { _forceRedraw = true; }
    void setScreenMs(uint32_t ms) { _screenMs = ms; }

    // ── Boot message ──────────────────────────────────────────
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
    SPIClass              _spi      = SPIClass(HSPI);
    Adafruit_ILI9341     *_tft      = nullptr;
    uint8_t               _screen   = 0;
    uint32_t              _lastSwitch = 0;
    uint32_t              _lastScroll = 0;
    uint32_t              _screenMs   = 4000;  // 4 s per screen
    bool                  _forceRedraw = true;
    uint8_t               _rowOff     = 0;

    // ─── Splash ───────────────────────────────────────────────
    void _showSplash() {
        _tft->fillScreen(C_BG);
        // Top gradient bar
        for (int y = 0; y < 8; y++) {
            uint16_t c = _tft->color565(0, y*12, y*20+80);
            _tft->drawFastHLine(0, y, SCREEN_W, c);
        }
        _tft->fillRect(0, 8, SCREEN_W, 56, C_HEADER_BG);

        _tft->setTextColor(C_ACCENT);
        _tft->setTextSize(3);
        _centreText("MODBUS", 14, 3);
        _tft->setTextColor(C_WHITE);
        _centreText("RTU → TCP GATEWAY", 44, 1);

        _tft->drawFastHLine(10, 68, SCREEN_W-20, C_ACCENT);

        _tft->setTextColor(C_LTGRAY);
        _tft->setTextSize(1);
        _centreText("CET Power Solutions", 80, 1);
        _centreText("ESP32-2432S028  |  v2.1.0", 96, 1);

        _tft->setTextColor(C_DKGRAY);
        _centreText("Initialising...", 120, 1);

        // Bottom bar
        _tft->fillRect(0, SCREEN_H-24, SCREEN_W, 24, C_HEADER_BG);
        _tft->setTextColor(C_ACCENT2);
        _centreText("ILI9341 TFT  |  HSPI  |  SD", SCREEN_H-16, 1);
    }

    // ─── Persistent top header ────────────────────────────────
    void _drawHeader(const String &ip, const String &mode,
                     int rssi, uint32_t uptimeSec) {
        _tft->fillRect(0, 0, SCREEN_W, HDR_H, C_HEADER_BG);
        _tft->drawFastHLine(0, HDR_H, SCREEN_W, C_ACCENT);

        // Left: logo text
        _tft->setTextColor(C_ACCENT);
        _tft->setTextSize(1);
        _tft->setCursor(MARGIN, 6);
        _tft->print(F("MODBUS GW"));

        // Centre: IP
        _tft->setTextColor(C_WHITE);
        _tft->setCursor(90, 6);
        _tft->print(mode + "  " + ip);

        // Right: RSSI or AP indicator
        _tft->setCursor(SCREEN_W - 56, 6);
        if (mode == "STA") {
            _tft->setTextColor(rssi > -70 ? C_GREEN : C_YELLOW);
            _tft->printf("RSSI:%d", rssi);
        } else {
            _tft->setTextColor(C_ACCENT2);
            _tft->print(F("AP MODE"));
        }

        // Uptime line
        _tft->setTextColor(C_DKGRAY);
        _tft->setCursor(MARGIN, 18);
        uint32_t h = uptimeSec/3600, m=(uptimeSec%3600)/60, s=uptimeSec%60;
        _tft->printf("UP %02u:%02u:%02u", h, m, s);
    }

    // ─── Screen 0: Network detail ─────────────────────────────
    void _drawNetwork(const String &ip, const String &mode, int rssi,
                      uint8_t clients, uint16_t port) {
        int y = HDR_H + 10;
        _sectionTitle("  NETWORK STATUS", y); y += 22;

        _rowPair("Mode",     mode,                y, C_ACCENT);  y += 20;
        _rowPair("IP Addr",  ip,                  y, C_WHITE);   y += 20;
        _rowPair("TCP Port", String(port),         y, C_WHITE);   y += 20;
        _rowPair("TCP Clients", String(clients),  y, clients > 0 ? C_GREEN : C_LTGRAY); y += 20;

        if (mode == "STA") {
            uint16_t col = rssi > -60 ? C_GREEN : rssi > -75 ? C_YELLOW : C_RED;
            _rowPair("RSSI", String(rssi) + " dBm", y, col); y += 20;
            // Signal bar
            _drawSignalBars(SCREEN_W - 50, y - 14, rssi);
        }

        _tft->drawFastHLine(MARGIN, y+2, SCREEN_W-MARGIN*2, C_DKGRAY);
        y += 10;
        _tft->setTextColor(C_DKGRAY);
        _tft->setTextSize(1);
        _tft->setCursor(MARGIN, y);
        _tft->print(F("mDNS: modbus-gw.local"));
    }

    // ─── Screen 1: TCP/RTU summary ────────────────────────────
    void _drawTCPScreen(uint8_t clients, uint16_t port, uint32_t uptimeSec) {
        int y = HDR_H + 8;
        _sectionTitle("  MODBUS TCP", y); y += 22;

        // Big client count
        _tft->setTextColor(C_ACCENT);
        _tft->setTextSize(1);
        _tft->setCursor(MARGIN, y);
        _tft->print(F("Connected clients"));
        _tft->setTextSize(5);
        _tft->setTextColor(clients > 0 ? C_GREEN : C_LTGRAY);
        _tft->setCursor(140, y-4);
        _tft->print(clients);
        y += 42;

        _tft->setTextSize(1);
        _tft->setTextColor(C_DKGRAY);
        _tft->drawFastHLine(MARGIN, y, SCREEN_W-MARGIN*2, C_DKGRAY);
        y += 8;

        _rowPair("Port",     String(port),   y, C_WHITE); y += 18;
        _rowPair("Max clients", "10",         y, C_LTGRAY); y += 18;
        _rowPair("Idle timeout","5000 ms",    y, C_LTGRAY); y += 18;
    }

    // ─── Screen 2: Live registers ─────────────────────────────
    void _drawRegisters(const std::vector<TFTRegSnapshot> &regs) {
        int y = HDR_H + 4;

        // Column headers
        _tft->fillRect(0, y, SCREEN_W, 14, C_PANEL);
        _tft->setTextColor(C_ACCENT);
        _tft->setTextSize(1);
        _tft->setCursor(MARGIN, y+3);  _tft->print(F("REGISTER"));
        _tft->setCursor(COL_LBL_W+MARGIN, y+3); _tft->print(F("VALUE"));
        _tft->setCursor(COL_LBL_W+COL_VAL_W+MARGIN, y+3); _tft->print(F("UNIT"));
        _tft->setCursor(SCREEN_W-28, y+3); _tft->print(F("STS"));
        y += 16;

        if (regs.empty()) {
            _tft->setTextColor(C_DKGRAY);
            _tft->setCursor(MARGIN, y+20);
            _tft->print(F("No registers configured."));
            return;
        }

        uint8_t visible = min((size_t)5, regs.size());
        for (uint8_t i = 0; i < visible; i++) {
            size_t idx = (_rowOff + i) % regs.size();
            const auto &r = regs[idx];

            // Alternating row shade
            if (i % 2 == 0)
                _tft->fillRect(0, y, SCREEN_W, ROW_H-2, C_PANEL);

            // Status indicator dot
            _tft->fillCircle(SCREEN_W-14, y+ROW_H/2-2, 5,
                             r.commOk ? C_GREEN : C_RED);

            // Name (truncate to fit)
            _tft->setTextColor(C_WHITE);
            _tft->setTextSize(1);
            _tft->setCursor(MARGIN, y+4);
            String nm = r.name.length() > 16 ? r.name.substring(0,16) : r.name;
            _tft->print(nm);

            // Slave ID badge
            _tft->setTextColor(C_DKGRAY);
            _tft->setCursor(MARGIN, y+16);
            _tft->printf("Slave %d", (int)0); // placeholder; extend if needed

            // Value  – large text
            _tft->setTextColor(r.commOk ? C_ACCENT : C_DKGRAY);
            _tft->setTextSize(2);
            _tft->setCursor(COL_LBL_W+MARGIN, y+8);
            char vbuf[16];
            if (r.value == (long)r.value && abs(r.value) < 100000)
                snprintf(vbuf, sizeof(vbuf), "%.0f", r.value);
            else
                snprintf(vbuf, sizeof(vbuf), "%.2f", r.value);
            _tft->print(vbuf);

            // Unit
            _tft->setTextSize(1);
            _tft->setTextColor(C_LTGRAY);
            _tft->setCursor(COL_LBL_W+COL_VAL_W+MARGIN, y+12);
            _tft->print(r.unit.length() > 6 ? r.unit.substring(0,6) : r.unit);

            _tft->drawFastHLine(0, y+ROW_H-2, SCREEN_W, C_DKGRAY);
            y += ROW_H;
        }

        // Scroll indicator
        if (regs.size() > 5) {
            _tft->setTextColor(C_DKGRAY);
            _tft->setTextSize(1);
            _tft->setCursor(MARGIN, SCREEN_H - 12);
            _tft->printf("Showing %u-%u of %u  (auto-scroll)",
                         (unsigned)(_rowOff+1),
                         (unsigned)min((size_t)(_rowOff+5), regs.size()),
                         (unsigned)regs.size());
        }
    }

    // ─── Screen 3: System / SD ────────────────────────────────
    void _drawSystem(uint32_t freeHeap, bool sdOk, uint32_t logCount,
                     uint32_t uptimeSec,
                     const String &lastDev, bool lastOk) {
        int y = HDR_H + 8;
        _sectionTitle("  SYSTEM STATUS", y); y += 22;

        _rowPair("Free Heap", String(freeHeap/1024) + " kB",  y, C_WHITE);  y += 18;
        _rowPair("Uptime",    _fmtUptime(uptimeSec),           y, C_WHITE);  y += 18;

        _tft->drawFastHLine(MARGIN, y, SCREEN_W-MARGIN*2, C_DKGRAY); y += 8;

        _sectionTitle("  SD CARD", y); y += 20;
        uint16_t sdCol = sdOk ? C_GREEN : C_RED;
        _rowPair("SD Status",  sdOk ? "Mounted" : "Not found", y, sdCol);   y += 18;
        if (sdOk) {
            _rowPair("Log entries", String(logCount), y, C_ACCENT); y += 18;
        }

        _tft->drawFastHLine(MARGIN, y, SCREEN_W-MARGIN*2, C_DKGRAY); y += 8;

        _sectionTitle("  RS485 RTU", y); y += 20;
        _rowPair("Last device", lastDev.isEmpty() ? "---" : lastDev, y, C_WHITE); y += 18;
        _rowPair("Last poll",   lastOk ? "OK" : "FAIL", y,
                 lastOk ? C_GREEN : C_RED);
    }

    // ─── Footer / page dots ───────────────────────────────────
    void _drawFooter(uint8_t active) {
        int y = SCREEN_H - 10;
        _tft->fillRect(0, y-2, SCREEN_W, 12, C_BG);
        static const char *labels[] = {"NET","TCP","REG","SYS"};
        int spacing = SCREEN_W / 4;
        for (int i = 0; i < 4; i++) {
            int x = i * spacing + spacing/2;
            if (i == active) {
                _tft->fillRoundRect(x-14, y-1, 28, 10, 4, C_ACCENT);
                _tft->setTextColor(C_BG);
            } else {
                _tft->drawRoundRect(x-14, y-1, 28, 10, 4, C_DKGRAY);
                _tft->setTextColor(C_DKGRAY);
            }
            _tft->setTextSize(1);
            _tft->setCursor(x-11, y+1);
            _tft->print(labels[i]);
        }
    }

    // ─── Helpers ──────────────────────────────────────────────
    void _sectionTitle(const char *t, int y) {
        _tft->fillRect(0, y, SCREEN_W, 14, C_PANEL);
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
        _tft->setCursor(140, y);
        _tft->print(val);
    }

    void _centreText(const char *t, int y, uint8_t sz) {
        _tft->setTextSize(sz);
        int16_t x1,y1; uint16_t w,h;
        _tft->getTextBounds(t,0,y,&x1,&y1,&w,&h);
        _tft->setCursor((SCREEN_W-w)/2, y);
        _tft->print(t);
    }

    void _drawSignalBars(int x, int y, int rssi) {
        // 4 bars
        for (int b = 0; b < 4; b++) {
            int threshold = -90 + b*10;
            uint16_t col  = rssi >= threshold ? C_GREEN : C_DKGRAY;
            int bh = (b+1) * 4;
            _tft->fillRect(x + b*8, y + (16-bh), 6, bh, col);
        }
    }

    String _fmtUptime(uint32_t s) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                 (unsigned)(s/3600), (unsigned)((s%3600)/60), (unsigned)(s%60));
        return String(buf);
    }
};
