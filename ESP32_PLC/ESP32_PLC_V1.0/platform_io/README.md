# ESP32 PLC Final Firmware - README

This project is a full firmware skeleton for an ESP32-based PLC controller featuring:
- Structured pin mapping for DI/DO/ADC
- SD card-based script storage (`/scripts/` path recommended)
- Web UI to upload and list scripts, and select which script to load and execute
- Ladder logic text parser & executor (simple format)
- "Mini-Python" line-based interpreter for light logic scripts (safe and small)
- Modbus RTU support (register 0 maps to ADC)

**Notes & Extension Points**
- Lua interpreter is marked as a placeholder; to support full Lua, embed a Lua VM and expose IO functions.
- For full MicroPython support, consider using MicroPython firmware or integrating its interpreter into ESP-IDF.
- Security: current web UI uses no auth; for production implement HTTPS and user auth.
