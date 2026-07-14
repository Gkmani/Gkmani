#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>
#include <ModbusRTU.h>
#include <EEPROM.h>
#include <mbedtls/sha256.h>  // For SHA256 hashing
#include "driver/ledc.h"     // For LEDC PWM functions

// ========== Configuration ==========
#define SD_CS_PIN 5
#define DI1_PIN 34
#define DI2_PIN 35
#define DO1_PIN 32
#define DO2_PIN 33
#define ADC1_PIN 36
#define MODBUS_RX_PIN 16
#define MODBUS_TX_PIN 17

// Default credentials (change in production)
const char* DEFAULT_SSID = "ESP32-PLC";
const char* DEFAULT_PASSWORD = "plc-secure";
const char* DEFAULT_ADMIN_USER = "admin";
const char* DEFAULT_ADMIN_PASS = "admin123"; // Will be hashed

// ========== Global Objects ==========
WebServer server(80);
ModbusRTU mb;

struct User {
  String username;
  String passwordHash;
  uint8_t permissions; // 0x01=read, 0x02=write, 0x04=admin
};

struct Config {
  String wifiSSID;
  String wifiPassword;
  uint8_t modbusSlaveId;
  uint32_t modbusBaudRate;
  String defaultLogicPath;
  User users[5]; // Max 5 users
  uint8_t userCount = 0;
};

Config config;
String currentScript;
int scriptType = 0; // 0=none, 1=ladder, 2=minipy, 3=lua

// ========== Helper Functions ==========
String hashPassword(const String& password) {
  unsigned char digest[32];
  mbedtls_sha256_context ctx;
  
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256, not SHA-224
  mbedtls_sha256_update(&ctx, (const unsigned char*)password.c_str(), password.length());
  mbedtls_sha256_finish(&ctx, digest);
  mbedtls_sha256_free(&ctx);
  
  String hash;
  for(int i=0; i<32; i++) {
    char buf[3];
    sprintf(buf, "%02x", digest[i]);
    hash += buf;
  }
  return hash;
}

bool checkAuth(WebServer& server) {
  if(!server.authenticate(DEFAULT_ADMIN_USER, DEFAULT_ADMIN_PASS)) {
    if(server.hasHeader("Cookie")) {
      String cookie = server.header("Cookie");
      if(cookie.indexOf("ESPSESSIONID=") != -1) {
        // In real implementation, validate session
        return true;
      }
    }
    server.requestAuthentication();
    return false;
  }
  return true;
}

void saveConfig() {
  EEPROM.begin(512);
  String json;
  json += "{\"wifiSSID\":\"" + config.wifiSSID + "\",";
  json += "\"wifiPassword\":\"" + config.wifiPassword + "\",";
  json += "\"modbusSlaveId\":" + String(config.modbusSlaveId) + ",";
  json += "\"modbusBaudRate\":" + String(config.modbusBaudRate) + ",";
  json += "\"defaultLogicPath\":\"" + config.defaultLogicPath + "\",";
  json += "\"users\":[";
  for(int i=0; i<config.userCount; i++) {
    if(i > 0) json += ",";
    json += "{\"username\":\"" + config.users[i].username + "\",";
    json += "\"passwordHash\":\"" + config.users[i].passwordHash + "\",";
    json += "\"permissions\":" + String(config.users[i].permissions) + "}";
  }
  json += "]}";
  
  for(int i=0; i<json.length(); i++) {
    EEPROM.write(i, json[i]);
  }
  EEPROM.commit();
}

void loadConfig() {
  EEPROM.begin(512);
  String json;
  for(int i=0; i<512; i++) {
    char c = EEPROM.read(i);
    if(c == 0) break;
    json += c;
  }
  
  if(json.length() == 0 || json == "null") {
    // Initialize default config
    config.wifiSSID = DEFAULT_SSID;
    config.wifiPassword = DEFAULT_PASSWORD;
    config.modbusSlaveId = 1;
    config.modbusBaudRate = 9600;
    config.defaultLogicPath = "/logic.txt";
    
    config.users[0].username = DEFAULT_ADMIN_USER;
    config.users[0].passwordHash = hashPassword(DEFAULT_ADMIN_PASS);
    config.users[0].permissions = 0x07; // All permissions
    config.userCount = 1;
    
    saveConfig();
    return;
  }
  
  // Simple JSON parsing (for demo - use ArduinoJSON in production)
  int pos = json.indexOf("\"wifiSSID\":\"") + 11;
  int end = json.indexOf("\"", pos);
  config.wifiSSID = json.substring(pos, end);
  
  pos = json.indexOf("\"wifiPassword\":\"") + 15;
  end = json.indexOf("\"", pos);
  config.wifiPassword = json.substring(pos, end);
  
  pos = json.indexOf("\"modbusSlaveId\":") + 15;
  end = json.indexOf(",", pos);
  config.modbusSlaveId = json.substring(pos, end).toInt();
  
  pos = json.indexOf("\"modbusBaudRate\":") + 16;
  end = json.indexOf(",", pos);
  config.modbusBaudRate = json.substring(pos, end).toInt();
  
  pos = json.indexOf("\"defaultLogicPath\":\"") + 19;
  end = json.indexOf("\"", pos);
  config.defaultLogicPath = json.substring(pos, end);
  
  // Parse users
  pos = json.indexOf("\"users\":[") + 8;
  while(pos < json.length() && json.charAt(pos) != ']') {
    if(json.charAt(pos) == '{') {
      User user;
      int userEnd = json.indexOf("}", pos);
      String userStr = json.substring(pos, userEnd+1);
      
      int uPos = userStr.indexOf("\"username\":\"") + 11;
      int uEnd = userStr.indexOf("\"", uPos);
      user.username = userStr.substring(uPos, uEnd);
      
      uPos = userStr.indexOf("\"passwordHash\":\"") + 15;
      uEnd = userStr.indexOf("\"", uPos);
      user.passwordHash = userStr.substring(uPos, uEnd);
      
      uPos = userStr.indexOf("\"permissions\":") + 13;
      uEnd = userStr.indexOf("}", uPos);
      user.permissions = userStr.substring(uPos, uEnd).toInt();
      
      config.users[config.userCount++] = user;
      pos = userEnd + 1;
    } else {
      pos++;
    }
  }
}

// ========== IO Functions ==========
void setupIO() {
  pinMode(DI1_PIN, INPUT);
  pinMode(DI2_PIN, INPUT);
  pinMode(DO1_PIN, OUTPUT);
  pinMode(DO2_PIN, OUTPUT);
  
  // Configure PWM
  ledc_timer_config_t timer_conf = {
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .duty_resolution = LEDC_TIMER_8_BIT,
    .timer_num = LEDC_TIMER_0,
    .freq_hz = 5000,
    .clk_cfg = LEDC_AUTO_CLK
  };
  ledc_timer_config(&timer_conf);
  
  ledc_channel_config_t channel_conf = {
    .gpio_num = DO2_PIN,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = LEDC_CHANNEL_0,
    .intr_type = LEDC_INTR_DISABLE,
    .timer_sel = LEDC_TIMER_0,
    .duty = 0,
    .hpoint = 0
  };
  ledc_channel_config(&channel_conf);
  
  digitalWrite(DO1_PIN, LOW);
  digitalWrite(DO2_PIN, LOW);
}

