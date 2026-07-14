#pragma once
// ============================================================
//  OLEDDisplay.h – SSD1306 128×64 I2C  (GPIO21 SDA / GPIO22 SCL)
//  3-screen cycle: IP+Mode → TCP Clients → RTU Last Poll
// ============================================================
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_W     128
#define OLED_H      64
#define OLED_ADDR 0x3C

class OLEDDisplay {
public:
    void begin() {
        _d = new Adafruit_SSD1306(OLED_W, OLED_H, &Wire, -1);
        if (!_d->begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
            Serial.println(F("[OLED] Not found"));
            _ok = false; return;
        }
        _ok = true;
        Serial.println(F("[OLED] SSD1306 OK"));
    }

    void showBoot(const char *t, const char *v) {
        if (!_ok) return;
        _d->clearDisplay();
        _d->drawRect(0,0,128,64,SSD1306_WHITE);
        _d->setTextSize(1); _d->setTextColor(SSD1306_WHITE);
        _d->setCursor(8,8);  _d->println(F("CET Power Solutions"));
        _d->setTextSize(2);  _d->setCursor(4,24); _d->println(t);
        _d->setTextSize(1);  _d->setCursor(48,50); _d->println(v);
        _d->display();
    }

    void showMsg(const char *l1, const char *l2="") {
        if (!_ok) return;
        _d->clearDisplay();
        _d->setTextColor(SSD1306_WHITE);
        _d->setTextSize(1); _d->setCursor(0,8);  _d->println(l1);
        _d->setTextSize(1); _d->setCursor(0,28); _d->println(l2);
        _d->display();
    }

    // Screen 0 – IP + mode
    void showIP(const char *ip, const char *mode) {
        if (!_ok) return;
        _d->clearDisplay();
        _hdr("NETWORK");
        _d->setTextColor(SSD1306_WHITE);
        _d->setTextSize(1); _d->setCursor(0,16);
        _d->print(F("Mode : ")); _d->println(mode);
        _d->setCursor(0,28); _d->println(F("IP Address :"));
        _d->setTextSize(2); _d->setCursor(0,42); _d->println(ip);
        _d->display();
    }

    // Screen 1 – TCP clients
    void showTCPClients(uint8_t n, uint16_t port) {
        if (!_ok) return;
        _d->clearDisplay();
        _hdr("MODBUS TCP");
        _d->setTextColor(SSD1306_WHITE);
        _d->setTextSize(1); _d->setCursor(0,18);
        _d->print(F("Port   : ")); _d->println(port);
        _d->setCursor(0,30); _d->println(F("Clients :"));
        _d->setTextSize(3); _d->setCursor(56,36); _d->println(n);
        _d->display();
    }

    // Screen 2 – RTU last poll
    void showRTUStatus(const String &dev, bool ok) {
        if (!_ok) return;
        _d->clearDisplay();
        _hdr("RS485 RTU");
        _d->setTextColor(SSD1306_WHITE);
        _d->setTextSize(1); _d->setCursor(0,18);
        _d->println(F("Last device :"));
        _d->setCursor(0,30);
        String n = dev.isEmpty() ? "---" :
                   (dev.length()>20 ? dev.substring(0,20) : dev);
        _d->println(n);
        _d->setCursor(0,48);
        _d->println(ok ? F("[  OK  ]") : F("[ FAIL ]"));
        _d->display();
    }

private:
    Adafruit_SSD1306 *_d = nullptr;
    bool _ok = false;

    void _hdr(const char *t) {
        _d->fillRect(0,0,128,12,SSD1306_WHITE);
        _d->setTextColor(SSD1306_BLACK);
        _d->setTextSize(1); _d->setCursor(2,2); _d->print(t);
        _d->setTextColor(SSD1306_WHITE);
    }
};
