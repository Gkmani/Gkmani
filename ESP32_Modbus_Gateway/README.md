# ESP32-WROOM Modbus RTU→TCP Gateway v2.1.0
No async libraries – 100% ESP32 built-in WebServer stack.

## Library Manager – Install these 4 libraries only:
| Library           | Author     | Version |
|-------------------|------------|---------|
| ArduinoJson       | bblanchon  | 6.x     |
| WebSockets        | links2004  | 2.4.x   |
| Adafruit SSD1306  | Adafruit   | 2.5.x   |
| Adafruit GFX      | Adafruit   | 1.11.x  |

## Remove / do NOT install:
- ESPAsyncWebServer (any version)
- AsyncTCP (any version)
- Async_TCP

## Pin Map
| Signal     | GPIO |
|------------|------|
| RS485 RX   | 16   |
| RS485 TX   | 17   |
| RS485 DE   | 4    |
| OLED SDA   | 21   |
| OLED SCL   | 22   |

## Board Settings (Arduino IDE)
- Board: ESP32 Dev Module
- Flash size: 4MB (with LittleFS)
- Partition: Default 4MB with spiffs

## Upload LittleFS data
Install plugin: ESP32 LittleFS Data Upload
Then: Tools → ESP32 LittleFS Data Upload

## First Boot
1. Connect to AP: ModbusGW-ESP32 / Admin12345
2. Open: http://192.168.4.1
3. Login: admin / Admin12345
4. System Settings → enter Wi-Fi → Save & Reboot