bool readDigital(uint8_t pin) {
  return digitalRead(pin);
}

void writeDigital(uint8_t pin, bool value) {
  digitalWrite(pin, value ? HIGH : LOW);
}

int readAnalog(uint8_t pin) {
  return analogRead(pin);
}

void writeAnalog(uint8_t pin, int value) {
  ledcWrite(0, constrain(value, 0, 255));
}

// ========== Modbus Functions ==========
void setupModbus() {
  Serial2.begin(config.modbusBaudRate, SERIAL_8N1, MODBUS_RX_PIN, MODBUS_TX_PIN);
  mb.begin(&Serial2);
  mb.slave(config.modbusSlaveId);
  
  // Setup registers
  mb.addCoil(0); // DO1
  mb.addCoil(1); // DO2
  mb.addIreg(0); // DI1
  mb.addIreg(1); // DI2
  mb.addIreg(2); // ADC1
  mb.addHreg(0); // Holding register 0
  mb.addHreg(1); // Holding register 1
}

void updateModbus() {
  mb.task();
  
  // Update input registers
  mb.Ireg(0, readDigital(DI1_PIN));
  mb.Ireg(1, readDigital(DI2_PIN));
  mb.Ireg(2, readAnalog(ADC1_PIN) >> 2); // Scale to 0-1023
  
  // Sync coils with outputs
  if(mb.Coil(0) != readDigital(DO1_PIN)) {
    writeDigital(DO1_PIN, mb.Coil(0));
  }
  if(mb.Coil(1) != readDigital(DO2_PIN)) {
    writeDigital(DO2_PIN, mb.Coil(1));
  }
}

// ========== Script Engine ==========
bool loadScript(const String& path) {
  if(!SD.exists(path)) return false;
  
  File file = SD.open(path, FILE_READ);
  if(!file) return false;
  
  currentScript = file.readString();
  file.close();
  
  if(path.endsWith(".lua")) scriptType = 3;
  else if(path.endsWith(".py")) scriptType = 2;
  else scriptType = 1; // Default to ladder
  
  return true;
}

void executeScript() {
  if(scriptType == 0) return;
  
  // Simple ladder logic interpreter
  if(scriptType == 1) {
    bool acc = false;
    int lineStart = 0;
    
    while(lineStart < currentScript.length()) {
      int lineEnd = currentScript.indexOf('\n', lineStart);
      if(lineEnd == -1) lineEnd = currentScript.length();
      
      String line = currentScript.substring(lineStart, lineEnd);
      line.trim();
      lineStart = lineEnd + 1;
      
      if(line.length() == 0 || line.startsWith("#")) continue;
      
      // Simple ladder parser
      if(line.startsWith("LD ")) {
        String arg = line.substring(3);
        arg.trim();
        if(arg == "DI1") acc = readDigital(DI1_PIN);
        else if(arg == "DI2") acc = readDigital(DI2_PIN);
      }
      else if(line.startsWith("AND ")) {
        String arg = line.substring(4);
        arg.trim();
        if(arg == "DI1") acc &= readDigital(DI1_PIN);
        else if(arg == "DI2") acc &= readDigital(DI2_PIN);
      }
      else if(line.startsWith("OR ")) {
        String arg = line.substring(3);
        arg.trim();
        if(arg == "DI1") acc |= readDigital(DI1_PIN);
        else if(arg == "DI2") acc |= readDigital(DI2_PIN);
      }
      else if(line.startsWith("OUT ")) {
        String arg = line.substring(4);
        arg.trim();
        if(arg == "DO1") writeDigital(DO1_PIN, acc);
        else if(arg == "DO2") writeDigital(DO2_PIN, acc);
      }
    }
  }
  // Mini-Python interpreter (very limited)
  else if(scriptType == 2) {
    int lineStart = 0;
    
    while(lineStart < currentScript.length()) {
      int lineEnd = currentScript.indexOf('\n', lineStart);
      if(lineEnd == -1) lineEnd = currentScript.length();
      
      String line = currentScript.substring(lineStart, lineEnd);
      line.trim();
      lineStart = lineEnd + 1;
      
      if(line.length() == 0 || line.startsWith("#")) continue;
      
      // Simple assignments
      int eqPos = line.indexOf('=');
      if(eqPos != -1) {
        String left = line.substring(0, eqPos);
        left.trim();
        String right = line.substring(eqPos+1);
        right.trim();
        
        if(left == "DO1") writeDigital(DO1_PIN, right.toInt() != 0);
        else if(left == "DO2") writeDigital(DO2_PIN, right.toInt() != 0);
      }
    }
  }
}

// ========== Web Server Handlers ==========
void handleRoot() {
  if(!checkAuth(server)) return;
  
  String html = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 PLC Control Panel</title>
  <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.1.3/dist/css/bootstrap.min.css" rel="stylesheet">
  <link href="https://cdn.jsdelivr.net/npm/bootstrap-icons@1.8.0/font/bootstrap-icons.css" rel="stylesheet">
  <style>
    body { padding-top: 20px; background-color: #f8f9fa; }
    .card { margin-bottom: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
    .status-card { background-color: #fff; border-left: 4px solid #0d6efd; }
    .digital-input { color: #dc3545; }
    .digital-output { color: #198754; }
    .analog-value { color: #6f42c1; }
    .navbar-brand { font-weight: bold; }
    .tab-content { padding: 15px; border: 1px solid #dee2e6; border-top: none; border-radius: 0 0 5px 5px; }
  </style>
</head>
<body>
  <div class="container">
    <nav class="navbar navbar-expand-lg navbar-dark bg-primary rounded mb-4">
      <div class="container-fluid">
        <a class="navbar-brand" href="#">
          <i class="bi bi-cpu"></i> ESP32 PLC
        </a>
        <button class="navbar-toggler" type="button" data-bs-toggle="collapse" data-bs-target="#navbarNav">
          <span class="navbar-toggler-icon"></span>
        </button>
        <div class="collapse navbar-collapse" id="navbarNav">
          <ul class="navbar-nav">
            <li class="nav-item">
              <a class="nav-link active" href="#" onclick="showTab('dashboard')">
                <i class="bi bi-speedometer2"></i> Dashboard
              </a>
            </li>
            <li class="nav-item">
              <a class="nav-link" href="#" onclick="showTab('io')">
                <i class="bi bi-toggle-on"></i> I/O Control
              </a>
            </li>
            <li class="nav-item">
              <a class="nav-link" href="#" onclick="showTab('scripts')">
                <i class="bi bi-file-code"></i> Scripts
              </a>
            </li>
            <li class="nav-item">
              <a class="nav-link" href="#" onclick="showTab('settings')">
                <i class="bi bi-gear"></i> Settings
              </a>
            </li>
          </ul>
          <ul class="navbar-nav ms-auto">
            <li class="nav-item dropdown">
              <a class="nav-link dropdown-toggle" href="#" id="userDropdown" role="button" data-bs-toggle="dropdown">
                <i class="bi bi-person-circle"></i> Admin
              </a>
              <ul class="dropdown-menu dropdown-menu-end">
                <li><a class="dropdown-item" href="#" onclick="logout()">Logout</a></li>
              </ul>
            </li>
          </ul>
        </div>
      </div>
    </nav>

    <!-- Dashboard Tab -->
    <div id="dashboard" class="tab-content active">
      <div class="row">
        <div class="col-md-6">
          <div class="card status-card">
            <div class="card-header bg-primary text-white">
              <i class="bi bi-input-cursor-text"></i> Input Status
            </div>
            <div class="card-body">
              <table class="table">
                <tr>
                  <td><i class="bi bi-circle-fill digital-input"></i> DI1</td>
                  <td id="di1-status">?</td>
                </tr>
                <tr>
                  <td><i class="bi bi-circle-fill digital-input"></i> DI2</td>
                  <td id="di2-status">?</td>
                </tr>
                <tr>
                  <td><i class="bi bi-speedometer2 analog-value"></i> ADC</td>
                  <td id="adc-value">?</td>
                </tr>
              </table>
            </div>
          </div>
        </div>
        <div class="col-md-6">
          <div class="card status-card">
            <div class="card-header bg-primary text-white">
              <i class="bi bi-output-cursor"></i> Output Status
            </div>
            <div class="card-body">
              <table class="table">
                <tr>
                  <td><i class="bi bi-circle-fill digital-output"></i> DO1</td>
                  <td id="do1-status">?</td>
                </tr>
                <tr>
                  <td><i class="bi bi-circle-fill digital-output"></i> DO2</td>
                  <td id="do2-status">?</td>
                </tr>
                <tr>
                  <td><i class="bi bi-file-code"></i> Current Script</td>
                  <td id="script-type">None</td>
                </tr>
              </table>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- I/O Control Tab -->
    <div id="io" class="tab-content" style="display:none;">
      <div class="card">
        <div class="card-header bg-primary text-white">
          <i class="bi bi-toggle-on"></i> Digital Output Control
        </div>
        <div class="card-body">
          <div class="mb-3">
            <label class="form-label">DO1</label>
            <div class="form-check form-switch">
              <input class="form-check-input" type="checkbox" id="do1-switch">
              <label class="form-check-label" for="do1-switch">Toggle Output</label>
            </div>
          </div>
          <div class="mb-3">
            <label class="form-label">DO2 (PWM)</label>
            <input type="range" class="form-range" min="0" max="255" id="do2-pwm">
            <div class="text-center" id="pwm-value">0</div>
          </div>
          <button class="btn btn-primary" onclick="updateOutputs()">
            <i class="bi bi-check-circle"></i> Apply Changes
          </button>
        </div>
      </div>
    </div>

    <!-- Scripts Tab -->
    <div id="scripts" class="tab-content" style="display:none;">
      <div class="card">
        <div class="card-header bg-primary text-white">
          <i class="bi bi-file-earmark-code"></i> Script Management
        </div>
        <div class="card-body">
          <ul class="nav nav-tabs" id="scriptTabs">
            <li class="nav-item">
              <a class="nav-link active" href="#" onclick="showScriptTab('upload')">Upload</a>
            </li>
            <li class="nav-item">
              <a class="nav-link" href="#" onclick="showScriptTab('manage')">Manage</a>
            </li>
            <li class="nav-item">
              <a class="nav-link" href="#" onclick="showScriptTab('editor')">Editor</a>
            </li>
          </ul>
          
          <!-- Upload Tab -->
          <div id="upload-script" class="script-tab-content">
            <div class="mb-3 mt-3">
              <label for="script-upload" class="form-label">Upload Script File</label>
              <input class="form-control" type="file" id="script-upload">
            </div>
            <button class="btn btn-primary" onclick="uploadScript()">
              <i class="bi bi-upload"></i> Upload
            </button>
            <div class="alert alert-info mt-3">
              Supported formats: .txt (Ladder), .py (Mini-Python), .lua (Lua)
            </div>
          </div>
          
          <!-- Manage Tab -->
          <div id="manage-scripts" class="script-tab-content" style="display:none;">
            <button class="btn btn-primary mb-3" onclick="listScripts()">
              <i class="bi bi-arrow-clockwise"></i> Refresh List
            </button>
            <div class="table-responsive">
              <table class="table table-striped">
                <thead>
                  <tr>
                    <th>Filename</th>
                    <th>Actions</th>
                  </tr>
                </thead>
                <tbody id="script-list">
                  <!-- Scripts will be loaded here -->
                </tbody>
              </table>
            </div>
          </div>
          
          <!-- Editor Tab -->
          <div id="script-editor-tab" class="script-tab-content" style="display:none;">
            <div class="mb-3 mt-3">
              <label class="form-label">Current Script</label>
              <select class="form-select" id="script-selector" onchange="loadScriptForEditing()">
                <option value="">Select a script...</option>
              </select>
            </div>
            <div class="mb-3">
              <textarea class="form-control font-monospace" id="script-editor" rows="15" style="white-space: pre; overflow-x: auto;"></textarea>
            </div>
            <button class="btn btn-primary" onclick="saveScript()">
              <i class="bi bi-save"></i> Save Changes
            </button>
            <button class="btn btn-success ms-2" onclick="loadAndRunScript()">
              <i class="bi bi-play-circle"></i> Load & Run
            </button>
          </div>
        </div>
      </div>
    </div>

    <!-- Settings Tab -->
    <div id="settings" class="tab-content" style="display:none;">
      <div class="card">
        <div class="card-header bg-primary text-white">
          <i class="bi bi-gear"></i> System Settings
        </div>
        <div class="card-body">
          <div class="mb-3">
            <h5><i class="bi bi-wifi"></i> WiFi Settings</h5>
            <label for="wifi-ssid" class="form-label">SSID</label>
            <input type="text" class="form-control" id="wifi-ssid">
            <label for="wifi-password" class="form-label">Password</label>
            <input type="password" class="form-control" id="wifi-password">
          </div>
          
          <div class="mb-3">
            <h5><i class="bi bi-plug"></i> Modbus Settings</h5>
            <label for="modbus-slave-id" class="form-label">Slave ID</label>
            <input type="number" class="form-control" id="modbus-slave-id" min="1" max="247">
            <label for="modbus-baudrate" class="form-label">Baud Rate</label>
            <select class="form-select" id="modbus-baudrate">
              <option value="9600">9600</option>
              <option value="19200">19200</option>
              <option value="38400">38400</option>
              <option value="57600">57600</option>
              <option value="115200">115200</option>
            </select>
          </div>
          
          <button class="btn btn-primary" onclick="saveSettings()">
            <i class="bi bi-save"></i> Save Settings
          </button>
          <button class="btn btn-danger ms-2" onclick="restartSystem()">
            <i class="bi bi-arrow-repeat"></i> Restart System
          </button>
        </div>
      </div>
    </div>
  </div>

  <script src="https://cdn.jsdelivr.net/npm/bootstrap@5.1.3/dist/js/bootstrap.bundle.min.js"></script>
  <script>
    // Tab management
    function showTab(tabId) {
      document.querySelectorAll('.tab-content').forEach(tab => {
        tab.style.display = 'none';
      });
      document.getElementById(tabId).style.display = 'block';
      
      // Update active nav link
      document.querySelectorAll('.nav-link').forEach(link => {
        link.classList.remove('active');
      });
      event.target.classList.add('active');
    }
    
    function showScriptTab(tabId) {
      document.querySelectorAll('.script-tab-content').forEach(tab => {
        tab.style.display = 'none';
      });
      document.getElementById(tabId + '-script').style.display = 'block';
      
      // Update active nav link
      document.querySelectorAll('#scriptTabs .nav-link').forEach(link => {
        link.classList.remove('active');
      });
      event.target.classList.add('active');
    }
    
    // Status updates
    function updateStatus() {
      fetch('/api/status')
        .then(response => response.json())
        .then(data => {
          document.getElementById('di1-status').textContent = data.di1 ? 'ON' : 'OFF';
          document.getElementById('di2-status').textContent = data.di2 ? 'ON' : 'OFF';
          document.getElementById('adc-value').textContent = data.adc;
          document.getElementById('do1-status').textContent = data.do1 ? 'ON' : 'OFF';
          document.getElementById('do2-status').textContent = data.do2 ? 'ON' : 'OFF';
          document.getElementById('do1-switch').checked = data.do1;
          document.getElementById('do2-pwm').value = data.do2;
          document.getElementById('pwm-value').textContent = data.do2;
          
          let scriptTypeText = 'None';
          if(data.scriptType === 1) scriptTypeText = 'Ladder Logic';
          else if(data.scriptType === 2) scriptTypeText = 'Mini-Python';
          else if(data.scriptType === 3) scriptTypeText = 'Lua';
          document.getElementById('script-type').textContent = scriptTypeText;
        })
        .catch(error => console.error('Status update error:', error));
    }
    
    // I/O Control
    function updateOutputs() {
      const do1 = document.getElementById('do1-switch').checked ? 1 : 0;
      const do2 = document.getElementById('do2-pwm').value;
      
      fetch('/api/io/write?do1=' + do1 + '&do2=' + do2)
        .then(response => response.text())
        .then(text => alert(text))
        .catch(err => alert('Error: ' + err));
    }
    
    // Script management
    function listScripts() {
      fetch('/api/scripts/list')
        .then(response => response.json())
        .then(data => {
          const tbody = document.querySelector('#script-list tbody');
          tbody.innerHTML = '';
          const selector = document.getElementById('script-selector');
          selector.innerHTML = '<option value="">Select a script...</option>';
          
          data.scripts.forEach(script => {
            // Add to table
            const row = document.createElement('tr');
            row.innerHTML = `
              <td>${script.name}</td>
              <td>
                <button class="btn btn-sm btn-primary" onclick="loadAndRunScript('${script.path}')">Load</button>
                <button class="btn btn-sm btn-danger" onclick="deleteScript('${script.path}')">Delete</button>
              </td>
            `;
            tbody.appendChild(row);
            
            // Add to selector
            const option = document.createElement('option');
            option.value = script.path;
            option.textContent = script.name;
            selector.appendChild(option);
          });
        })
        .catch(error => console.error('Error listing scripts:', error));
    }
    
    function loadScriptForEditing() {
      const path = document.getElementById('script-selector').value;
      if(!path) return;
      
      fetch('/api/script/get?path=' + encodeURIComponent(path))
        .then(response => response.text())
        .then(text => {
          document.getElementById('script-editor').value = text;
        })
        .catch(error => console.error('Error loading script:', error));
    }
    
    function saveScript() {
      const path = document.getElementById('script-selector').value;
      const content = document.getElementById('script-editor').value;
      
      fetch('/api/script/save', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'path=' + encodeURIComponent(path) + '&content=' + encodeURIComponent(content)
      })
      .then(response => response.text())
      .then(text => alert(text))
      .catch(error => console.error('Error saving script:', error));
    }
    
    function loadAndRunScript(path) {
      if(!path) {
        path = document.getElementById('script-selector').value;
        if(!path) return alert('Select a script first');
      }
      
      fetch('/api/script/load?path=' + encodeURIComponent(path))
        .then(response => response.text())
        .then(text => alert(text))
        .catch(error => console.error('Error loading script:', error));
    }
    
    function deleteScript(path) {
      if(!confirm('Delete ' + path + '?')) return;
      
      fetch('/api/script/delete?path=' + encodeURIComponent(path), { method: 'DELETE' })
        .then(response => response.text())
        .then(text => {
          alert(text);
          listScripts();
        })
        .catch(error => console.error('Error deleting script:', error));
    }
    
    function uploadScript() {
      const fileInput = document.getElementById('script-upload');
      if(!fileInput.files.length) return alert('Select a file');
      
      const formData = new FormData();
      formData.append('file', fileInput.files[0]);
      
      fetch('/api/script/upload', {
        method: 'POST',
        body: formData
      })
      .then(response => response.text())
      .then(text => {
        alert(text);
        listScripts();
      })
      .catch(error => console.error('Error uploading script:', error));
    }
    
    // Settings
    function loadSettings() {
      fetch('/api/settings/get')
        .then(response => response.json())
        .then(settings => {
          document.getElementById('wifi-ssid').value = settings.ssid || '';
          document.getElementById('modbus-slave-id').value = settings.slaveId || 1;
          document.getElementById('modbus-baudrate').value = settings.baudrate || 9600;
        })
        .catch(error => console.error('Error loading settings:', error));
    }
    
    function saveSettings() {
      const ssid = document.getElementById('wifi-ssid').value;
      const password = document.getElementById('wifi-password').value;
      const slaveId = document.getElementById('modbus-slave-id').value;
      const baudrate = document.getElementById('modbus-baudrate').value;
      
      fetch('/api/settings/save', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}&slaveId=${slaveId}&baudrate=${baudrate}`
      })
      .then(response => response.text())
      .then(text => alert(text))
      .catch(error => console.error('Error saving settings:', error));
    }
    
    function restartSystem() {
      if(confirm('Restart system?')) {
        fetch('/api/system/restart', { method: 'POST' })
          .then(() => alert('Restarting...'))
          .catch(error => console.error('Error restarting:', error));
      }
    }
    
    function logout() {
      fetch('/logout', { method: 'POST' })
        .then(() => window.location.reload())
        .catch(error => console.error('Error logging out:', error));
    }
    
    // Initialize
    document.addEventListener('DOMContentLoaded', function() {
      loadSettings();
      listScripts();
      updateStatus();
      setInterval(updateStatus, 1000);
      
      // Update PWM value display
      document.getElementById('do2-pwm').addEventListener('input', function() {
        document.getElementById('pwm-value').textContent = this.value;
      });
    });
  </script>
</body>
</html>
)=====";

  server.send(200, "text/html", html);
}

void handleStatus() {
  if(!checkAuth(server)) return;
  
  String json = "{";
  json += "\"di1\":" + String(readDigital(DI1_PIN)) + ",";
  json += "\"di2\":" + String(readDigital(DI2_PIN)) + ",";
  json += "\"adc\":" + String(readAnalog(ADC1_PIN)) + ",";
  json += "\"do1\":" + String(readDigital(DO1_PIN)) + ",";
  json += "\"do2\":" + String(readDigital(DO2_PIN)) + ",";
  json += "\"scriptType\":" + String(scriptType);
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleIOWrite() {
  if(!checkAuth(server)) return;
  
  if(server.hasArg("do1")) {
    writeDigital(DO1_PIN, server.arg("do1").toInt() != 0);
  }
  if(server.hasArg("do2")) {
    writeAnalog(DO2_PIN, server.arg("do2").toInt());
  }
  
  server.send(200, "text/plain", "Outputs updated");
}

void handleScriptList() {
  if(!checkAuth(server)) return;
  
  String json = "{\"scripts\":[";
  bool first = true;
  
  File root = SD.open("/");
  File file = root.openNextFile();
  while(file) {
    if(!file.isDirectory()) {
      if(!first) json += ",";
      first = false;
      json += "{\"name\":\"" + String(file.name()) + "\",";
      json += "\"path\":\"" + String(file.name()) + "\"}";
    }
    file = root.openNextFile();
  }
  
  json += "]}";
  server.send(200, "application/json", json);
}

void handleScriptGet() {
  if(!checkAuth(server)) return;
  
  if(!server.hasArg("path")) {
    server.send(400, "text/plain", "Missing path parameter");
    return;
  }
  
  String path = server.arg("path");
  if(!SD.exists(path)) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  
  File file = SD.open(path, FILE_READ);
  if(!file) {
    server.send(500, "text/plain", "Failed to open file");
    return;
  }
  
  String content = file.readString();
  file.close();
  server.send(200, "text/plain", content);
}

void handleScriptSave() {
  if(!checkAuth(server)) return;
  
  if(!server.hasArg("path") || !server.hasArg("content")) {
    server.send(400, "text/plain", "Missing parameters");
    return;
  }
  
  String path = server.arg("path");
  String content = server.arg("content");
  
  if(SD.exists(path)) {
    SD.remove(path);
  }
  
  File file = SD.open(path, FILE_WRITE);
  if(!file) {
    server.send(500, "text/plain", "Failed to create file");
    return;
  }
  
  file.print(content);
  file.close();
  server.send(200, "text/plain", "Script saved");
}

void handleScriptLoad() {
  if(!checkAuth(server)) return;
  
  if(!server.hasArg("path")) {
    server.send(400, "text/plain", "Missing path parameter");
    return;
  }
  
  String path = server.arg("path");
  if(loadScript(path)) {
    server.send(200, "text/plain", "Script loaded successfully");
  } else {
    server.send(500, "text/plain", "Failed to load script");
  }
}

void handleScriptDelete() {
  if(!checkAuth(server)) return;
  
  if(server.method() != HTTP_DELETE || !server.hasArg("path")) {
    server.send(400, "text/plain", "Invalid request");
    return;
  }
  
  String path = server.arg("path");
  if(!SD.exists(path)) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  
  if(SD.remove(path)) {
    server.send(200, "text/plain", "Script deleted");
  } else {
    server.send(500, "text/plain", "Failed to delete script");
  }
}

File uploadFile;

void handleScriptUpload() {
  if(!checkAuth(server)) return;
  
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/" + filename;
    
    if(SD.exists(filename)) {
      SD.remove(filename);
    }
    
    uploadFile = SD.open(filename, FILE_WRITE);
    if(!uploadFile) {
      server.send(500, "text/plain", "Failed to create file");
      return;
    }
  } else if(upload.status == UPLOAD_FILE_WRITE) {
    if(uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } else if(upload.status == UPLOAD_FILE_END) {
    if(uploadFile) {
      uploadFile.close();
    }
    server.send(200, "text/plain", "Upload complete");
  }
}

void handleSettingsGet() {
  if(!checkAuth(server)) return;
  
  String json = "{";
  json += "\"ssid\":\"" + config.wifiSSID + "\",";
  json += "\"slaveId\":" + String(config.modbusSlaveId) + ",";
  json += "\"baudrate\":" + String(config.modbusBaudRate);
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleSettingsSave() {
  if(!checkAuth(server)) return;
  
  if(!server.hasArg("ssid") || !server.hasArg("slaveId") || !server.hasArg("baudrate")) {
    server.send(400, "text/plain", "Missing parameters");
    return;
  }
  
  config.wifiSSID = server.arg("ssid");
  if(server.hasArg("password") && server.arg("password").length() > 0) {
    config.wifiPassword = server.arg("password");
  }
  config.modbusSlaveId = server.arg("slaveId").toInt();
  config.modbusBaudRate = server.arg("baudrate").toInt();
  
  saveConfig();
  server.send(200, "text/plain", "Settings saved");
}

void handleSystemRestart() {
  if(!checkAuth(server)) return;
  
  server.send(200, "text/plain", "System restarting...");
  delay(1000);
  ESP.restart();
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/io/write", HTTP_GET, handleIOWrite);
  server.on("/api/scripts/list", HTTP_GET, handleScriptList);
  server.on("/api/script/get", HTTP_GET, handleScriptGet);
  server.on("/api/script/save", HTTP_POST, handleScriptSave);
  server.on("/api/script/load", HTTP_GET, handleScriptLoad);
  server.on("/api/script/delete", HTTP_DELETE, handleScriptDelete);
  server.on("/api/script/upload", HTTP_POST, [](){
    if(!checkAuth(server)) return;
    server.send(200);
  }, handleScriptUpload);
  server.on("/api/settings/get", HTTP_GET, handleSettingsGet);
  server.on("/api/settings/save", HTTP_POST, handleSettingsSave);
  server.on("/api/system/restart", HTTP_POST, handleSystemRestart);
  
  server.begin();
}

// ========== Main Setup ==========
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("Starting ESP32 PLC...");
  
  // Initialize EEPROM and load config
  EEPROM.begin(512);
  loadConfig();
  
  // Initialize SD card
  if(!SD.begin(SD_CS_PIN)) {
    Serial.println("SD Card initialization failed!");
    while(1);
  }
  
  // Initialize I/O
  setupIO();
  
  // Initialize Modbus
  setupModbus();
  
  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifiSSID.c_str(), config.wifiPassword.c_str());
  
  Serial.print("Connecting to WiFi...");
  int timeout = 0;
  while(WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(DEFAULT_SSID, DEFAULT_PASSWORD);
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
  }
  
  // Setup web server
  setupWebServer();
  
  // Try to load default script
  if(!loadScript(config.defaultLogicPath)) {
    Serial.println("No default script found");
  }
  
  Serial.println("Setup complete");
}

// ========== Main Loop ==========
void loop() {
  static unsigned long lastUpdate = millis();
  
  // Handle web server
  server.handleClient();
  
  // Update Modbus
  updateModbus();
  
  // Execute script
  executeScript();
  
  // Maintain 10ms cycle time
  while(millis() - lastUpdate < 10) {
    delay(1);
  }
  lastUpdate = millis();
}

/*
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 PLC Control Panel</title>
  <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.1.3/dist/css/bootstrap.min.css" rel="stylesheet">
  <link href="https://cdn.jsdelivr.net/npm/bootstrap-icons@1.8.0/font/bootstrap-icons.css" rel="stylesheet">
  <style>
    body { padding-top: 20px; background-color: #f8f9fa; }
    .card { margin-bottom: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
    .status-card { background-color: #fff; border-left: 4px solid #0d6efd; }
    .digital-input { color: #dc3545; }
    .digital-output { color: #198754; }
    .analog-value { color: #6f42c1; }
    .navbar-brand { font-weight: bold; }
  </style>
</head>
<body>
  <div class="container">
    <nav class="navbar navbar-expand-lg navbar-dark bg-primary rounded mb-4">
      <div class="container-fluid">
        <a class="navbar-brand" href="#">
          <i class="bi bi-cpu"></i> ESP32 PLC
        </a>
        <button class="navbar-toggler" type="button" data-bs-toggle="collapse" data-bs-target="#navbarNav">
          <span class="navbar-toggler-icon"></span>
        </button>
        <div class="collapse navbar-collapse" id="navbarNav">
          <ul class="navbar-nav">
            <li class="nav-item">
              <a class="nav-link active" href="#" id="dashboard-tab">
                <i class="bi bi-speedometer2"></i> Dashboard
              </a>
            </li>
            <li class="nav-item">
              <a class="nav-link" href="#" id="io-tab">
                <i class="bi bi-toggle-on"></i> I/O Control
              </a>
            </li>
            <li class="nav-item">
              <a class="nav-link" href="#" id="scripts-tab">
                <i class="bi bi-file-code"></i> Scripts
              </a>
            </li>
            <li class="nav-item">
              <a class="nav-link" href="#" id="settings-tab">
                <i class="bi bi-gear"></i> Settings
              </a>
            </li>
          </ul>
          <ul class="navbar-nav ms-auto">
            <li class="nav-item dropdown">
              <a class="nav-link dropdown-toggle" href="#" id="userDropdown" role="button" data-bs-toggle="dropdown">
                <i class="bi bi-person-circle"></i> Admin
              </a>
              <ul class="dropdown-menu dropdown-menu-end">
                <li><a class="dropdown-item" href="#" id="logout-btn">Logout</a></li>
              </ul>
            </li>
          </ul>
        </div>
      </div>
    </nav>

    <!-- Dashboard Tab -->
    <div id="dashboard-content">
      <div class="row">
        <div class="col-md-6">
          <div class="card status-card">
            <div class="card-header bg-primary text-white">
              <i class="bi bi-input-cursor-text"></i> Input Status
            </div>
            <div class="card-body">
              <table class="table">
                <tr>
                  <td><i class="bi bi-circle-fill digital-input"></i> DI1</td>
                  <td id="di1-status">?</td>
                </tr>
                <tr>
                  <td><i class="bi bi-circle-fill digital-input"></i> DI2</td>
                  <td id="di2-status">?</td>
                </tr>
                <tr>
                  <td><i class="bi bi-speedometer2 analog-value"></i> ADC</td>
                  <td id="adc-value">?</td>
                </tr>
              </table>
            </div>
          </div>
        </div>
        <div class="col-md-6">
          <div class="card status-card">
            <div class="card-header bg-primary text-white">
              <i class="bi bi-output-cursor"></i> Output Status
            </div>
            <div class="card-body">
              <table class="table">
                <tr>
                  <td><i class="bi bi-circle-fill digital-output"></i> DO1</td>
                  <td id="do1-status">?</td>
                </tr>
                <tr>
                  <td><i class="bi bi-circle-fill digital-output"></i> DO2</td>
                  <td id="do2-status">?</td>
                </tr>
                <tr>
                  <td><i class="bi bi-file-code"></i> Current Script</td>
                  <td id="script-type">None</td>
                </tr>
              </table>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- I/O Control Tab -->
    <div id="io-content" style="display: none;">
      <div class="card">
        <div class="card-header bg-primary text-white">
          <i class="bi bi-toggle-on"></i> Digital Output Control
        </div>
        <div class="card-body">
          <div class="mb-3">
            <label class="form-label">DO1</label>
            <div class="form-check form-switch">
              <input class="form-check-input" type="checkbox" id="do1-switch">
              <label class="form-check-label" for="do1-switch">Toggle Output</label>
            </div>
          </div>
          <div class="mb-3">
            <label class="form-label">DO2 (PWM)</label>
            <input type="range" class="form-range" min="0" max="255" id="do2-pwm">
            <div class="text-center" id="pwm-value">0</div>
          </div>
          <button class="btn btn-primary" id="apply-outputs">
            <i class="bi bi-check-circle"></i> Apply Changes
          </button>
        </div>
      </div>
    </div>

    <!-- Scripts Tab -->
    <div id="scripts-content" style="display: none;">
      <div class="card">
        <div class="card-header bg-primary text-white">
          <i class="bi bi-file-earmark-code"></i> Script Management
        </div>
        <div class="card-body">
          <ul class="nav nav-tabs" id="scriptTabs" role="tablist">
            <li class="nav-item" role="presentation">
              <button class="nav-link active" id="upload-tab" data-bs-toggle="tab" data-bs-target="#upload-script" type="button">Upload</button>
            </li>
            <li class="nav-item" role="presentation">
              <button class="nav-link" id="manage-tab" data-bs-toggle="tab" data-bs-target="#manage-scripts" type="button">Manage</button>
            </li>
            <li class="nav-item" role="presentation">
              <button class="nav-link" id="editor-tab" data-bs-toggle="tab" data-bs-target="#script-editor" type="button">Editor</button>
            </li>
          </ul>
          <div class="tab-content p-3 border border-top-0 rounded-bottom">
            <div class="tab-pane fade show active" id="upload-script" role="tabpanel">
              <div class="mb-3">
                <label for="script-upload" class="form-label">Upload Script File</label>
                <input class="form-control" type="file" id="script-upload">
              </div>
              <button class="btn btn-primary" id="upload-btn">
                <i class="bi bi-upload"></i> Upload
              </button>
              <div class="alert alert-info mt-3">
                Supported formats: .txt (Ladder), .py (Mini-Python), .lua (Lua)
              </div>
            </div>
            <div class="tab-pane fade" id="manage-scripts" role="tabpanel">
              <button class="btn btn-primary mb-3" id="refresh-scripts">
                <i class="bi bi-arrow-clockwise"></i> Refresh List
              </button>
              <div class="table-responsive">
                <table class="table table-striped">
                  <thead>
                    <tr>
                      <th>Filename</th>
                      <th>Size</th>
                      <th>Actions</th>
                    </tr>
                  </thead>
                  <tbody id="script-list">
                    <!-- Scripts will be loaded here -->
                  </tbody>
                </table>
              </div>
            </div>
            <div class="tab-pane fade" id="script-editor" role="tabpanel">
              <div class="mb-3">
                <label class="form-label">Current Script</label>
                <select class="form-select" id="script-selector">
                  <option value="">Select a script...</option>
                </select>
              </div>
              <div class="mb-3">
                <textarea class="form-control font-monospace" id="script-editor-text" rows="15" style="white-space: pre; overflow-x: auto;"></textarea>
              </div>
              <button class="btn btn-primary" id="save-script">
                <i class="bi bi-save"></i> Save Changes
              </button>
              <button class="btn btn-success ms-2" id="load-script">
                <i class="bi bi-play-circle"></i> Load & Run
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Settings Tab -->
    <div id="settings-content" style="display: none;">
      <div class="card">
        <div class="card-header bg-primary text-white">
          <i class="bi bi-gear"></i> System Settings
        </div>
        <div class="card-body">
          <form id="wifi-settings">
            <h5><i class="bi bi-wifi"></i> WiFi Settings</h5>
            <div class="mb-3">
              <label for="wifi-ssid" class="form-label">SSID</label>
              <input type="text" class="form-control" id="wifi-ssid" required>
            </div>
            <div class="mb-3">
              <label for="wifi-password" class="form-label">Password</label>
              <input type="password" class="form-control" id="wifi-password">
            </div>
            
            <h5 class="mt-4"><i class="bi bi-plug"></i> Modbus Settings</h5>
            <div class="mb-3">
              <label for="modbus-slave-id" class="form-label">Slave ID</label>
              <input type="number" class="form-control" id="modbus-slave-id" min="1" max="247" required>
            </div>
            <div class="mb-3">
              <label for="modbus-baudrate" class="form-label">Baud Rate</label>
              <select class="form-select" id="modbus-baudrate" required>
                <option value="9600">9600</option>
                <option value="19200">19200</option>
                <option value="38400">38400</option>
                <option value="57600">57600</option>
                <option value="115200">115200</option>
              </select>
            </div>
            
            <button type="submit" class="btn btn-primary">
              <i class="bi bi-save"></i> Save Settings
            </button>
            <button type="button" class="btn btn-danger ms-2" id="restart-btn">
              <i class="bi bi-arrow-repeat"></i> Restart System
            </button>
          </form>
        </div>
      </div>
    </div>
  </div>

  <script src="https://cdn.jsdelivr.net/npm/bootstrap@5.1.3/dist/js/bootstrap.bundle.min.js"></script>
  <script>
    // Global variables
    let statusInterval;
    let currentScriptContent = '';
    
    // DOM Ready
    document.addEventListener('DOMContentLoaded', function() {
      // Tab navigation
      document.getElementById('dashboard-tab').addEventListener('click', showTab.bind(null, 'dashboard'));
      document.getElementById('io-tab').addEventListener('click', showTab.bind(null, 'io'));
      document.getElementById('scripts-tab').addEventListener('click', showTab.bind(null, 'scripts'));
      document.getElementById('settings-tab').addEventListener('click', showTab.bind(null, 'settings'));
      
      // I/O Control
      document.getElementById('apply-outputs').addEventListener('click', applyOutputChanges);
      document.getElementById('do2-pwm').addEventListener('input', function() {
        document.getElementById('pwm-value').textContent = this.value;
      });
      
      // Script Management
      document.getElementById('upload-btn').addEventListener('click', uploadScript);
      document.getElementById('refresh-scripts').addEventListener('click', loadScriptList);
      document.getElementById('script-selector').addEventListener('change', loadScriptForEditing);
      document.getElementById('save-script').addEventListener('click', saveScriptChanges);
      document.getElementById('load-script').addEventListener('click', loadAndRunScript);
      
      // Settings
      document.getElementById('wifi-settings').addEventListener('submit', saveSettings);
      document.getElementById('restart-btn').addEventListener('click', restartSystem);
      
      // Logout
      document.getElementById('logout-btn').addEventListener('click', logout);
      
      // Initial setup
      loadSettings();
      loadScriptList();
      startStatusUpdates();
    });
    
    function showTab(tabName) {
      document.querySelectorAll('[id$="-content"]').forEach(el => {
        el.style.display = 'none';
      });
      document.getElementById(tabName + '-content').style.display = 'block';
      
      // Update active tab styling
      document.querySelectorAll('.nav-link').forEach(el => {
        el.classList.remove('active');
      });
      document.getElementById(tabName + '-tab').classList.add('active');
    }
    
    function startStatusUpdates() {
      fetchStatus();
      statusInterval = setInterval(fetchStatus, 1000);
    }
    
    function fetchStatus() {
      fetch('/api/status')
        .then(response => response.json())
        .then(data => {
          document.getElementById('di1-status').textContent = data.di1 ? 'ON' : 'OFF';
          document.getElementById('di2-status').textContent = data.di2 ? 'ON' : 'OFF';
          document.getElementById('adc-value').textContent = data.adc;
          document.getElementById('do1-status').textContent = data.do1 ? 'ON' : 'OFF';
          document.getElementById('do2-status').textContent = data.do2 ? 'ON' : 'OFF';
          
          // Update switch to match current state
          document.getElementById('do1-switch').checked = data.do1;
          
          // Update script type display
          let scriptTypeText = 'None';
          if(data.scriptType === 1) scriptTypeText = 'Ladder Logic';
          else if(data.scriptType === 2) scriptTypeText = 'Mini-Python';
          else if(data.scriptType === 3) scriptTypeText = 'Lua';
          document.getElementById('script-type').textContent = scriptTypeText;
        })
        .catch(error => console.error('Error fetching status:', error));
    }
    
    function applyOutputChanges() {
      const do1State = document.getElementById('do1-switch').checked;
      const do2Value = document.getElementById('do2-pwm').value;
      
      fetch('/api/io/digital/write', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/x-www-form-urlencoded',
        },
        body: `pin=${DO1_PIN}&value=${do1State ? 1 : 0}`
      })
      .then(response => {
        if(response.ok) {
          return fetch('/api/io/digital/write', {
            method: 'POST',
            headers: {
              'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: `pin=${DO2_PIN}&value=${do2Value}`
          });
        }
        throw new Error('Failed to set outputs');
      })
      .then(response => {
        if(response.ok) {
          alert('Outputs updated successfully');
        } else {
          throw new Error('Failed to set outputs');
        }
      })
      .catch(error => {
        console.error('Error:', error);
        alert('Failed to update outputs');
      });
    }
    
    function uploadScript() {
      const fileInput = document.getElementById('script-upload');
      if(fileInput.files.length === 0) {
        alert('Please select a file to upload');
        return;
      }
      
      const formData = new FormData();
      formData.append('file', fileInput.files[0]);
      
      fetch('/api/upload', {
        method: 'POST',
        body: formData
      })
      .then(response => {
        if(response.ok) {
          alert('Script uploaded successfully');
          loadScriptList();
          fileInput.value = '';
        } else {
          throw new Error('Upload failed');
        }
      })
      .catch(error => {
        console.error('Error:', error);
        alert('Failed to upload script');
      });
    }
    
    function loadScriptList() {
      fetch('/api/scripts/list')
        .then(response => response.json())
        .then(data => {
          const scriptList = document.getElementById('script-list');
          scriptList.innerHTML = '';
          
          const scriptSelector = document.getElementById('script-selector');
          scriptSelector.innerHTML = '<option value="">Select a script...</option>';
          
          data.scripts.forEach(script => {
            // Add to table
            const row = document.createElement('tr');
            row.innerHTML = `
              <td>${script}</td>
              <td>${script.size} bytes</td>
              <td>
                <button class="btn btn-sm btn-primary load-btn" data-script="${script}">Load</button>
                <button class="btn btn-sm btn-danger delete-btn" data-script="${script}">Delete</button>
              </td>
            `;
            scriptList.appendChild(row);
            
            // Add to selector
            const option = document.createElement('option');
            option.value = script;
            option.textContent = script;
            scriptSelector.appendChild(option);
          });
          
          // Add event listeners to new buttons
          document.querySelectorAll('.load-btn').forEach(btn => {
            btn.addEventListener('click', function() {
              loadAndRunScript(this.dataset.script);
            });
          });
          
          document.querySelectorAll('.delete-btn').forEach(btn => {
            btn.addEventListener('click', function() {
              if(confirm(`Are you sure you want to delete ${this.dataset.script}?`)) {
                deleteScript(this.dataset.script);
              }
            });
          });
        })
        .catch(error => console.error('Error loading script list:', error));
    }
    
    function loadScriptForEditing() {
      const scriptPath = this.value;
      if(!scriptPath) return;
      
      fetch(`/api/script/current?path=${encodeURIComponent(scriptPath)}`)
        .then(response => response.text())
        .then(text => {
          currentScriptContent = text;
          document.getElementById('script-editor-text').value = text;
        })
        .catch(error => console.error('Error loading script:', error));
    }
    
    function saveScriptChanges() {
      const scriptPath = document.getElementById('script-selector').value;
      if(!scriptPath) {
        alert('Please select a script to save');
        return;
      }
      
      const newContent = document.getElementById('script-editor-text').value;
      
      fetch('/api/script/save', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/x-www-form-urlencoded',
        },
        body: `path=${encodeURIComponent(scriptPath)}&content=${encodeURIComponent(newContent)}`
      })
      .then(response => {
        if(response.ok) {
          alert('Script saved successfully');
          currentScriptContent = newContent;
        } else {
          throw new Error('Save failed');
        }
      })
      .catch(error => {
        console.error('Error:', error);
        alert('Failed to save script');
      });
    }
    
    function loadAndRunScript(scriptPath) {
      if(!scriptPath) {
        scriptPath = document.getElementById('script-selector').value;
        if(!scriptPath) {
          alert('Please select a script to load');
          return;
        }
      }
      
      fetch('/api/script/load', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/x-www-form-urlencoded',
        },
        body: `path=${encodeURIComponent(scriptPath)}`
      })
      .then(response => {
        if(response.ok) {
          return response.text();
        }
        throw new Error('Load failed');
      })
      .then(text => {
        alert(text);
        fetchStatus(); // Refresh status to show new script type
      })
      .catch(error => {
        console.error('Error:', error);
        alert('Failed to load script');
      });
    }
    
    function deleteScript(scriptPath) {
      fetch('/api/script/delete', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/x-www-form-urlencoded',
        },
        body: `path=${encodeURIComponent(scriptPath)}`
      })
      .then(response => {
        if(response.ok) {
          alert('Script deleted successfully');
          loadScriptList();
        } else {
          throw new Error('Delete failed');
        }
      })
      .catch(error => {
        console.error('Error:', error);
        alert('Failed to delete script');
      });
    }
    
    function loadSettings() {
      fetch('/api/system/config')
        .then(response => response.json())
        .then(data => {
          document.getElementById('wifi-ssid').value = data.wifiSSID || '';
          document.getElementById('modbus-slave-id').value = data.modbusSlaveId || 1;
          document.getElementById('modbus-baudrate').value = data.modbusBaudRate || 9600;
        })
        .catch(error => console.error('Error loading settings:', error));
    }
    
    function saveSettings(e) {
      e.preventDefault();
      
      const ssid = document.getElementById('wifi-ssid').value;
      const password = document.getElementById('wifi-password').value;
      const slaveId = document.getElementById('modbus-slave-id').value;
      const baudrate = document.getElementById('modbus-baudrate').value;
      
      fetch('/api/system/config', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/x-www-form-urlencoded',
        },
        body: `wifiSSID=${encodeURIComponent(ssid)}&wifiPassword=${encodeURIComponent(password)}&modbusSlaveId=${slaveId}&modbusBaudRate=${baudrate}`
      })
      .then(response => {
        if(response.ok) {
          alert('Settings saved successfully');
        } else {
          throw new Error('Save failed');
        }
      })
      .catch(error => {
        console.error('Error:', error);
        alert('Failed to save settings');
      });
    }
    
    function restartSystem() {
      if(confirm('Are you sure you want to restart the system?')) {
        fetch('/api/system/restart', {
          method: 'POST'
        })
        .catch(error => console.error('Error restarting:', error));
      }
    }
    
    function logout() {
      // In a real implementation, this would invalidate the session
      window.location.reload();
    }
  </script>
</body>
</html>
*/
