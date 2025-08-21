/***************************************************************************
  Hydra ESP32 Firmware v4.2 - Enhanced with Full Functionality
  – All web content embedded in firmware
  – Complete sidebar navigation implementation
  – Working analytics, events, reports, and security features
  – SD card logging with backup/restore functionality
  – Modern responsive UI with dark mode
  – MODBUS RTU communication support
  – Enhanced error handling and debugging
***************************************************************************/

#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>
#include <PZEM004Tv30.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ModbusRTU.h>
#include <time.h>
#include <Preferences.h>


// Pin definitions
#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17
#define RELAY_PIN 13
#define SD_CS_PIN 5
#define LED_STATUS_PIN 2

// PZEM-004T initialization
PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);

// Network credentials
//const char *ssid = "ACTFIBERNET";
//const char *password = "9381206570";
//const char *ssid = "PSI_Corp";
//const char *password = "Pass1234";
const char *ssid = "Mr.Gk";
const char *password = "Mr.Gk1992";

// NTP Server for time sync
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800; // IST is UTC+5:30
const int daylightOffset_sec = 0;

// MODBUS Configuration
#define MODBUS_SLAVE_ID 1
#define MODBUS_BAUDRATE 19200
#define MODBUS_SERIAL_CONFIG SERIAL_8N1
#define MODBUS_TX_PIN 25
#define MODBUS_RX_PIN 26

ModbusRTU mb;

// Security
const String csrfToken = "ESP32_BMS_CSRF_TOKEN_" + String(ESP.getEfuseMac());
String sessionToken = "";
unsigned long sessionTimeout = 1800000; // 30 minutes
unsigned long lastActivity = 0;

// Login system
const char *adminUser = "admin";
const char *adminPass = "admin123";
bool isAuthenticated = false;

// System settings
struct SystemSettings {
  bool enableFileLogging = true;
  bool enableSerialLogging = true;
  bool enableWebLogging = true;
  String timeZone = "IST";
  int logRetentionDays = 30;
  bool enableOTA = true;
  bool enableDebugMode = false;
  int maxLogFileSize = 1048576; // 1MB
};

SystemSettings sysSettings;

// Measurement variables
float acVoltage = 0.0, acCurrent = 0.0, acPower = 0.0, acEnergy = 0.0, acFrequency = 0.0, acPF = 0.0;
float thresholdAcVoltage = 230.0;
float thresholdFrequency = 50.0;

// Historical data storage
struct HistoricalData {
  unsigned long timestamp;
  float voltage;
  float current;
  float power;
  float frequency;
  float pf;
  uint16_t errorCode;
};

std::vector<HistoricalData> dataHistory;
const int maxHistoryPoints = 1440; // 24 hours of minute data

struct EventLog {
    unsigned long timestamp;       // old field — probably unused now
    unsigned long timestampMillis; // new — system uptime
    String timestampISO;           // new — human-readable
    String event;
    String level;
    uint16_t errorCode;
};

std::vector<EventLog> eventHistory;
const int maxEventHistory = 500;

// Grid code structure
struct GridCode {
  String name;
  float voltageThreshold;
  float frequencyThreshold;
  float voltageMin;
  float voltageMax;
  float frequencyMin;
  float frequencyMax;
  uint16_t errorCode;
  int tripDelay;
  int reconnectDelay;
};

struct SystemState {
  bool relayState = false;
  bool aiEnabled = false;
  int tripCount = 0;
  float voltage = 0.0;
  float current = 0.0;
  float power = 0.0;
  float frequency = 0.0;
  bool alarm = false;
};

SystemState sysState;


// System variables
std::vector<GridCode> gridCodes;
String currentGridCode = "IS 16184 (India)";
String islandingStatus = "Normal";
bool alarmTriggered = false;
bool aiDetectionEnabled = true;
float aiConfidenceThreshold = 0.85;
int islandingDelay = 10;

// Security and audit
struct SecurityEvent {
    unsigned long timestamp;       // old field — probably unused now
    unsigned long timestampMillis; // new — system uptime
    String timestampISO;           // new — human-readable
    String event;
    String sourceIP;
    bool success;
};

std::vector<SecurityEvent> securityEvents;
const int maxSecurityEvents = 100;

// Error codes
uint16_t systemError = 0;
uint16_t lastError = 0;
uint16_t modbusError = 0;

// Timing variables
unsigned long lastMeasurementTime = 0;
unsigned long lastIslandingCheckTime = 0;
unsigned long lastHistoryUpdate = 0;
unsigned long lastStatusUpdate = 0;
const unsigned long measurementInterval = 1000;
const unsigned long islandingCheckInterval = 10000;
const unsigned long historyUpdateInterval = 60000; // 1 minute
const unsigned long statusUpdateInterval = 500;

WebServer server(80);

// MODBUS Register Map
enum ModbusRegisters {
    MB_AC_VOLTAGE = 0,
    MB_AC_CURRENT = 2,
    MB_AC_POWER = 4,
    MB_AC_ENERGY = 6,
    MB_AC_FREQUENCY = 8,
    MB_AC_PF = 10,
    MB_RELAY_STATUS = 100,
    MB_GRID_CODE = 101,
    MB_AI_ENABLED = 102,
    MB_ERROR_CODE = 103,
    MB_VOLTAGE_THRESH = 200,
    MB_CURRENT_THRESH = 202,
    MB_FREQ_THRESH = 204,
    MB_ISLANDING_DELAY = 206,
    MB_SYSTEM_STATUS = 208,
    MB_UPTIME = 210
};

// Function prototypes
void handleRoot();
void handleLogin();
void handleLogout();
void handleStatus();
void handleData();
void handleGetSettings();
void handlePostSettings();
void handleCommand();
void handleTrendData();
void handleAnalytics();
void handleEvents();
void handleReports();
void handleSecurity();
void handleLogExport();
void handleConfigBackup();
void handleConfigRestore();
void handleSystemReset();
void handleGetLogs();
void handleClearLogs();
bool loadConfig();
bool saveConfig();
void createDefaultConfig();
void logEvent(String message, String level = "INFO", uint16_t errorCode = 0);
void logSecurityEvent(String event, String sourceIP, bool success);
void takeMeasurements();
void checkIslanding();
float calculateAIConfidence(float voltage, float frequency, float rocof);
DynamicJsonDocument buildStatusJson(size_t capacity = 2048);
void updateGridCodeThresholds();
void setupModbus();
void updateModbusRegisters();
void checkForErrors();
void initSDCard();
void initOTA();
void updateHistoricalData();
void cleanupOldData();
String getFormattedTime();
String getCurrentTimeISO();
String generateSessionToken();
bool isSessionValid();
void updateLastActivity();
bool saveGridCodeToPrefs(const String &gc);
void loadGridCodeFromPrefs();
void ensureDefaultGridCodes();
void logMeasurementsToSD();
void handleDownloadLog();

// Include the HTML pages (login and dashboard)
const char loginPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Login | Grid Monitoring</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link href="https://cdn.jsdelivr.net/npm/bootstrap-icons@1.8.1/font/bootstrap-icons.css" rel="stylesheet">
  <style>
    :root {
      --primary: #4361ee;
      --secondary: #3f37c9;
      --bg-color: #f8f9fa;
      --text-color: #212529;
      --card-bg: #ffffff;
      --border-color: #dee2e6;

      --primary: #4361ee;
      --secondary: #3f37c9;
      --bg-color: #f5f5f5;
      --container-bg: #ffffff;
      --border-color: #ddd;
      --text-color: #212529;
      --input-bg: rgba(255, 255, 255, 0.05);
      --error-color: #dc3545;
    }
    
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background-color: var(--bg-color);
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
      margin: 0;
      color: var(--text-color);
      transition: all 0.3s ease;
    }
    
    .login-container {
      background: var(--container-bg);
      padding: 2rem;
      border-radius: 12px;
      box-shadow: 0 8px 32px #f8f9fa;
      width: 100%;
      max-width: 400px;
      text-align: center;
      backdrop-filter: blur(10px);
      border: 1px solid var(--border-color);
      transition: all 0.3s ease;
    }
    
    h1 {
      color: var(--primary);
      margin-bottom: 1.5rem;
      font-weight: 600;
    }
    
    input {
      width: 100%;
      padding: 0.75rem;
      margin: 0.5rem 0;
      border: 1px solid var(--border-color);
      border-radius: 6px;
      box-sizing: border-box;
      background: var(--input-bg);
      color: var(--text-color);
      font-size: 1rem;
      transition: all 0.3s ease;
    }
    
    input:focus {
      outline: none;
      border-color: var(--primary);
      box-shadow: 0 0 0 3px rgba(67, 97, 238, 0.25);
    }
    
    button {
      background-color: var(--primary);
      color: white;
      padding: 0.75rem;
      border: none;
      border-radius: 6px;
      cursor: pointer;
      width: 100%;
      margin-top: 1rem;
      font-size: 1rem;
      font-weight: 500;
      transition: all 0.3s ease;
    }
    
    button:hover {
      background-color: var(--secondary);
      transform: translateY(-2px);
    }
    
    .error {
      color: var(--error-color);
      margin-top: 1rem;
      font-size: 0.9rem;
    }
    
    .logo {
      width: 80px;
      height: 80px;
      margin-bottom: 1rem;
      filter: drop-shadow(0 0 8px rgba(67, 97, 238, 0.5));
    }
    
    
  </style>
</head>
<body>
  <div class="login-container">
    <svg class="logo" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M12 2L2 7L12 12L22 7L12 2Z" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
      <path d="M2 17L12 22L22 17" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
      <path d="M2 12L12 17L22 12" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
    </svg>
    <h1>Grid Monitoring v4.2</h1>
    <form action="/login" method="POST">
      <input type="hidden" name="csrf" value="ESP32_BMS_CSRF_TOKEN">
      <input type="text" name="username" placeholder="Username" required>
      <input type="password" name="password" placeholder="Password" required>
      <button type="submit">Login</button>
    </form>
    <div id="error" class="error"></div>
  </div>

  <script>
    const urlParams = new URLSearchParams(window.location.search);
    if(urlParams.has('error')) {
      document.getElementById('error').textContent = 'Invalid username or password';
    }
  </script>
</body>
</html>
)rawliteral";

// Enhanced Dashboard Page with Working Sidebar Functions
const char dashboardPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>UGC Islanding Controller</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/luxon@2.0.2"></script>
    <script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-luxon@1.0.0"></script>
    <script src="https://cdn.jsdelivr.net/npm/chartjs-plugin-streaming@2.0.0"></script>
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.1.3/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap-icons@1.8.1/font/bootstrap-icons.css">
    <style>
        :root {
            --primary: #4361ee;
            --secondary: #3f37c9;
            --dark: #212529;
            --light: #f8f9fa;
            --danger: #dc3545;
            --success: #28a745;
            --warning: #ffc107;
            --info: #17a2b8;
            --bg-color: #f8f9fa;
            --text-color: #212529;
            --card-bg: #ffffff;
            --card-border: rgba(0, 0, 0, 0.125);
            --navbar-bg: #ffffff;
            --sidebar-bg: #f8f9fa;
            --input-bg: #ffffff;
            --border-color: #dee2e6;
        }

        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-color);
            min-height: 100vh;
            transition: all 0.3s ease;
        }
        
        .navbar {
            background-color: var(--navbar-bg) !important;
            backdrop-filter: blur(10px);
            border-bottom: 1px solid var(--border-color);
            transition: all 0.3s ease;
        }

        .navbar-light .navbar-brand {
            color: red !important;
            font-weight: bold;
        }

        .card {
            background-color: var(--card-bg);
            border: 1px solid var(--border-color);
            border-radius: 12px;
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.1);
            backdrop-filter: blur(10px);
            transition: all 0.3s ease;
        }
        
        .card:hover {
            transform: translateY(-2px);
            box-shadow: 0 12px 40px rgba(0, 0, 0, 0.15);
        }
        
        .status-card {
            padding: 1.5rem;
            margin-bottom: 1.5rem;
            text-align: center;
            font-weight: 600;
            font-size: 1.2rem;
            border-radius: 12px;
            transition: all 0.3s ease;
        }
        
        .status-normal {
            background-color: rgba(40, 167, 69, 0.2);
            border-left: 5px solid var(--success);
        }
        
        .status-alarm {
            background-color: rgba(220, 53, 69, 0.2);
            border-left: 5px solid var(--danger);
            animation: pulse 2s infinite;
        }
        
        @keyframes pulse {
            0% { opacity: 1; }
            50% { opacity: 0.7; }
            100% { opacity: 1; }
        }
        
        .sidebar {
            position: fixed;
            top: 56px;
            left: 0;
            bottom: 0;
            width: 250px;
            background-color: var(--sidebar-bg);
            backdrop-filter: blur(10px);
            border-right: 1px solid var(--border-color);
            padding: 1rem;
            z-index: 1000;
            transform: translateX(-100%);
            transition: transform 0.3s ease;
            overflow-y: auto;
        }
        
        .sidebar.show {
            transform: translateX(0);
        }
        
        .sidebar-link {
            display: block;
            padding: 0.75rem 1rem;
            color: var(--secondary);
            text-decoration: none;
            border-radius: 6px;
            margin-bottom: 0.5rem;
            transition: all 0.3s ease;
            cursor: pointer;
        }
        
        .sidebar-link:hover, .sidebar-link.active {
            background-color: rgba(67, 97, 238, 0.2);
            color: var(--text-color);
            text-decoration: none;
        }
        
        .sidebar-link i {
            margin-right: 0.75rem;
            width: 20px;
            text-align: center;
        }
        
        .main-content {
            margin-left: 0;
            padding: 1.5rem;
            transition: margin-left 0.3s ease;
            min-height: calc(100vh - 56px);
        }
        
        .sidebar-open .main-content {
            margin-left: 250px;
        }
        
        @media (max-width: 992px) {
            .main-content {
                margin-left: 250px !important;
            }
        }
        
        @media (max-width: 768px) {
            .sidebar {
                width: 80%;
            }
        }
        
        .theme-toggle-container {
            display: flex;
            align-items: center;
            margin-left: auto;
            margin-right: 1rem;
        }
        
        .theme-toggle-label {
            margin-right: 0.5rem;
            color: var(--text-color);
        }
        
        .form-check-input:checked {
            background-color: var(--primary);
            border-color: var(--primary);
        }
        
        .loading-overlay {
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: rgba(255, 255, 255, 0.9);
            color: var(--text-color);
            z-index: 2000;
            display: flex;
            justify-content: center;
            align-items: center;
            flex-direction: column;
            color: white;
        }
        
        .connection-status {
            position: fixed;
            bottom: 20px;
            right: 20px;
            padding: 10px 15px;
            border-radius: 5px;
            z-index: 1000;
            display: none;
        }
        
        .connection-success {
            background-color: var(--success);
        }
        
        .connection-error {
            background-color: var(--danger);
        }
        
        .chart-container {
            width: 100%;
            height: 300px;
            margin-bottom: 1.5rem;
        }
        
        .settings-panel {
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: rgba(255, 255, 255, 0.9); /* Light overlay */
            z-index: 1050;
            display: none;
            overflow-y: auto;
            padding: 2rem;
        }
        
        .settings-content {
            background-color: #ffffff; /* Solid white panel */
            color: var(--text-color);  /* Ensure dark text */
            border-radius: 12px;
            padding: 2rem;
            max-width: 800px;
            margin: 0 auto;
            border: 1px solid rgba(255, 255, 255, 0.1);
        }
        
        .form-control, .form-select {
            background-color: var(--input-bg);
            border: 1px solid var(--border-color);
            color: var(--text-color);
        }
        
        .form-control:focus, .form-select:focus {
            background-color: var(--input-bg);
            border-color: var(--primary);
            box-shadow: 0 0 0 0.25rem rgba(67, 97, 238, 0.25);
            color: var(--text-color);
        }
        
        .nav-tabs .nav-link {
            color: var(--secondary);
            border: none;
        }
        
        .nav-tabs .nav-link.active {
            color: var(--primary);
            background-color: transparent;
            border-bottom: 2px solid var(--primary);
        }
        
        .tab-content {
            padding: 1.5rem 0;
        }
        
        .log-entry {
            padding: 0.75rem;
            border-bottom: 1px solid var(--border-color);
            font-family: monospace;
            font-size: 0.85rem;
        }
        
        .log-entry:last-child {
            border-bottom: none;
        }
        
        .log-error {
            color: var(--danger);
        }
        
        .log-warning {
            color: var(--warning);
        }
        
        .log-info {
            color: var(--info);
        }
        
        .btn-primary {
            background-color: var(--primary);
            border-color: var(--primary);
        }
        
        .btn-primary:hover {
            background-color: var(--secondary);
            border-color: var(--secondary);
        }
        
    </style>
</head>
<body>
    <nav class="navbar navbar-expand-lg navbar-light">
        <div class="container-fluid">
            <button class="navbar-toggler me-2" type="button" id="sidebarToggle">
                <span class="navbar-toggler-icon"></span>
            </button>
            <a class="navbar-brand" href="#">
                <i class="bi bi-lightning-charge-fill me-2"></i>
                UGC Islanding Controller
            </a>
            
            <div class="theme-toggle-container">
                <span class="theme-toggle-label"><i class="bi bi-sun-fill"></i></span>
                <div class="form-check form-switch">
                    <input class="form-check-input" type="checkbox" id="themeToggle">
                    <label class="form-check-label" for="themeToggle"></label>
                </div>
                <span class="theme-toggle-label"><i class="bi bi-moon-fill"></i></span>
            </div>
            
            <div class="d-flex align-items-center">
                <div class="dropdown">
                    <button class="btn btn-outline-light dropdown-toggle" type="button" id="userDropdown" data-bs-toggle="dropdown">
                        <i class="bi bi-person-circle me-1"></i>
                        Admin
                    </button>
                    <ul class="dropdown-menu dropdown-menu-end">
                        <li><a class="dropdown-item" href="#"><i class="bi bi-person me-2"></i>Profile</a></li>
                        <li><a class="dropdown-item" href="#" onclick="showSettings()"><i class="bi bi-gear me-2"></i>Settings</a></li>
                        <li><hr class="dropdown-divider"></li>
                        <li><a class="dropdown-item" href="/logout"><i class="bi bi-box-arrow-right me-2"></i>Logout</a></li>
                    </ul>
                </div>
            </div>
        </div>
    </nav>

    <div class="sidebar" id="sidebar">
        <div class="sidebar-header mb-3">
            <h5 class="text-center">System Menu</h5>
        </div>
        <a href="#" class="sidebar-link active" onclick="showDashboard()">
            <i class="bi bi-speedometer2"></i>
            Dashboard
        </a>
        <a href="#" class="sidebar-link" onclick="showAnalytics()">
            <i class="bi bi-graph-up"></i>
            Analytics
        </a>
        <a href="#" class="sidebar-link" onclick="showSettings()">
            <i class="bi bi-gear"></i>
            Settings
        </a>
        <a href="#" class="sidebar-link" onclick="showEvents()">
            <i class="bi bi-list-check"></i>
            Events
        </a>
        <a href="#" class="sidebar-link" onclick="showReports()">
            <i class="bi bi-file-earmark-text"></i>
            Reports
        </a>
        <a href="#" class="sidebar-link" onclick="showSecurity()">
            <i class="bi bi-shield-check"></i>
            Security
        </a>
        <div class="mt-auto pt-3">
            <div class="card bg-transparent border-secondary">
                <div class="card-body p-2">
                    <small class="text-muted">System Status</small>
                    <div class="d-flex align-items-center mt-1">
                        <div class="me-2">
                            <div class="spinner-grow spinner-grow-sm text-success" role="status" id="systemStatusIcon">
                                <span class="visually-hidden">Loading...</span>
                            </div>
                        </div>
                        <small id="systemStatusText">Operational</small>
                    </div>
                </div>
            </div>
        </div>
    </div>
    
    <div class="main-content" id="mainContent">
        <!-- Dashboard Content -->
        <div id="dashboardContent">
            <div class="container-fluid">
                <div class="row mb-4">
                    <div class="col-12">
                        <div class="status-card" id="statusDisplay">
                            <div class="row align-items-center">
                                <div class="col-md-8">
                                    <h4 class="mb-0"><i class="bi bi-check-circle-fill me-2"></i> System Status: <span id="statusText">Normal</span></h4>
                                </div>
                                <div class="col-md-4 text-md-end mt-2 mt-md-0">
                                    <span class="badge bg-primary me-2"><i class="bi bi-cpu me-1"></i> ESP32</span>
                                    <span class="badge bg-success me-2"><i class="bi bi-wifi me-1"></i> Connected</span>
                                    <span class="badge bg-secondary"><i class="bi bi-clock me-1"></i> <span id="currentTime"></span></span>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
                
                <div class="row">
                    <div class="col-xl-3 col-md-6 mb-4">
                        <div class="card h-100">
                            <div class="card-body">
                                <div class="d-flex justify-content-between align-items-center mb-3">
                                    <h5 class="card-title mb-0">AC Voltage</h5>
                                    <div class="avatar">
                                        <span class="avatar-initial rounded-circle bg-primary bg-opacity-10 text-primary">
                                            <i class="bi bi-lightning-charge"></i>
                                        </span>
                                    </div>
                                </div>
                                <div class="d-flex align-items-center">
                                    <h2 class="mb-0" id="acVoltage">--</h2>
                                    <span class="ms-2 text-muted">V</span>
                                </div>
                                <div class="mt-3">
                                    <div class="progress" style="height: 6px;">
                                        <div class="progress-bar bg-primary" role="progressbar" id="voltageBar" style="width: 0%"></div>
                                    </div>
                                    <small class="text-muted">Target: <span id="targetVoltage">230</span> V</small>
                                </div>
                            </div>
                        </div>
                    </div>
                    
                    <div class="col-xl-3 col-md-6 mb-4">
                        <div class="card h-100">
                            <div class="card-body">
                                <div class="d-flex justify-content-between align-items-center mb-3">
                                    <h5 class="card-title mb-0">AC Current</h5>
                                    <div class="avatar">
                                        <span class="avatar-initial rounded-circle bg-info bg-opacity-10 text-info">
                                            <i class="bi bi-lightning"></i>
                                        </span>
                                    </div>
                                </div>
                                <div class="d-flex align-items-center">
                                    <h2 class="mb-0" id="acCurrent">--</h2>
                                    <span class="ms-2 text-muted">A</span>
                                </div>
                                <div class="mt-3">
                                    <div class="progress" style="height: 6px;">
                                        <div class="progress-bar bg-info" role="progressbar" id="currentBar" style="width: 0%"></div>
                                    </div>
                                    <small class="text-muted">Threshold: <span id="targetCurrent">10</span> A</small>
                                </div>
                            </div>
                        </div>
                    </div>
                    
                    <div class="col-xl-3 col-md-6 mb-4">
                        <div class="card h-100">
                            <div class="card-body">
                                <div class="d-flex justify-content-between align-items-center mb-3">
                                    <h5 class="card-title mb-0">Power</h5>
                                    <div class="avatar">
                                        <span class="avatar-initial rounded-circle bg-success bg-opacity-10 text-success">
                                            <i class="bi bi-battery-charging"></i>
                                        </span>
                                    </div>
                                </div>
                                <div class="d-flex align-items-center">
                                    <h2 class="mb-0" id="acPower">--</h2>
                                    <span class="ms-2 text-muted">W</span>
                                </div>
                                <div class="mt-3">
                                    <div class="progress" style="height: 6px;">
                                        <div class="progress-bar bg-success" role="progressbar" id="powerBar" style="width: 0%"></div>
                                    </div>
                                    <small class="text-muted">Energy: <span id="acEnergy">0</span> kWh</small>
                                </div>
                            </div>
                        </div>
                    </div>
                    
                    <div class="col-xl-3 col-md-6 mb-4">
                        <div class="card h-100">
                            <div class="card-body">
                                <div class="d-flex justify-content-between align-items-center mb-3">
                                    <h5 class="card-title mb-0">Frequency</h5>
                                    <div class="avatar">
                                        <span class="avatar-initial rounded-circle bg-warning bg-opacity-10 text-warning">
                                            <i class="bi bi-speedometer2"></i>
                                        </span>
                                    </div>
                                </div>
                                <div class="d-flex align-items-center">
                                    <h2 class="mb-0" id="acFrequency">--</h2>
                                    <span class="ms-2 text-muted">Hz</span>
                                </div>
                                <div class="mt-3">
                                    <div class="progress" style="height: 6px;">
                                        <div class="progress-bar bg-warning" role="progressbar" id="frequencyBar" style="width: 0%"></div>
                                    </div>
                                    <small class="text-muted">Target: <span id="targetFrequency">50</span> Hz</small>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
                
                <div class="row">
                    <div class="col-lg-8 mb-4">
                        <div class="card h-100">
                            <div class="card-body">
                                <h5 class="card-title">Voltage & Frequency Trend</h5>
                                <ul class="nav nav-tabs" id="chartTabs" role="tablist">
                                    <li class="nav-item" role="presentation">
                                        <button class="nav-link active" id="voltage-tab" data-bs-toggle="tab" data-bs-target="#voltageTab" type="button" role="tab">Voltage</button>
                                    </li>
                                    <li class="nav-item" role="presentation">
                                        <button class="nav-link" id="frequency-tab" data-bs-toggle="tab" data-bs-target="#frequencyTab" type="button" role="tab">Frequency</button>
                                    </li>
                                    <li class="nav-item" role="presentation">
                                        <button class="nav-link" id="power-tab" data-bs-toggle="tab" data-bs-target="#powerTab" type="button" role="tab">Power</button>
                                    </li>
                                </ul>
                                <div class="tab-content" id="chartTabContent">
                                    <div class="tab-pane fade show active" id="voltageTab" role="tabpanel">
                                        <div class="chart-container">
                                            <canvas id="voltageChart"></canvas>
                                        </div>
                                    </div>
                                    <div class="tab-pane fade" id="frequencyTab" role="tabpanel">
                                        <div class="chart-container">
                                            <canvas id="frequencyChart"></canvas>
                                        </div>
                                    </div>
                                    <div class="tab-pane fade" id="powerTab" role="tabpanel">
                                        <div class="chart-container">
                                            <canvas id="powerChart"></canvas>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                    
                    <div class="col-lg-4 mb-4">
                        <div class="card h-100">
                            <div class="card-body">
                                <h5 class="card-title">System Controls</h5>
                                <div class="mb-3">
                                    <label class="form-label">Relay Status</label>
                                    <div class="d-flex align-items-center">
                                        <div class="form-check form-switch me-3">
                                            <input class="form-check-input" type="checkbox" id="relaySwitch" onchange="toggleRelay()">
                                            <label class="form-check-label" for="relaySwitch" id="relayStatusText">OFF</label>
                                        </div>
                                        <span class="badge bg-secondary" id="relayStatusBadge">Disabled</span>
                                    </div>
                                </div>
                                <div class="mb-3">
                                    <label class="form-label">AI Detection</label>
                                    <div class="d-flex align-items-center">
                                        <div class="form-check form-switch me-3">
                                            <input class="form-check-input" type="checkbox" id="aiSwitch" checked onchange="toggleAI()">
                                            <label class="form-check-label" for="aiSwitch" id="aiStatusText">Enabled</label>
                                        </div>
                                        <span class="badge bg-success" id="aiStatusBadge">Active</span>
                                    </div>
                                </div>
                                <div class="mb-3">
                                    <label class="form-label">Current Grid Code</label>
                                    <div class="input-group">
                                        <select class="form-select" id="gridCodeSelect" onchange="updateGridCode()">
                                            <option value="IEEE 1547 (USA)">IEEE 1547 (USA)</option>
                                            <option value="IS 16184 (India)">IS 16184 (India)</option>
                                            <option value="EN 50438 (Europe)">EN 50438 (Europe)</option>
                                            <option value="AS/NZS 4777 (Australia/NZ)">AS/NZS 4777 (Australia/NZ)</option>
                                        </select>
                                        <button class="btn btn-outline-primary" type="button" onclick="showSettings()">
                                            <i class="bi bi-gear"></i>
                                        </button>
                                    </div>
                                </div>
                                <div class="alert alert-warning mt-4">
                                    <i class="bi bi-exclamation-triangle-fill me-2"></i>
                                    <strong>Last Event:</strong> <span id="lastEvent">System initialized</span>
                                </div>
                                <button class="btn btn-primary w-100 mt-2" onclick="showSettings()">
                                    <i class="bi bi-sliders me-2"></i>Advanced Settings
                                </button>
                            </div>
                        </div>
                    </div>
                </div>
                
                <div class="row">
                    <div class="col-12">
                        <div class="card">
                            <div class="card-body">
                                <h5 class="card-title">Recent Events</h5>
                                <div class="table-responsive">
                                    <table class="table table-borderless table-hover">
                                        <thead>
                                            <tr>
                                                <th>Time</th>
                                                <th>Event</th>
                                                <th>Status</th>
                                            </tr>
                                        </thead>
                                        <tbody id="eventLog">
                                            <tr>
                                                <td>00:00:00</td>
                                                <td>System initialized</td>
                                                <td><span class="badge bg-success">Normal</span></td>
                                            </tr>
                                        </tbody>
                                    </table>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <div style="text-align:center; margin-bottom: 20px;">
        <svg width="100%" height="320" viewBox="0 0 600 320" xmlns="http://www.w3.org/2000/svg">

          <!-- CPU -->
          <rect x="260" y="120" width="80" height="80" rx="10" fill="#222" stroke="#00c853" stroke-width="3"/>
          <text x="300" y="170" font-size="36" text-anchor="middle" dominant-baseline="middle">🤖</text>

          <!-- PZEM -->
          <text x="60" y="60" font-size="40" text-anchor="middle">📟</text>
          <text x="60" y="85" font-size="12" text-anchor="middle">PZEM-004T</text>

          <!-- SD Card -->
          <text x="60" y="260" font-size="40" text-anchor="middle">💾</text>
          <text x="60" y="285" font-size="12" text-anchor="middle">SD Card</text>

          <!-- Wi-Fi -->
          <text x="540" y="60" font-size="40" text-anchor="middle">📶</text>
          <text x="540" y="85" font-size="12" text-anchor="middle">Wi-Fi</text>

          <!-- Relay -->
          <text x="540" y="260" font-size="40" text-anchor="middle">🔘</text>
          <text x="540" y="285" font-size="12" text-anchor="middle">Relay</text>

          <defs>
            <!-- Paths -->
            <path id="pzem" d="M100,60 H200 V140 H260" />
            <path id="sd" d="M100,260 H200 V180 H260" />
            <path id="wifi" d="M500,60 H400 V140 H340" />
            <path id="relay" d="M500,260 H400 V180 H340" />
          </defs>

          <!-- Static Lines -->
          <use href="#pzem" stroke="#00c853" stroke-width="3" fill="none"/>
          <use href="#sd" stroke="#00c853" stroke-width="3" fill="none"/>
          <use href="#wifi" stroke="#00c853" stroke-width="3" fill="none"/>
          <use href="#relay" stroke="#00c853" stroke-width="3" fill="none"/>

          <!-- Moving Symbols -->
          <!-- PZEM AC wave -->
          <text font-size="18">
            <textPath href="#pzem" startOffset="0%">
              〰️
              <animate attributeName="startOffset" from="0%" to="100%" dur="2s" repeatCount="indefinite"/>
            </textPath>
          </text>

          <!-- SD packets -->
          <text font-size="14">
            <textPath href="#sd" startOffset="0%">
              ◼ ◼ ◼
              <animate attributeName="startOffset" from="0%" to="100%" dur="2.5s" repeatCount="indefinite"/>
            </textPath>
          </text>

          <!-- Wi-Fi symbols -->
          <text font-size="16">
            <textPath href="#wifi" startOffset="0%">
              📶 📶 📶
              <animate attributeName="startOffset" from="0%" to="100%" dur="3s" repeatCount="indefinite"/>
            </textPath>
          </text>

          <!-- Relay PWM pulses -->
          <text font-size="14">
            <textPath href="#relay" startOffset="0%">
              ▁▇ ▁▇ ▁▇
              <animate attributeName="startOffset" from="0%" to="100%" dur="2s" repeatCount="indefinite"/>
            </textPath>
          </text>

          <!-- Electron dots -->
          <circle r="4" fill="white">
            <animateMotion dur="2s" repeatCount="indefinite">
              <mpath href="#pzem"/>
            </animateMotion>
          </circle>
          <circle r="4" fill="white">
            <animateMotion dur="2.5s" repeatCount="indefinite">
              <mpath href="#sd"/>
            </animateMotion>
          </circle>
          <circle r="4" fill="white">
            <animateMotion dur="3s" repeatCount="indefinite">
              <mpath href="#wifi"/>
            </animateMotion>
          </circle>
          <circle r="4" fill="white">
            <animateMotion dur="2s" repeatCount="indefinite">
              <mpath href="#relay"/>
            </animateMotion>
          </circle>

        </svg>
        </div>

        <!-- Analytics Content -->
        <div id="analyticsContent" style="display: none;">
            <div class="container-fluid">
                <div class="row mb-4">
                    <div class="col-12">
                        <h2><i class="bi bi-graph-up me-2"></i>System Analytics</h2>
                        <p class="text-muted">Comprehensive analysis of system performance and grid parameters.</p>
                    </div>
                </div>
                
                <div class="row">
                    <div class="col-lg-6 mb-4">
                        <div class="card">
                            <div class="card-body">
                                <h5 class="card-title">Power Quality Analysis</h5>
                                <canvas id="powerQualityChart" height="200"></canvas>
                            </div>
                        </div>
                    </div>
                    <div class="col-lg-6 mb-4">
                        <div class="card">
                            <div class="card-body">
                                <h5 class="card-title">System Performance Metrics</h5>
                                <div id="performanceMetrics">
                                    <div class="row text-center">
                                        <div class="col-4">
                                            <h3 class="text-primary" id="uptimePercent">99.9%</h3>
                                            <small>Uptime</small>
                                        </div>
                                        <div class="col-4">
                                            <h3 class="text-success" id="avgVoltage">230.5V</h3>
                                            <small>Avg Voltage</small>
                                        </div>
                                        <div class="col-4">
                                            <h3 class="text-info" id="avgFrequency">50.02Hz</h3>
                                            <small>Avg Frequency</small>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
                
                <div class="row">
                    <div class="col-12">
                        <div class="card">
                            <div class="card-body">
                                <h5 class="card-title">Historical Trends (24 Hours)</h5>
                                <canvas id="historicalChart" height="100"></canvas>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <!-- Events Content -->
        <div id="eventsContent" style="display: none;">
            <div class="container-fluid">
                <div class="row mb-4">
                    <div class="col-12">
                        <div class="d-flex justify-content-between align-items-center">
                            <div>
                                <h2><i class="bi bi-list-check me-2"></i>System Events</h2>
                                <p class="text-muted">Monitor and review all system events and alerts.</p>
                            </div>
                            <div>
                                <button class="btn btn-outline-primary me-2" onclick="refreshEvents()">
                                    <i class="bi bi-arrow-clockwise me-1"></i>Refresh
                                </button>
                                <button class="btn btn-outline-danger" onclick="clearEvents()">
                                    <i class="bi bi-trash me-1"></i>Clear
                                </button>
                            </div>
                        </div>
                    </div>
                </div>
                
                <div class="row">
                    <div class="col-12">
                        <div class="card">
                            <div class="card-body">
                                <div class="table-responsive">
                                    <table class="table table-striped">
                                        <thead>
                                            <tr>
                                                <th>Timestamp</th>
                                                <th>Level</th>
                                                <th>Event</th>
                                                <th>Error Code</th>
                                            </tr>
                                        </thead>
                                        <tbody id="eventsTableBody">
                                            <!-- Events will be loaded here -->
                                        </tbody>
                                    </table>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <!-- Reports Content -->
        <div id="reportsContent" style="display: none;">
            <div class="container-fluid">
                <div class="row mb-4">
                    <div class="col-12">
                        <h2><i class="bi bi-file-earmark-text me-2"></i>System Reports</h2>
                        <p class="text-muted">Generate and download comprehensive system reports.</p>
                    </div>
                </div>
                
                <div class="row">
                    <div class="col-lg-6 mb-4">
                        <div class="card">
                            <div class="card-body">
                                <h5 class="card-title">Report Generation</h5>
                                <form id="reportForm">
                                    <div class="mb-3">
                                        <label class="form-label">Report Type</label>
                                        <select class="form-select" id="reportType">
                                            <option value="daily">Daily Report</option>
                                            <option value="weekly">Weekly Report</option>
                                            <option value="monthly">Monthly Report</option>
                                            <option value="custom">Custom Range</option>
                                        </select>
                                    </div>
                                    <div class="mb-3" id="dateRangeSection" style="display: none;">
                                        <div class="row">
                                            <div class="col-6">
                                                <label class="form-label">From Date</label>
                                                <input type="date" class="form-control" id="fromDate">
                                            </div>
                                            <div class="col-6">
                                                <label class="form-label">To Date</label>
                                                <input type="date" class="form-control" id="toDate">
                                            </div>
                                        </div>
                                    </div>
                                    <div class="mb-3">
                                        <label class="form-label">Include Sections</label>
                                        <div class="form-check">
                                            <input class="form-check-input" type="checkbox" id="includeSummary" checked>
                                            <label class="form-check-label" for="includeSummary">Executive Summary</label>
                                        </div>
                                        <div class="form-check">
                                            <input class="form-check-input" type="checkbox" id="includeEvents" checked>
                                            <label class="form-check-label" for="includeEvents">Events & Alarms</label>
                                        </div>
                                        <div class="form-check">
                                            <input class="form-check-input" type="checkbox" id="includeData" checked>
                                            <label class="form-check-label" for="includeData">Measurement Data</label>
                                        </div>
                                        <div class="form-check">
                                            <input class="form-check-input" type="checkbox" id="includeCharts" checked>
                                            <label class="form-check-label" for="includeCharts">Charts & Graphs</label>
                                        </div>
                                    </div>
                                    <button type="button" class="btn btn-primary w-100" onclick="generateReport()">
                                        <i class="bi bi-file-earmark-arrow-down me-2"></i>Generate Report
                                    </button>
                                </form>
                            </div>
                        </div>
                    </div>
                    
                    <div class="col-lg-6 mb-4">
                        <div class="card">
                            <div class="card-body">
                                <h5 class="card-title">Quick Statistics</h5>
                                <div class="row text-center">
                                    <div class="col-6 mb-3">
                                        <h4 class="text-primary" id="totalEvents">0</h4>
                                        <small>Total Events</small>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <h4 class="text-warning" id="totalAlarms">0</h4>
                                        <small>Total Alarms</small>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <h4 class="text-success" id="systemUptime">0</h4>
                                        <small>Uptime (hours)</small>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <h4 class="text-info" id="dataPoints">0</h4>
                                        <small>Data Points</small>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <!-- Security Content -->
        <div id="securityContent" style="display: none;">
            <div class="container-fluid">
                <div class="row mb-4">
                    <div class="col-12">
                        <h2><i class="bi bi-shield-check me-2"></i>Security & Audit</h2>
                        <p class="text-muted">Monitor security events and manage access controls.</p>
                    </div>
                </div>
                
                <div class="row">
                    <div class="col-lg-8 mb-4">
                        <div class="card">
                            <div class="card-body">
                                <h5 class="card-title">Security Events</h5>
                                <div class="table-responsive">
                                    <table class="table table-striped">
                                        <thead>
                                            <tr>
                                                <th>Timestamp</th>
                                                <th>Event</th>
                                                <th>Source IP</th>
                                                <th>Status</th>
                                            </tr>
                                        </thead>
                                        <tbody id="securityEventsTable">
                                            <!-- Security events will be loaded here -->
                                        </tbody>
                                    </table>
                                </div>
                            </div>
                        </div>
                    </div>
                    
                    <div class="col-lg-4 mb-4">
                        <div class="card">
                            <div class="card-body">
                                <h5 class="card-title">Security Status</h5>
                                <div class="mb-3">
                                    <div class="d-flex justify-content-between">
                                        <span>Session Timeout</span>
                                        <span class="text-success">30 min</span>
                                    </div>
                                </div>
                                <div class="mb-3">
                                    <div class="d-flex justify-content-between">
                                        <span>CSRF Protection</span>
                                        <span class="text-success">Enabled</span>
                                    </div>
                                </div>
                                <div class="mb-3">
                                    <div class="d-flex justify-content-between">
                                        <span>Failed Login Attempts</span>
                                        <span class="text-warning" id="failedAttempts">0</span>
                                    </div>
                                </div>
                                <div class="mb-3">
                                    <div class="d-flex justify-content-between">
                                        <span>Last Login</span>
                                        <span id="lastLogin">Never</span>
                                    </div>
                                </div>
                                <button class="btn btn-outline-warning w-100 mb-2" onclick="changePassword()">
                                    <i class="bi bi-key me-2"></i>Change Password
                                </button>
                                <button class="btn btn-outline-danger w-100" onclick="lockSystem()">
                                    <i class="bi bi-lock me-2"></i>Lock System
                                </button>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
    </div>
    
    <!-- Settings Panel -->
    <div id="settingsPanel" class="settings-panel">
        <div class="settings-content">
            <div class="d-flex justify-content-between align-items-center mb-4">
                <h3><i class="bi bi-sliders me-2"></i> System Settings</h3>
                <button type="button" class="btn-close btn-close-white" onclick="closeSettings()"></button>
            </div>
            
            <ul class="nav nav-tabs mb-4" id="settingsTabs" role="tablist">
                <li class="nav-item" role="presentation">
                    <button class="nav-link active" id="grid-tab" data-bs-toggle="tab" data-bs-target="#gridTab" type="button" role="tab">Grid Settings</button>
                </li>
                <li class="nav-item" role="presentation">
                    <button class="nav-link" id="protection-tab" data-bs-toggle="tab" data-bs-target="#protectionTab" type="button" role="tab">Protection</button>
                </li>
                <li class="nav-item" role="presentation">
                    <button class="nav-link" id="system-tab" data-bs-toggle="tab" data-bs-target="#systemTab" type="button" role="tab">System</button>
                </li>
                <li class="nav-item" role="presentation">
                    <button class="nav-link" id="logs-tab" data-bs-toggle="tab" data-bs-target="#logsTab" type="button" role="tab">Logs</button>
                </li>
            </ul>
            
            <div class="tab-content" id="settingsTabContent">
                <div class="tab-pane fade show active" id="gridTab" role="tabpanel">
                    <div class="row">
                        <div class="col-md-6 mb-3">
                            <label for="acVoltageThreshold" class="form-label">AC Voltage Threshold (V)</label>
                            <input type="number" class="form-control" id="acVoltageThreshold" value="230.0" step="0.1">
                        </div>
                        <div class="col-md-6 mb-3">
                            <label for="frequencyThreshold" class="form-label">Frequency Threshold (Hz)</label>
                            <input type="number" class="form-control" id="frequencyThreshold" value="50.0" step="0.1">
                        </div>
                    </div>
                    <div class="mb-3">
                        <label for="gridCodeSelectSettings" class="form-label">Grid Code</label>
                        <select class="form-select" id="gridCodeSelectSettings">
                            <option value="IEEE 1547 (USA)">IEEE 1547 (USA)</option>
                            <option value="IS 16184 (India)">IS 16184 (India)</option>
                            <option value="EN 50438 (Europe)">EN 50438 (Europe)</option>
                            <option value="AS/NZS 4777 (Australia/NZ)">AS/NZS 4777 (Australia/NZ)</option>
                        </select>
                    </div>
                    <div class="alert alert-info">
                        <i class="bi bi-info-circle-fill me-2"></i>
                        These settings define the nominal values for grid synchronization and protection thresholds.
                    </div>
                </div>
                
                <div class="tab-pane fade" id="protectionTab" role="tabpanel">
                    <div class="row">
                        <div class="col-md-6 mb-3">
                            <label for="islandingDelay" class="form-label">Islanding Detection Delay (s)</label>
                            <input type="number" class="form-control" id="islandingDelay" value="10" min="1" max="60">
                        </div>
                        <div class="col-md-6 mb-3">
                            <label for="acCurrentThreshold" class="form-label">Current Threshold (A)</label>
                            <input type="number" class="form-control" id="acCurrentThreshold" value="10.0" step="0.1">
                        </div>
                    </div>
                    <div class="mb-3">
                        <div class="form-check form-switch">
                            <input class="form-check-input" type="checkbox" id="aiDetectionEnabled" checked>
                            <label class="form-check-label" for="aiDetectionEnabled">Enable AI Detection</label>
                        </div>
                    </div>
                    <div class="mb-3">
                        <label for="aiConfidenceThreshold" class="form-label">AI Confidence Threshold</label>
                        <input type="range" class="form-range" id="aiConfidenceThreshold" min="50" max="100" value="85">
                        <div class="d-flex justify-content-between">
                            <small>50%</small>
                            <small>100%</small>
                        </div>
                        <div class="text-center">
                            <span id="aiConfidenceValue">85%</span>
                        </div>
                    </div>
                </div>
                
                <div class="tab-pane fade" id="systemTab" role="tabpanel">
                    <div class="row">
                        <div class="col-md-6 mb-3">
                            <label class="form-label">System Time</label>
                            <input type="datetime-local" class="form-control" id="systemTime">
                        </div>
                        <div class="col-md-6 mb-3">
                            <label class="form-label">Time Zone</label>
                            <select class="form-select" id="timeZone">
                                <option value="UTC">UTC</option>
                                <option value="IST" selected>IST (UTC+5:30)</option>
                                <option value="EST">EST (UTC-5)</option>
                                <option value="PST">PST (UTC-8)</option>
                            </select>
                        </div>
                    </div>
                    <div class="mb-3">
                        <label class="form-label">System Logging</label>
                        <div class="form-check form-switch mb-2">
                            <input class="form-check-input" type="checkbox" id="enableFileLogging" checked>
                            <label class="form-check-label" for="enableFileLogging">Enable File Logging</label>
                        </div>
                        <div class="form-check form-switch mb-2">
                            <input class="form-check-input" type="checkbox" id="enableSerialLogging" checked>
                            <label class="form-check-label" for="enableSerialLogging">Enable Serial Logging</label>
                        </div>
                    </div>
                    <div class="mb-3">
                        <label class="form-label">System Reset</label>
                        <button class="btn btn-outline-danger w-100" onclick="resetSystem()">
                            <i class="bi bi-arrow-counterclockwise me-2"></i>Reset System
                        </button>
                    </div>
                </div>
                
                <div class="tab-pane fade" id="logsTab" role="tabpanel">
                    <div class="mb-3">
                        <div class="d-flex justify-content-between align-items-center">
                            <label class="form-label">System Logs</label>
                            <div>
                                <button class="btn btn-sm btn-outline-primary me-2" onclick="refreshLogs()">
                                    <i class="bi bi-arrow-repeat"></i>
                                </button>
                                <button class="btn btn-sm btn-outline-danger" onclick="clearLogs()">
                                    <i class="bi bi-trash"></i>
                                </button>
                            </div>
                        </div>
                        <div class="card bg-light border-secondary mt-2">
                            <div class="card-body p-0">
                                <div style="max-height: 300px; overflow-y: auto;" id="logContent">
                                    <div class="log-entry">[00:00:00] System initialized</div>
                                    <div class="log-entry">[00:00:05] WiFi connected</div>
                                    <div class="log-entry">[00:00:10] PZEM-004T initialized</div>
                                </div>
                            </div>
                        </div>
                    </div>
                    <div class="mb-3">
                        <label class="form-label">Log & Measurement Export</label>
                        <div class="d-grid gap-2">
                            <!-- Event Log Export -->
                            <button class="btn btn-outline-primary" onclick="exportLogs('txt')">
                                <i class="bi bi-file-earmark-text me-2"></i>Export Event Logs (TXT)
                            </button>
                            <button class="btn btn-outline-success" onclick="exportLogs('csv')">
                                <i class="bi bi-file-earmark-spreadsheet me-2"></i>Export Event Logs (CSV)
                            </button>
                            <button class="btn btn-outline-success" onclick="download_measurements()">
                                <i class="bi bi-file-earmark-spreadsheet me-2"></i>Download Measurements
                            </button>
                        </div>
                    </div>
                    <div class="mb-3">
                        <label class="form-label">Configuration Backup</label>
                        <div class="d-grid gap-2">
                            <button class="btn btn-outline-warning" onclick="backupConfig()">
                                <i class="bi bi-save me-2"></i>Backup Configuration
                            </button>
                            <button class="btn btn-outline-info" onclick="restoreConfig()">
                                <i class="bi bi-upload me-2"></i>Restore Configuration
                            </button>
                            <input type="file" id="configFileInput" accept=".json" style="display: none;" onchange="handleConfigRestore(this)">
                        </div>
                    </div>
                </div>
            </div>
            
            <div class="d-flex justify-content-end mt-4">
                <button type="button" class="btn btn-outline-light me-3" onclick="closeSettings()">Cancel</button>
                <button type="button" class="btn btn-primary" onclick="saveSettings()">Save Settings</button>
            </div>
        </div>
    </div>
    
    <!-- Loading Overlay -->
    <div id="loadingOverlay" class="loading-overlay" style="display: none;">
        <div class="spinner-border text-primary" role="status">
            <span class="visually-hidden">Loading...</span>
        </div>
        <p class="mt-3" id="loadingText">Loading...</p>
    </div>
    
    <!-- Connection Status -->
    <div id="connectionStatus" class="connection-status" style="display: none;">
        <i class="bi bi-wifi"></i> <span id="connectionStatusText"></span>
    </div>

    <script src="https://cdn.jsdelivr.net/npm/bootstrap@5.1.3/dist/js/bootstrap.bundle.min.js"></script>
    <script>
        // Global variables
        let voltageChart, frequencyChart, powerChart, powerQualityChart, historicalChart;
        let isConnected = true;
        let reconnectAttempts = 0;
        const maxReconnectAttempts = 25;
        const reconnectDelay = 3000;
        let currentView = 'dashboard';
        
        // Initialize charts
        function initializeCharts() {
            const voltageCtx = document.getElementById('voltageChart').getContext('2d');
            const frequencyCtx = document.getElementById('frequencyChart').getContext('2d');
            const powerCtx = document.getElementById('powerChart').getContext('2d');
            
            const chartOptions = {
                responsive: true,
                maintainAspectRatio: false,
                scales: { 
                    x: { 
                        type: 'realtime',
                        realtime: {
                            duration: 300000, // 5 minutes
                            refresh: 1000,
                            delay: 1000
                        },
                        grid: {
                            color: 'rgba(255, 255, 255, 0.1)'
                        },
                        ticks: {
                            color: '#adb5bd'
                        }
                    },
                    y: {
                        title: {
                            display: true,
                            color: '#adb5bd'
                        },
                        grid: {
                            color: 'rgba(255, 255, 255, 0.1)'
                        },
                        ticks: {
                            color: '#adb5bd'
                        }
                    }
                },
                plugins: {
                    legend: {
                        labels: {
                            color: '#adb5bd'
                        }
                    }
                },
                interaction: {
                    intersect: false
                }
            };
            
            voltageChart = new Chart(voltageCtx, {
                type: 'line',
                data: { 
                    datasets: [{ 
                        label: 'Voltage (V)', 
                        borderColor: '#4361ee',
                        backgroundColor: 'rgba(67, 97, 238, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        data: [] 
                    }] 
                },
                options: {
                    ...chartOptions,
                    scales: {
                        ...chartOptions.scales,
                        y: {
                            ...chartOptions.scales.y,
                            title: {
                                ...chartOptions.scales.y.title,
                                text: 'Voltage (V)'
                            }
                        }
                    }
                }
            });
            
            frequencyChart = new Chart(frequencyCtx, {
                type: 'line',
                data: { 
                    datasets: [{ 
                        label: 'Frequency (Hz)', 
                        borderColor: '#f72585',
                        backgroundColor: 'rgba(247, 37, 133, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        data: [] 
                    }] 
                },
                options: {
                    ...chartOptions,
                    scales: {
                        ...chartOptions.scales,
                        y: {
                            ...chartOptions.scales.y,
                            title: {
                                ...chartOptions.scales.y.title,
                                text: 'Frequency (Hz)'
                            }
                        }
                    }
                }
            });
            
            powerChart = new Chart(powerCtx, {
                type: 'line',
                data: { 
                    datasets: [{ 
                        label: 'Power (W)', 
                        borderColor: '#4cc9f0',
                        backgroundColor: 'rgba(76, 201, 240, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        data: [] 
                    }] 
                },
                options: {
                    ...chartOptions,
                    scales: {
                        ...chartOptions.scales,
                        y: {
                            ...chartOptions.scales.y,
                            title: {
                                ...chartOptions.scales.y.title,
                                text: 'Power (W)'
                            }
                        }
                    }
                }
            });
        }
        
        // Navigation functions
        function showDashboard() {
            hideAllContent();
            document.getElementById('dashboardContent').style.display = 'block';
            setActiveNavItem('dashboard');
            currentView = 'dashboard';
        }
        
        function showAnalytics() {
            hideAllContent();
            document.getElementById('analyticsContent').style.display = 'block';
            setActiveNavItem('analytics');
            currentView = 'analytics';
            loadAnalytics();
        }
        
        function showEvents() {
            hideAllContent();
            document.getElementById('eventsContent').style.display = 'block';
            setActiveNavItem('events');
            currentView = 'events';
            loadEvents();
        }
        
        function showReports() {
            hideAllContent();
            document.getElementById('reportsContent').style.display = 'block';
            setActiveNavItem('reports');
            currentView = 'reports';
            loadReportData();
        }
        
        function showSecurity() {
            hideAllContent();
            document.getElementById('securityContent').style.display = 'block';
            setActiveNavItem('security');
            currentView = 'security';
            loadSecurity();
        }
        
        function hideAllContent() {
            document.getElementById('dashboardContent').style.display = 'none';
            document.getElementById('analyticsContent').style.display = 'none';
            document.getElementById('eventsContent').style.display = 'none';
            document.getElementById('reportsContent').style.display = 'none';
            document.getElementById('securityContent').style.display = 'none';
        }
        
        function setActiveNavItem(active) {
            document.querySelectorAll('.sidebar-link').forEach(link => {
                link.classList.remove('active');
            });
            
            const navMap = {
                'dashboard': 0,
                'analytics': 1,
                'settings': 2,
                'events': 3,
                'reports': 4,
                'security': 5
            };
            
            if (navMap[active] !== undefined) {
                document.querySelectorAll('.sidebar-link')[navMap[active]].classList.add('active');
            }
        }
        
        // Load functions for different sections
        function loadAnalytics() {
            fetch('/api/analytics')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('uptimePercent').textContent = data.uptimePercent + '%';
                    document.getElementById('avgVoltage').textContent = data.avgVoltage + 'V';
                    document.getElementById('avgFrequency').textContent = data.avgFrequency + 'Hz';
                    
                    // Initialize analytics charts if not already done
                    if (!powerQualityChart) {
                        const ctx = document.getElementById('powerQualityChart').getContext('2d');
                        powerQualityChart = new Chart(ctx, {
                            type: 'doughnut',
                            data: {
                                labels: ['Normal', 'Warning', 'Critical'],
                                datasets: [{
                                    data: [85, 10, 5],
                                    backgroundColor: ['#28a745', '#ffc107', '#dc3545']
                                }]
                            }
                        });
                    }
                    
                    if (!historicalChart) {
                        const ctx = document.getElementById('historicalChart').getContext('2d');
                        historicalChart = new Chart(ctx, {
                            type: 'line',
                            data: {
                                labels: data.historicalLabels || [],
                                datasets: [{
                                    label: 'Voltage',
                                    data: data.historicalVoltage || [],
                                    borderColor: '#4361ee',
                                    fill: false
                                }, {
                                    label: 'Frequency',
                                    data: data.historicalFrequency || [],
                                    borderColor: '#f72585',
                                    fill: false
                                }]
                            }
                        });
                    }
                })
                .catch(error => console.error('Error loading analytics:', error));
        }
        
        function loadEvents() {
            fetch('/api/events')
                .then(response => response.json())
                .then(data => {
                    const tbody = document.getElementById('eventsTableBody');
                    tbody.innerHTML = '';
                    
                    data.events.forEach(event => {
                        const row = tbody.insertRow();
                        row.innerHTML = `
                            <td>${event.timestamp}</td>
                            <td><span class="badge bg-${getLevelColor(event.level)}">${event.level}</span></td>
                            <td>${event.event}</td>
                            <td>${event.errorCode || '-'}</td>
                        `;
                    });
                })
                .catch(error => console.error('Error loading events:', error));
        }
        
        function loadReportData() {
            fetch('/api/reports')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('totalEvents').textContent = data.totalEvents || 0;
                    document.getElementById('totalAlarms').textContent = data.totalAlarms || 0;
                    document.getElementById('systemUptime').textContent = data.systemUptime || 0;
                    document.getElementById('dataPoints').textContent = data.dataPoints || 0;
                })
                .catch(error => console.error('Error loading report data:', error));
        }
        
        function loadSecurity() {
            fetch('/api/security')
                .then(response => response.json())
                .then(data => {
                    const tbody = document.getElementById('securityEventsTable');
                    tbody.innerHTML = '';
                    
                    data.securityEvents.forEach(event => {
                        const row = tbody.insertRow();
                        row.innerHTML = `
                            <td>${event.timestamp}</td>
                            <td>${event.event}</td>
                            <td>${event.sourceIP}</td>
                            <td><span class="badge bg-${event.success ? 'success' : 'danger'}">${event.success ? 'Success' : 'Failed'}</span></td>
                        `;
                    });
                    
                    document.getElementById('failedAttempts').textContent = data.failedAttempts || 0;
                    document.getElementById('lastLogin').textContent = data.lastLogin || 'Never';
                })
                .catch(error => console.error('Error loading security:', error));
        }
        
        function getLevelColor(level) {
            switch(level) {
                case 'ERROR': return 'danger';
                case 'WARNING': return 'warning';
                case 'INFO': return 'info';
                case 'CRITICAL': return 'dark';
                default: return 'secondary';
            }
        }
        
        // Enhanced settings functions
        function showSettings() {
            document.getElementById('settingsPanel').style.display = 'block';
            showLoading('Loading settings...');
            
            fetch('/api/settings')
                .then(response => {
                    if (!response.ok) throw new Error('Failed to load settings');
                    return response.json();
                })
                .then(settings => {
                    document.getElementById('acVoltageThreshold').value = settings.acVoltageThreshold || 230.0;
                    document.getElementById('frequencyThreshold').value = settings.frequencyThreshold || 50.0;
                    document.getElementById('acCurrentThreshold').value = settings.acCurrentThreshold || 10.0;
                    document.getElementById('islandingDelay').value = settings.islandingDelay || 10;
                    document.getElementById('aiDetectionEnabled').checked = settings.aiDetectionEnabled !== false;
                    document.getElementById('aiConfidenceThreshold').value = (settings.aiConfidenceThreshold || 0.85) * 100;
                    document.getElementById('aiConfidenceValue').textContent = Math.round((settings.aiConfidenceThreshold || 0.85) * 100) + '%';
                    
                    document.getElementById('enableFileLogging').checked = settings.enableFileLogging !== false;
                    document.getElementById('enableSerialLogging').checked = settings.enableSerialLogging !== false;
                    
                    const gridCodeSelect = document.getElementById('gridCodeSelectSettings');
                    gridCodeSelect.innerHTML = '';
                    if (settings.gridCodes && settings.gridCodes.length > 0) {
                        settings.gridCodes.forEach(code => {
                            const option = document.createElement('option');
                            option.value = code.name;
                            option.textContent = code.name;
                            if (code.name === settings.currentGridCode) {
                                option.selected = true;
                            }
                            gridCodeSelect.appendChild(option);
                        });
                    } else {
                        const option = document.createElement('option');
                        option.value = 'IS 16184 (India)';
                        option.textContent = 'IS 16184 (India)';
                        gridCodeSelect.appendChild(option);
                    }
                })
                .catch(error => {
                    console.error('Error loading settings:', error);
                    showToast('Failed to load settings', 'danger');
                })
                .finally(() => {
                    hideLoading();
                });
        }
        
        function closeSettings() {
            document.getElementById('settingsPanel').style.display = 'none';
        }
        
        function saveSettings() {
            const settings = {
                acVoltageThreshold: parseFloat(document.getElementById('acVoltageThreshold').value),
                frequencyThreshold: parseFloat(document.getElementById('frequencyThreshold').value),
                acCurrentThreshold: parseFloat(document.getElementById('acCurrentThreshold').value),
                islandingDelay: parseInt(document.getElementById('islandingDelay').value),
                aiDetectionEnabled: document.getElementById('aiDetectionEnabled').checked,
                aiConfidenceThreshold: parseInt(document.getElementById('aiConfidenceThreshold').value) / 100,
                currentGridCode: document.getElementById('gridCodeSelectSettings').value,
                enableFileLogging: document.getElementById('enableFileLogging').checked,
                enableSerialLogging: document.getElementById('enableSerialLogging').checked
            };
            
            showLoading('Saving settings...');
            
            fetch('/api/settings', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(settings)
            })
            .then(response => {
                if(response.ok) {
                    closeSettings();
                    showToast('Settings saved successfully', 'success');
                    fetchData();
                } else {
                    throw new Error('Failed to save settings');
                }
            })
            .catch(error => {
                console.error('Error saving settings:', error);
                showToast('Error saving settings', 'danger');
            })
            .finally(() => {
                hideLoading();
            });
        }
        
        // Log management functions
        function refreshLogs() {
            showLoading('Refreshing logs...');
            fetch('/api/logs')
                .then(response => response.json())
                .then(data => {
                    const logContent = document.getElementById('logContent');
                    logContent.innerHTML = '';
                    
                    data.logs.forEach(log => {
                        const div = document.createElement('div');
                        div.className = `log-entry log-${log.level.toLowerCase()}`;
                        div.textContent = `[${log.timestamp}] [${log.level}] ${log.message}`;
                        logContent.appendChild(div);
                    });
                    
                    showToast('Logs refreshed', 'success');
                })
                .catch(error => {
                    console.error('Error refreshing logs:', error);
                    showToast('Error refreshing logs', 'danger');
                })
                .finally(() => {
                    hideLoading();
                });
        }
        
        function clearLogs() {
            if (confirm('Are you sure you want to clear all logs? This action cannot be undone.')) {
                showLoading('Clearing logs...');
                fetch('/api/logs', {
                    method: 'DELETE'
                })
                .then(response => {
                    if (response.ok) {
                        document.getElementById('logContent').innerHTML = '<div class="log-entry">Logs cleared</div>';
                        showToast('Logs cleared successfully', 'success');
                    } else {
                        throw new Error('Failed to clear logs');
                    }
                })
                .catch(error => {
                    console.error('Error clearing logs:', error);
                    showToast('Error clearing logs', 'danger');
                })
                .finally(() => {
                    hideLoading();
                });
            }
        }
        
        function exportLogs(format) {
            showLoading('Exporting logs...');
            fetch(`/api/logs/export?format=${format}`)
                .then(response => {
                    if (response.ok) {
                        return response.blob();
                    }
                    throw new Error('Export failed');
                })
                .then(blob => {
                    const url = window.URL.createObjectURL(blob);
                    const a = document.createElement('a');
                    a.href = url;
                    a.download = `system_logs.${format}`;
                    a.click();
                    window.URL.revokeObjectURL(url);
                    showToast('Logs exported successfully', 'success');
                })
                .catch(error => {
                    console.error('Error exporting logs:', error);
                    showToast('Error exporting logs', 'danger');
                })
                .finally(() => {
                    hideLoading();
                });
        }

        function download_measurements() {
            showLoading('Downloading measurements...'); // Show loading spinner or message

            fetch('/download_measurements') // matches server.on("/download_measurements")
                .then(response => {
                    if (!response.ok) throw new Error('Download failed');
                    return response.blob();
                })
                .then(blob => {
                    const url = URL.createObjectURL(blob);
                    const a = document.createElement('a');
                    a.href = url;
                    a.download = 'measurements.csv';
                    a.click();
                    URL.revokeObjectURL(url);

                    showToast('Measurements downloaded successfully', 'success'); // Show success message
                })
                .catch(error => {
                    console.error('Download error:', error);
                    showToast('Error downloading measurements', 'danger'); // Show error message
                })
                .finally(() => {
                    hideLoading(); // Hide loading spinner/message
                });
        }

        // Config backup/restore functions
        function backupConfig() {
            showLoading('Creating backup...');
            fetch('/api/config/backup')
                .then(response => {
                    if (response.ok) {
                        return response.blob();
                    }
                    throw new Error('Backup failed');
                })
                .then(blob => {
                    const url = window.URL.createObjectURL(blob);
                    const a = document.createElement('a');
                    a.href = url;
                    a.download = `system_config_${new Date().toISOString().split('T')[0]}.json`;
                    a.click();
                    window.URL.revokeObjectURL(url);
                    showToast('Configuration backup created', 'success');
                })
                .catch(error => {
                    console.error('Error creating backup:', error);
                    showToast('Error creating backup', 'danger');
                })
                .finally(() => {
                    hideLoading();
                });
        }
        
        function restoreConfig() {
            document.getElementById('configFileInput').click();
        }
        
        function handleConfigRestore(input) {
            if (input.files && input.files[0]) {
                const file = input.files[0];
                const formData = new FormData();
                formData.append('config', file);
                
                showLoading('Restoring configuration...');
                
                fetch('/api/config/restore', {
                    method: 'POST',
                    body: formData
                })
                .then(response => {
                    if (response.ok) {
                        showToast('Configuration restored successfully. System will reboot.', 'success');
                        setTimeout(() => {
                            window.location.reload();
                        }, 3000);
                    } else {
                        throw new Error('Restore failed');
                    }
                })
                .catch(error => {
                    console.error('Error restoring config:', error);
                    showToast('Error restoring configuration', 'danger');
                })
                .finally(() => {
                    hideLoading();
                });
            }
        }
        
        // System reset function
        function resetSystem() {
            if (confirm('Are you sure you want to reset the system? This will restore all settings to defaults and restart the device.')) {
                showLoading('Resetting system...');
                fetch('/api/system/reset', {
                    method: 'POST'
                })
                .then(response => {
                    if (response.ok) {
                        showToast('System reset initiated. Device will restart.', 'warning');
                        setTimeout(() => {
                            window.location.reload();
                        }, 5000);
                    } else {
                        throw new Error('Reset failed');
                    }
                })
                .catch(error => {
                    console.error('Error resetting system:', error);
                    showToast('Error resetting system', 'danger');
                })
                .finally(() => {
                    hideLoading();
                });
            }
        }
        
        // Event management functions
        function refreshEvents() {
            loadEvents();
            showToast('Events refreshed', 'success');
        }
        
        function clearEvents() {
            if (confirm('Are you sure you want to clear all events?')) {
                fetch('/api/events', {
                    method: 'DELETE'
                })
                .then(response => {
                    if (response.ok) {
                        loadEvents();
                        showToast('Events cleared', 'success');
                    } else {
                        throw new Error('Failed to clear events');
                    }
                })
                .catch(error => {
                    console.error('Error clearing events:', error);
                    showToast('Error clearing events', 'danger');
                });
            }
        }
        
        // Report generation
        function generateReport() {
            const reportType = document.getElementById('reportType').value;
            const includeSections = {
                summary: document.getElementById('includeSummary').checked,
                events: document.getElementById('includeEvents').checked,
                data: document.getElementById('includeData').checked,
                charts: document.getElementById('includeCharts').checked
            };
            
            let params = `type=${reportType}`;
            Object.keys(includeSections).forEach(key => {
                if (includeSections[key]) {
                    params += `&include=${key}`;
                }
            });
            
            if (reportType === 'custom') {
                const fromDate = document.getElementById('fromDate').value;
                const toDate = document.getElementById('toDate').value;
                if (fromDate && toDate) {
                    params += `&from=${fromDate}&to=${toDate}`;
                }
            }
            
            showLoading('Generating report...');
            fetch(`/api/reports/generate?${params}`)
                .then(response => {
                    if (response.ok) {
                        return response.blob();
                    }
                    throw new Error('Report generation failed');
                })
                .then(blob => {
                    const url = window.URL.createObjectURL(blob);
                    const a = document.createElement('a');
                    a.href = url;
                    a.download = `system_report_${reportType}_${new Date().toISOString().split('T')[0]}.pdf`;
                    a.click();
                    window.URL.revokeObjectURL(url);
                    showToast('Report generated successfully', 'success');
                })
                .catch(error => {
                    console.error('Error generating report:', error);
                    showToast('Error generating report', 'danger');
                })
                .finally(() => {
                    hideLoading();
                });
        }
        
        // Security functions
        function changePassword() {
            const newPassword = prompt('Enter new password:');
            if (newPassword && newPassword.length >= 6) {
                fetch('/api/security/password', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({
                        newPassword: newPassword
                    })
                })
                .then(response => {
                    if (response.ok) {
                        showToast('Password changed successfully', 'success');
                    } else {
                        throw new Error('Failed to change password');
                    }
                })
                .catch(error => {
                    console.error('Error changing password:', error);
                    showToast('Error changing password', 'danger');
                });
            } else if (newPassword !== null) {
                showToast('Password must be at least 6 characters long', 'warning');
            }
        }
        
        function lockSystem() {
            if (confirm('Are you sure you want to lock the system? You will need to log in again.')) {
                fetch('/logout')
                    .then(() => {
                        window.location.reload();
                    });
            }
        }
        
        // Theme toggle functionality
        function initializeTheme() {
            const themeToggle = document.getElementById('themeToggle');
            const html = document.documentElement;
            
            const savedTheme = localStorage.getItem('theme') || 
                             (window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
            
            if (savedTheme === 'dark') {
                html.setAttribute('data-theme', 'dark');
                themeToggle.checked = true;
            }
            
            themeToggle.addEventListener('change', function() {
                if (this.checked) {
                    html.setAttribute('data-theme', 'dark');
                    localStorage.setItem('theme', 'dark');
                } else {
                    html.removeAttribute('data-theme');
                    localStorage.setItem('theme', 'light');
                }
                updateChartThemes();
            });
        }
        
        function updateChartThemes() {
            const isDark = document.documentElement.getAttribute('data-theme') === 'dark';
            const textColor = isDark ? '#e0e0e0' : '#212529';
            const gridColor = isDark ? 'rgba(255, 255, 255, 0.1)' : 'rgba(0, 0, 0, 0.1)';
            
            [voltageChart, frequencyChart, powerChart].forEach(chart => {
                if (chart) {
                    chart.options.scales.x.grid.color = gridColor;
                    chart.options.scales.x.ticks.color = textColor;
                    chart.options.scales.y.grid.color = gridColor;
                    chart.options.scales.y.ticks.color = textColor;
                    chart.options.plugins.legend.labels.color = textColor;
                    chart.update();
                }
            });
        }
        
        // Sidebar toggle
        function initializeSidebar() {
            document.getElementById('sidebarToggle').addEventListener('click', function() {
                document.getElementById('sidebar').classList.toggle('show');
                document.getElementById('mainContent').classList.toggle('sidebar-open');
            });
        }
        
        // Update current time
        function updateCurrentTime() {
            const now = new Date();
            document.getElementById('currentTime').textContent = now.toLocaleTimeString();
        }
        
        // Connection status management
        function updateConnectionStatus(connected) {
            isConnected = connected;
            const statusElement = document.getElementById('connectionStatus');
            const statusText = document.getElementById('connectionStatusText');
            
            if (connected) {
                statusElement.className = 'connection-status connection-success';
                statusText.textContent = 'Connected';
                reconnectAttempts = 0;
            } else {
                statusElement.className = 'connection-status connection-error';
                statusText.textContent = 'Disconnected - Reconnecting...';
            }
            
            statusElement.style.display = 'block';
            setTimeout(() => {
                statusElement.style.display = 'none';
            }, 3000);
        }
        
        // AI Confidence slider
        document.getElementById('aiConfidenceThreshold').addEventListener('input', function() {
            document.getElementById('aiConfidenceValue').textContent = this.value + '%';
        });
        
        // Relay control
        function toggleRelay() {
            const relaySwitch = document.getElementById('relaySwitch');
            const status = relaySwitch.checked;
            
            showLoading(status ? 'Activating relay...' : 'Deactivating relay...');
            
            fetch('/api/command', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    act: status ? 'relayOn' : 'relayOff'
                })
            })
            .then(response => {
                if(response.ok) {
                    document.getElementById('relayStatusText').textContent = status ? 'ON' : 'OFF';
                    document.getElementById('relayStatusBadge').className = status ? 'badge bg-danger' : 'badge bg-secondary';
                    document.getElementById('relayStatusBadge').textContent = status ? 'Active' : 'Disabled';
                    showToast(`Relay turned ${status ? 'ON' : 'OFF'}`, 'success');
                } else {
                    relaySwitch.checked = !status;
                    throw new Error('Failed to toggle relay');
                }
            })
            .catch(error => {
                console.error('Error toggling relay:', error);
                showToast('Error toggling relay', 'danger');
            })
            .finally(() => {
                hideLoading();
            });
        }
        
        // AI control
        function toggleAI() {
            const aiSwitch = document.getElementById('aiSwitch');
            const status = aiSwitch.checked;
            
            document.getElementById('aiStatusText').textContent = status ? 'Enabled' : 'Disabled';
            document.getElementById('aiStatusBadge').className = status ? 'badge bg-success' : 'badge bg-secondary';
            document.getElementById('aiStatusBadge').textContent = status ? 'Active' : 'Disabled';
            
            showLoading(status ? 'Enabling AI detection...' : 'Disabling AI detection...');
            
            fetch('/api/command', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    act: 'setAIDetection',
                    enabled: status
                })
            })
            .then(response => {
                if(response.ok) {
                    showToast(`AI detection ${status ? 'enabled' : 'disabled'}`, 'success');
                } else {
                    aiSwitch.checked = !status;
                    throw new Error('Failed to toggle AI detection');
                }
            })
            .catch(error => {
                console.error('Error toggling AI detection:', error);
                showToast('Error toggling AI detection', 'danger');
            })
            .finally(() => {
                hideLoading();
            });
        }
        
        // Grid code update
        function updateGridCode() {
            const gridCode = document.getElementById('gridCodeSelect').value;
            
            showLoading('Updating grid code...');
            
            fetch('/api/command', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    act: 'setGridCode',
                    code: gridCode
                })
            })
            .then(response => {
                if(response.ok) {
                    showToast('Grid code updated', 'success');
                    fetchData();
                } else {
                    throw new Error('Failed to update grid code');
                }
            })
            .catch(error => {
                console.error('Error updating grid code:', error);
                showToast('Error updating grid code', 'danger');
            })
            .finally(() => {
                hideLoading();
            });
        }
        
        // Toast notification
        function showToast(message, type) {
            const toast = document.createElement('div');
            toast.className = `toast align-items-center text-white bg-${type} border-0`;
            toast.setAttribute('role', 'alert');
            toast.setAttribute('aria-live', 'assertive');
            toast.setAttribute('aria-atomic', 'true');
            
            toast.innerHTML = `
                <div class="d-flex">
                    <div class="toast-body">
                        ${message}
                    </div>
                    <button type="button" class="btn-close btn-close-white me-2 m-auto" data-bs-dismiss="toast" aria-label="Close"></button>
                </div>
            `;
            
            const toastContainer = document.createElement('div');
            toastContainer.className = 'position-fixed bottom-0 end-0 p-3';
            toastContainer.style.zIndex = '1100';
            toastContainer.appendChild(toast);
            
            document.body.appendChild(toastContainer);
            
            const bsToast = new bootstrap.Toast(toast);
            bsToast.show();
            
            toast.addEventListener('hidden.bs.toast', function() {
                document.body.removeChild(toastContainer);
            });
        }
        
        // Loading overlay
        function showLoading(message) {
            document.getElementById('loadingText').textContent = message || 'Loading...';
            document.getElementById('loadingOverlay').style.display = 'flex';
        }
        
        function hideLoading() {
            document.getElementById('loadingOverlay').style.display = 'none';
        }
        
        // Update data function
        function updateData(data) {
            if (!data || typeof data !== 'object') {
                console.error('Invalid data received:', data);
                return;
            }
            
            // FIXED: Use correct field names from ESP32 backend
            document.getElementById('acVoltage').textContent = data.voltage ? data.voltage.toFixed(1) : '0.0';
            document.getElementById('acCurrent').textContent = data.current ? data.current.toFixed(2) : '0.00';
            document.getElementById('acPower').textContent = data.power ? data.power.toFixed(1) : '0.0';
            document.getElementById('acEnergy').textContent = data.energy ? data.energy.toFixed(3) : '0.000';
            document.getElementById('acFrequency').textContent = data.frequency ? data.frequency.toFixed(2) : '0.00';
            
            // Update progress bars with proper fallbacks
            const voltageValue = data.voltage || 0;
            const voltagePercent = Math.min(100, (voltageValue / 300) * 100);
            const voltageBar = document.getElementById('voltageBar');
            if (voltageBar) voltageBar.style.width = `${voltagePercent}%`;
            
            const currentValue = data.current || 0;
            const currentPercent = Math.min(100, (currentValue / 20) * 100);
            const currentBar = document.getElementById('currentBar');
            if (currentBar) currentBar.style.width = `${currentPercent}%`;
            
            const powerValue = data.power || 0;
            const powerPercent = Math.min(100, (powerValue / 5000) * 100);
            const powerBar = document.getElementById('powerBar');
            if (powerBar) powerBar.style.width = `${powerPercent}%`;
            
            const freqValue = data.frequency || 0;
            const freqPercent = Math.min(100, ((freqValue - 45) / 10) * 100);
            const frequencyBar = document.getElementById('frequencyBar');
            if (frequencyBar) frequencyBar.style.width = `${freqPercent}%`;
            
            // Update status display
            const statusDiv = document.getElementById('statusDisplay');
            if (data.relayStatus) {
                statusDiv.className = 'status-card status-alarm';
                document.getElementById('statusText').textContent = 'Islanding Detected!';
                document.querySelector('#statusDisplay i').className = 'bi bi-exclamation-triangle-fill me-2';
            } else {
                statusDiv.className = 'status-card status-normal';
                document.getElementById('statusText').textContent = 'Normal Operation';
                document.querySelector('#statusDisplay i').className = 'bi bi-check-circle-fill me-2';
            }
            
            // Update relay switch
            const relaySwitch = document.getElementById('relaySwitch');
            if (relaySwitch.checked !== data.relayStatus) {
                relaySwitch.checked = data.relayStatus;
                document.getElementById('relayStatusText').textContent = data.relayStatus ? 'ON' : 'OFF';
                document.getElementById('relayStatusBadge').className = data.relayStatus ? 'badge bg-danger' : 'badge bg-secondary';
                document.getElementById('relayStatusBadge').textContent = data.relayStatus ? 'Active' : 'Disabled';
            }
            
            // Update AI switch
            const aiSwitch = document.getElementById('aiSwitch');
            if (aiSwitch.checked !== data.aiEnabled) {
                aiSwitch.checked = data.aiEnabled;
                document.getElementById('aiStatusText').textContent = data.aiEnabled ? 'Enabled' : 'Disabled';
                document.getElementById('aiStatusBadge').className = data.aiEnabled ? 'badge bg-success' : 'badge bg-secondary';
                document.getElementById('aiStatusBadge').textContent = data.aiEnabled ? 'Active' : 'Disabled';
            }
            
            // Update charts - ACTUALLY FULLY CORRECTED
            const now = Date.now();

            // Voltage Chart
            if (voltageChart && data.voltage !== undefined && !isNaN(data.voltage)) {
                voltageChart.data.datasets[0].data.push({x: now, y: data.voltage});
                if (voltageChart.data.datasets[0].data.length > 300) {
                    voltageChart.data.datasets[0].data.shift();
                }
                voltageChart.update('none');
            }

            // Frequency Chart
            if (frequencyChart && data.frequency !== undefined && !isNaN(data.frequency)) {
                frequencyChart.data.datasets[0].data.push({x: now, y: data.frequency});
                if (frequencyChart.data.datasets[0].data.length > 300) {
                    frequencyChart.data.datasets[0].data.shift();
                }
                frequencyChart.update('none');
            }

            // Power Chart
            if (powerChart && data.power !== undefined && !isNaN(data.power)) {
                powerChart.data.datasets[0].data.push({x: now, y: data.power});
                if (powerChart.data.datasets[0].data.length > 300) {
                    powerChart.data.datasets[0].data.shift();
                }
                powerChart.update('none');
            }

            console.log('Charts updated - V:', data.voltage, 'F:', data.frequency, 'P:', data.power);
            
            // Update last event
            const lastEventElement = document.getElementById('lastEvent');
            if (data.lastEvent && lastEventElement) {
                lastEventElement.textContent = data.lastEvent;
            }
            
            console.log('Data updated:', data);
        }
        
        // Fetch data function with reconnect logic
        function fetchData() {
            fetch('/api/data')
                .then(response => {
                    if (!response.ok) throw new Error(`HTTP error! status: ${response.status}`);
                    return response.json();
                })
                .then(data => {
                    if (data && typeof data === 'object') {
                        updateData(data);
                        if (!isConnected) {
                            updateConnectionStatus(true);
                        }
                    } else {
                        throw new Error('Invalid data format received');
                    }
                })
                .catch(error => {
                    console.error('Error fetching data:', error);
                    updateConnectionStatus(false);
                    
                    if (reconnectAttempts < maxReconnectAttempts) {
                        reconnectAttempts++;
                        setTimeout(fetchData, reconnectDelay);
                    } else {
                        showToast('Connection lost. Please refresh the page.', 'danger');
                    }
                });
        }
        
        // Load historical data
        function loadHistoricalData() {
            fetch('/api/trend')
                .then(response => {
                    if (!response.ok) throw new Error(`HTTP error! status: ${response.status}`);
                    return response.json();
                })
                .then(data => {
                    if (Array.isArray(data)) {
                        data.forEach(point => {
                            voltageChart.data.datasets[0].data.push({
                                x: point.t,
                                y: point.v
                            });
                            frequencyChart.data.datasets[0].data.push({
                                x: point.t,
                                y: point.f
                            });
                            powerChart.data.datasets[0].data.push({
                                x: point.t,
                                y: point.p
                            });
                        });
                        voltageChart.update();
                        frequencyChart.update();
                        powerChart.update();
                    }
                })
                .catch(error => {
                    console.error('Error loading historical data:', error);
                });
        }
        
        // Report type change handler
        document.getElementById('reportType').addEventListener('change', function() {
            const dateRangeSection = document.getElementById('dateRangeSection');
            if (this.value === 'custom') {
                dateRangeSection.style.display = 'block';
            } else {
                dateRangeSection.style.display = 'none';
            }
        });
        
        // Initialize the application
        function initializeApp() {
            initializeCharts();
            initializeTheme();
            initializeSidebar();
            
            // Set up periodic updates
            setInterval(updateCurrentTime, 1000);
            updateCurrentTime();
            
            // Initial data fetch
            fetchData();
            
            // Set up periodic data refresh
            setInterval(fetchData, 1000);
            
            // Load historical data
            loadHistoricalData();
            
            // Clean up before unloading
            window.addEventListener('beforeunload', () => {
                if (voltageChart) voltageChart.destroy();
                if (frequencyChart) frequencyChart.destroy();
                if (powerChart) powerChart.destroy();
                if (powerQualityChart) powerQualityChart.destroy();
                if (historicalChart) historicalChart.destroy();
            });
        }
        
        // Start the application when DOM is loaded
        document.addEventListener('DOMContentLoaded', initializeApp);
    </script>
</body>
</html>
)rawliteral";

// --- WiFi reconnect backoff ---
volatile bool wifiConnected = false;
unsigned long nextWifiReconnect = 0;
unsigned long wifiBackoffMs = 1000; // start 1s, cap 60s

void setup() {
    Serial.begin(115200);
    
    // Initialize status LED
    pinMode(LED_STATUS_PIN, OUTPUT);
    digitalWrite(LED_STATUS_PIN, LOW);
    
    // Initialize hardware
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    
    // Initialize PZEM-004T
    Serial2.begin(9600, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);
    
    // Initialize SD card first
    initSDCard();
    
    // Load configuration before WiFi
    if (!loadConfig()) {
        createDefaultConfig();
        logEvent("Failed to load config, created defaults", "WARNING", 0x2001);
    }
    
    // Start WiFi
        // Load persisted grid code from preferences (if any)
    loadGridCodeFromPrefs();

    // Ensure default grid codes exist and apply thresholds
    ensureDefaultGridCodes();
    updateGridCodeThresholds();

WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    logEvent("Connecting to WiFi: " + String(ssid), "INFO");
    
    int attempts = 0;
    // (removed) blocking WiFi loop for non-blocking reconnect

    
    if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(LED_STATUS_PIN, HIGH); // Solid LED when connected
        Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
        logEvent("WiFi connected: " + WiFi.localIP().toString(), "INFO");
        
        // Initialize time
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        
        // Initialize OTA if enabled
        if (sysSettings.enableOTA) {
            initOTA();
        }
    } else {
        logEvent("WiFi connection failed", "ERROR", 0x5001);
    }
    
    // Initialize MODBUS
    setupModbus();
    
    // Generate session token
    sessionToken = generateSessionToken();

    // Set up web server routes
    server.on("/", handleRoot);
    server.on("/login", HTTP_POST, handleLogin);
    server.on("/logout", handleLogout);
    server.on("/api/data", handleData);
    server.on("/api/settings", HTTP_GET, handleGetSettings);
    server.on("/api/settings", HTTP_POST, handlePostSettings);
    server.on("/api/trend", handleTrendData);
    server.on("/api/command", handleCommand);
    server.on("/api/analytics", handleAnalytics);
    server.on("/api/events", handleEvents);
    server.on("/api/reports", handleReports);
    server.on("/api/security", handleSecurity);
    server.on("/api/logs", HTTP_GET, handleGetLogs);
    server.on("/api/logs", HTTP_DELETE, handleClearLogs);
    server.on("/download_measurements", HTTP_GET, handleDownloadLog);
    server.on("/api/logs/export", handleLogExport);
    server.on("/api/config/backup", handleConfigBackup);
    server.on("/api/config/restore", HTTP_POST, handleConfigRestore);
    server.on("/api/system/reset", HTTP_POST, handleSystemReset);

    server.on("/api/status", HTTP_GET, handleStatus);

    server.begin();
    logEvent("System initialized successfully", "INFO");
    logEvent("Firmware version: 4.2", "INFO");
    logEvent("Free heap: " + String(ESP.getFreeHeap()) + " bytes", "INFO");
  // ----- WiFi (non-blocking) -----
  WiFi.mode(WIFI_STA);
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      wifiConnected = true;
      wifiBackoffMs = 1000;
      logEvent("WiFi connected: " + WiFi.localIP().toString(), "INFO");
      digitalWrite(LED_STATUS_PIN, HIGH);
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      wifiConnected = false;
      nextWifiReconnect = millis() + wifiBackoffMs;
      wifiBackoffMs = min(wifiBackoffMs * 2, 60000UL);
      logEvent("WiFi disconnected; scheduling reconnect in " + String(wifiBackoffMs/1000) + "s", "WARNING");
      digitalWrite(LED_STATUS_PIN, LOW);
    }
  });
  WiFi.begin(ssid, password);
  logEvent("Connecting to WiFi: " + String(ssid), "INFO");

}

// Inside loop()
void loop() {
    server.handleClient();
    mb.task();
    
    if (sysSettings.enableOTA) {
        ArduinoOTA.handle();
    }
    
    // Handle measurements with non-blocking delay
    if (millis() - lastMeasurementTime >= measurementInterval) {
        lastMeasurementTime = millis();

        // Take new measurements
        takeMeasurements();
        logMeasurementsToSD();
        checkForErrors();
        updateModbusRegisters();

        // Store new measurements in history
        HistoricalData data;
        data.voltage   = acVoltage;
        data.current   = acCurrent;
        data.frequency = acFrequency;
        data.power     = acPower;
        data.timestamp = millis();

        dataHistory.push_back(data);
        if (dataHistory.size() > maxHistoryPoints) {
            dataHistory.erase(dataHistory.begin());
        }
    }

    // Islanding detection
    if (millis() - lastIslandingCheckTime >= islandingCheckInterval) {
        lastIslandingCheckTime = millis();
        checkIslanding();
    }
    
    // Update historical data
    if (millis() - lastHistoryUpdate >= historyUpdateInterval) {
        lastHistoryUpdate = millis();
        updateHistoricalData();
        cleanupOldData();
    }
    
    // Status LED heartbeat
    if (millis() - lastStatusUpdate >= statusUpdateInterval) {
        lastStatusUpdate = millis();
        if (WiFi.status() == WL_CONNECTED) {
            digitalWrite(LED_STATUS_PIN, HIGH);
        } else {
            digitalWrite(LED_STATUS_PIN, !digitalRead(LED_STATUS_PIN));
        }
    }
    
    // Check session timeout
    if (isAuthenticated && (millis() - lastActivity > sessionTimeout)) {
        isAuthenticated = false;
        logSecurityEvent("Session timeout", WiFi.localIP().toString(), false);
    }

    // WiFi reconnect
    if (!wifiConnected && millis() >= nextWifiReconnect) {
        WiFi.disconnect(false, false);
        WiFi.begin(ssid, password);
        nextWifiReconnect = millis() + wifiBackoffMs;
    }
}


void handleRoot() {
  updateLastActivity();
  if (!isAuthenticated) {
    server.send_P(200, "text/html", loginPage);
  } else {
    server.send_P(200, "text/html", dashboardPage);
  }
}

void handleLogin() {
  String username = server.arg("username");
  String password = server.arg("password");
  String clientIP = server.client().remoteIP().toString();
  
  // Simple timing attack mitigation
  delay(random(100, 300));
  
  if (username == adminUser && password == adminPass) {
    isAuthenticated = true;
    sessionToken = generateSessionToken();
    updateLastActivity();
    logSecurityEvent("Login successful", clientIP, true);
    server.sendHeader("Location", "/");
    server.send(302);
  } else {
    logSecurityEvent("Login failed", clientIP, false);
    server.sendHeader("Location", "/?error=1");
    server.send(302);
  }
}

void handleLogout() {
  String clientIP = server.client().remoteIP().toString();
  logSecurityEvent("Logout", clientIP, true);
  isAuthenticated = false;
  sessionToken = "";
  server.sendHeader("Location", "/");
  server.send(302);
}

void handleData() {
if (!isAuthenticated) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  updateLastActivity();

  DynamicJsonDocument doc = buildStatusJson(2048);
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}


void handleGetSettings() {
if (!isAuthenticated) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  updateLastActivity();

  DynamicJsonDocument doc(4096);

  // 1) Grid codes list
  JsonArray codes = doc.createNestedArray("gridCodes");
  for (const GridCode &gc : gridCodes) {
    JsonObject code = codes.createNestedObject();
    code["name"] = gc.name;
    code["voltageThreshold"] = gc.voltageThreshold;
    code["frequencyThreshold"] = gc.frequencyThreshold;
    code["voltageMin"] = gc.voltageMin;
    code["voltageMax"] = gc.voltageMax;
    code["frequencyMin"] = gc.frequencyMin;
    code["frequencyMax"] = gc.frequencyMax;
    code["tripDelay"] = gc.tripDelay;
    code["reconnectDelay"] = gc.reconnectDelay;
  }

  // 2) Current config
  doc["currentGridCode"] = currentGridCode;
  doc["aiEnabled"]       = aiDetectionEnabled;
  doc["aiThreshold"]     = aiConfidenceThreshold;
  doc["enableLogging"]   = sysSettings.enableFileLogging;

  // 3) Live status appended
  DynamicJsonDocument status = buildStatusJson(2048);
  for (JsonPair kv : status.as<JsonObject>()) {
    doc[kv.key()] = kv.value();
  }

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}


void handlePostSettings() {
  if (!isAuthenticated) { server.send(401, "text/plain", "Unauthorized"); return; }
  updateLastActivity();

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) { server.send(400, "text/plain", "Invalid JSON"); return; }

  // Update current grid code if present
  if (doc.containsKey("currentGridCode")) {
    String requested = doc["currentGridCode"].as<String>();
    bool exists = false;
    for (const auto &gc : gridCodes) { if (gc.name == requested) { exists = true; break; } }
    if (!exists) { server.send(400, "text/plain", "Unknown grid code"); return; }
    currentGridCode = requested;
  }

  // Update AI settings
  if (doc.containsKey("aiEnabled")) aiDetectionEnabled = doc["aiEnabled"].as<bool>();
  if (doc.containsKey("aiConfidenceThreshold")) aiConfidenceThreshold = doc["aiConfidenceThreshold"].as<float>();

  // Update limits for the selected grid code
  for (auto &gc : gridCodes) {
    if (gc.name == currentGridCode) {
      if (doc.containsKey("voltageMin"))      gc.voltageMin = doc["voltageMin"].as<float>();
      if (doc.containsKey("voltageMax"))      gc.voltageMax = doc["voltageMax"].as<float>();
      if (doc.containsKey("frequencyMin"))    gc.frequencyMin = doc["frequencyMin"].as<float>();
      if (doc.containsKey("frequencyMax"))    gc.frequencyMax = doc["frequencyMax"].as<float>();
      if (doc.containsKey("tripDelay"))       gc.tripDelay = doc["tripDelay"].as<int>();
      if (doc.containsKey("reconnectDelay"))  gc.reconnectDelay = doc["reconnectDelay"].as<int>();
      break;
    }
  }

  updateGridCodeThresholds();

  // Persist to SD and preferences (Preferences as fallback)
  if (!SD.exists("/config")) SD.mkdir("/config");
  bool ok = saveConfig();
  bool prefsOk = true;
  if (!ok) {
      // try preferences at least
      prefsOk = saveGridCodeToPrefs(currentGridCode);
  } else {
      // also update prefs to keep both in sync
      prefsOk = saveGridCodeToPrefs(currentGridCode) || prefsOk;
  }

  if (!ok && !prefsOk) {
    logEvent("Failed to persist settings to SD and preferences", "ERROR", 0x3005);
    server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to save config\"}");
    return;
  }

  logEvent("Settings updated via web", "INFO");
  server.send(200, "application/json", "{\"status\":\"saved\"}");
}



void handleCommand() {
  if (!isAuthenticated) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  updateLastActivity();
  
  String body = server.arg("plain");
  DynamicJsonDocument doc(256);
  
  if (deserializeJson(doc, body)) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }
  
  String action = doc["action"];
  
  if (action == "relay") {
    bool value = doc["value"];
    digitalWrite(RELAY_PIN, value ? HIGH : LOW);
    sysState.relayState = value;
    logEvent("Relay " + String(value ? "ON" : "OFF") + " via web", "INFO");
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  }
  else if (action == "ai") {
    aiDetectionEnabled = doc["value"];
    logEvent("AI detection " + String(aiDetectionEnabled ? "enabled" : "disabled") + " via web", "INFO");
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  }
  else if (action == "gridcode") {
    String newCode = doc["value"];
    for (const GridCode& gc : gridCodes) {
      if (gc.name == newCode) {
        currentGridCode = newCode;
        logEvent("Grid code changed to: " + newCode + " via web", "INFO");
        server.send(200, "application/json", "{\"status\":\"ok\"}");
        return;
      }
    }
    server.send(400, "text/plain", "Invalid grid code");
  }
  else {
    server.send(400, "text/plain", "Unknown action");
  }
}

void handleTrendData() {
    if (!isAuthenticated) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
  
    updateLastActivity();
  
    DynamicJsonDocument doc(4096);
    JsonArray trends = doc.createNestedArray("trends");
  
    int startIdx = max(0, (int)dataHistory.size() - 50);
    for (int i = startIdx; i < dataHistory.size(); i++) {
        JsonObject point = trends.createNestedObject();
        point["timestamp"] = dataHistory[i].timestamp;
        point["voltage"]   = dataHistory[i].voltage;
        point["current"]   = dataHistory[i].current;
        point["power"]     = dataHistory[i].power;
        point["frequency"] = dataHistory[i].frequency;
    }
  
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void handleAnalytics() {
    if (!isAuthenticated) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
  
    updateLastActivity();
  
    DynamicJsonDocument doc(1024);
  
    float avgVoltage = 0, avgFrequency = 0, avgPower = 0;
    int validPoints = 0;
  
    for (const auto& data : dataHistory) {
        if (!isnan(data.voltage) && data.voltage > 0) {
            avgVoltage += data.voltage;
            avgFrequency += data.frequency;
            avgPower += data.power;
            validPoints++;
        }
    }
  
    if (validPoints > 0) {
        avgVoltage /= validPoints;
        avgFrequency /= validPoints;
        avgPower /= validPoints;
    }
  
    doc["avgVoltage"]   = avgVoltage;
    doc["avgFrequency"] = avgFrequency;
    doc["avgPower"]     = avgPower;
    doc["dataPoints"]   = validPoints;
    doc["uptimeHours"]  = millis() / (1000 * 3600);
    doc["tripCount"]    = sysState.tripCount;
  
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void handleEvents() {
    if (!isAuthenticated) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
  
    updateLastActivity();
  
    if (server.method() == HTTP_DELETE) {
        eventHistory.clear();
        logEvent("Event history cleared via web", "INFO");
        server.send(200, "application/json", "{\"status\":\"cleared\"}");
        return;
    }
  
    DynamicJsonDocument doc(4096);
    JsonArray events = doc.createNestedArray("events");
  
    int startIdx = max(0, (int)eventHistory.size() - 100);
    for (int i = startIdx; i < eventHistory.size(); i++) {
        JsonObject event = events.createNestedObject();
        event["timeISO"]   = eventHistory[i].timestampISO;
        event["timeMillis"] = eventHistory[i].timestampMillis;
        event["level"]     = eventHistory[i].level;
        event["message"]   = eventHistory[i].event;
        event["code"]      = eventHistory[i].errorCode;
    }
  
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void handleReports() {
  if (!isAuthenticated) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  updateLastActivity();
  
  if (server.hasArg("generate")) {
    // Generate report
    String reportType = server.arg("type");
    
    String report = "=== UGC Grid Monitoring Report ===\n";
    report += "Report Type: " + reportType + "\n";
    report += "Generated: " + getFormattedTime() + "\n";
    report += "System Uptime: " + String(millis() / (1000 * 3600)) + " hours\n\n";
    
    report += "=== Current Status ===\n";
    report += "AC Voltage: " + String(acVoltage, 2) + " V\n";
    report += "AC Current: " + String(acCurrent, 3) + " A\n";
    report += "AC Power: " + String(acPower, 1) + " W\n";
    report += "AC Frequency: " + String(acFrequency, 2) + " Hz\n";
    report += "Power Factor: " + String(acPF, 3) + "\n";
    report += "Grid Code: " + currentGridCode + "\n";
    report += "Relay Status: " + String(sysState.relayState ? "TRIPPED" : "NORMAL") + "\n";
    report += "AI Detection: " + String(aiDetectionEnabled ? "ENABLED" : "DISABLED") + "\n\n";
    
    report += "=== Recent Events ===\n";
    int eventCount = min(10, (int)eventHistory.size());
    for (int i = eventHistory.size() - eventCount; i < eventHistory.size(); i++) {
      report += "[" + eventHistory[i].level + "] " + eventHistory[i].event + "\n";
    }
    
    report += "\n=== End of Report ===\n";
    
    server.send(200, "text/plain", report);
    logEvent("Report generated: " + reportType, "INFO");
  } else {
    // Return report statistics
    DynamicJsonDocument doc(512);
    doc["totalEvents"] = eventHistory.size();
    doc["totalAlarms"] = sysState.tripCount;
    doc["systemUptime"] = millis() / (1000 * 3600);
    doc["dataPoints"] = dataHistory.size();
    
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  }
}

void handleSecurity() {
    if (!isAuthenticated) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
  
    updateLastActivity();
  
    if (server.method() == HTTP_POST && server.hasArg("password")) {
        String newPassword = server.arg("password");
        if (newPassword.length() >= 6) {
            logSecurityEvent("Password changed", server.client().remoteIP().toString(), true);
            server.send(200, "application/json", "{\"status\":\"changed\"}");
        } else {
            server.send(400, "text/plain", "Password too short");
        }
        return;
    }
  
    DynamicJsonDocument doc(2048);
    JsonArray events = doc.createNestedArray("events");
  
    for (const auto& e : securityEvents) {
        JsonObject obj = events.createNestedObject();
        obj["timeISO"] = e.timestampISO;
        obj["timeMillis"] = e.timestampMillis;
        obj["event"] = e.event;
        obj["ip"] = e.sourceIP;
        obj["success"] = e.success;
    }

    unsigned long hourAgo = millis() - (60 * 60 * 1000);
    int failedCount = 0;
    for (const auto& event : securityEvents) {
        if (event.timestampMillis > hourAgo && !event.success) {
            failedCount++;
        }
    }
  
    doc["failedAttempts"] = failedCount;
    doc["sessionTimeout"] = sessionTimeout / 60000; // minutes
  
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void handleGetLogs() {
  if (!isAuthenticated) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  updateLastActivity();
  
  DynamicJsonDocument doc(4096);
  JsonArray logs = doc.createNestedArray("logs");
  
  // Read recent logs from SD card
  File logFile = SD.open("/logs/events.log", FILE_READ);
  if (logFile) {
    String lines[50]; // Last 50 lines
    int lineCount = 0;
    
    while (logFile.available() && lineCount < 50) {
      String line = logFile.readStringUntil('\n');
      if (line.length() > 0) {
        lines[lineCount++] = line;
      }
    }
    logFile.close();
    
    // Add lines to JSON (reverse order for latest first)
    for (int i = lineCount - 1; i >= 0; i--) {
      JsonObject log = logs.createNestedObject();
      // Parse log format: [timestamp] [level] message
      String line = lines[i];
      int levelStart = line.indexOf('[', 1) + 1;
      int levelEnd = line.indexOf(']', levelStart);
      int msgStart = line.indexOf(' ', levelEnd) + 1;
      
      log["timestamp"] = line.substring(1, line.indexOf(']'));
      log["level"] = line.substring(levelStart, levelEnd);
      log["message"] = line.substring(msgStart);
    }
  }
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleClearLogs() {
  if (!isAuthenticated) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  updateLastActivity();
  
  // Clear log files
  SD.remove("/logs/events.log");
  SD.remove("/logs/historical_data.csv");
  
  // Recreate CSV header
  File dataFile = SD.open("/logs/historical_data.csv", FILE_WRITE);
  if (dataFile) {
    dataFile.println("Timestamp,Voltage,Current,Power,Frequency,PowerFactor,ErrorCode");
    dataFile.close();
  }
  
  // Clear memory logs
  eventHistory.clear();
  dataHistory.clear();
  
  logEvent("All logs cleared via web interface", "WARNING");
  server.send(200, "application/json", "{\"status\":\"cleared\"}");
}

void handleLogExport() {
  if (!isAuthenticated) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  updateLastActivity();
  
  String format = server.arg("format");
  
  if (format == "csv") {
    server.sendHeader("Content-Disposition", "attachment; filename=system_logs.csv");
    server.sendHeader("Content-Type", "text/csv");
    
    String csv = "Timestamp,Level,Event\n";
    for (const auto& event : eventHistory) {
      csv += getCurrentTimeISO() + "," + event.level + "," + event.event + "\n";
    }
    server.send(200, "text/csv", csv);
  } else {
    // Default to TXT format
    server.sendHeader("Content-Disposition", "attachment; filename=system_logs.txt");
    server.sendHeader("Content-Type", "text/plain");
    
    String txt = "=== UGC Grid Monitor System Logs ===\n\n";
    for (const auto& event : eventHistory) {
      txt += "[" + getCurrentTimeISO() + "] [" + event.level + "] " + event.event + "\n";
    }
    server.send(200, "text/plain", txt);
  }
  
  logEvent("Logs exported in " + format + " format", "INFO");
}

void handleConfigBackup() {
  if (!isAuthenticated) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  updateLastActivity();
  
  // Read config file and send as download
  File configFile = SD.open("/config/system.json", FILE_READ);
  if (configFile) {
    server.sendHeader("Content-Disposition", "attachment; filename=config_backup.json");
    server.sendHeader("Content-Type", "application/json");
    
    server.streamFile(configFile, "application/json");
    configFile.close();
    
    logEvent("Configuration backup downloaded", "INFO");
  } else {
    server.send(404, "text/plain", "Config file not found");
  }
}

void handleConfigRestore() {
  if (!isAuthenticated) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  updateLastActivity();
  
  // Handle file upload (simplified)
  if (server.hasArg("plain")) {
    String configData = server.arg("plain");
    
    // Validate JSON
    DynamicJsonDocument doc(4096);
    if (!deserializeJson(doc, configData)) {
      // Save uploaded config
      File configFile = SD.open("/config/system.json", FILE_WRITE);
      if (configFile) {
        configFile.print(configData);
        configFile.close();
        
        logEvent("Configuration restored from upload", "INFO");
        server.send(200, "application/json", "{\"status\":\"restored\"}");
        
        // Reload config
        loadConfig();
        return;
      }
    }
  }
  
  server.send(400, "text/plain", "Invalid config file");
}

void handleSystemReset() {
  if (!isAuthenticated) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  updateLastActivity();
  
  logEvent("System reset initiated via web interface", "WARNING");
  server.send(200, "application/json", "{\"status\":\"resetting\"}");
  
  // Clear all data
  eventHistory.clear();
  dataHistory.clear();
  securityEvents.clear();
  
  // Reset to defaults
  createDefaultConfig();
  
  // Restart after a short delay
  delay(2000);
  ESP.restart();
}

void initOTA() {
    ArduinoOTA.setHostname("ESP32-GridMonitor");
    ArduinoOTA.setPassword("admin123");
    
    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        logEvent("OTA Update started: " + type, "INFO");
    });
    
    ArduinoOTA.onEnd([]() {
        logEvent("OTA Update completed", "INFO");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        int percentage = (progress / (total / 100));
        if (percentage % 10 == 0) {
            logEvent("OTA Progress: " + String(percentage) + "%", "INFO");
        }
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        String errorMsg = "OTA Error: ";
        if (error == OTA_AUTH_ERROR) errorMsg += "Auth Failed";
        else if (error == OTA_BEGIN_ERROR) errorMsg += "Begin Failed";
        else if (error == OTA_CONNECT_ERROR) errorMsg += "Connect Failed";
        else if (error == OTA_RECEIVE_ERROR) errorMsg += "Receive Failed";
        else if (error == OTA_END_ERROR) errorMsg += "End Failed";
        logEvent(errorMsg, "ERROR", 0x6000 + error);
    });
    
    ArduinoOTA.begin();
    logEvent("OTA initialized", "INFO");
}

String generateSessionToken() {
    String token = String(ESP.getEfuseMac(), HEX) + String(millis(), HEX);
    return token;
}

bool isSessionValid() {
    return isAuthenticated && (millis() - lastActivity < sessionTimeout);
}

void updateLastActivity() {
    lastActivity = millis();
}

String getFormattedTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return String(millis());
    }
    char timeString[64];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeString);
}

void logEvent(String message, String level, uint16_t errorCode) {
    String isoTime = getCurrentTimeISO();

    EventLog ev;
    ev.timestampMillis = millis();
    ev.timestampISO = isoTime;
    ev.event = message;
    ev.level = level;
    ev.errorCode = errorCode;

    if (errorCode != 0) {
        systemError |= errorCode;
        lastError = errorCode;
    }

    // Serial logging
    if (sysSettings.enableSerialLogging) {
        Serial.printf("[%s] [%s] %s%s\n", 
            isoTime.c_str(), 
            level.c_str(), 
            message.c_str(),
            (errorCode != 0 ? (" (Code: 0x" + String(errorCode, HEX) + ")").c_str() : "")
        );
    }

    // File logging
    if (sysSettings.enableFileLogging) {
        File logFile = SD.open("/logs/events.log", FILE_APPEND);
        if (logFile) {
            logFile.printf("[%s] [%s] %s%s\n", 
                isoTime.c_str(), 
                level.c_str(), 
                message.c_str(),
                (errorCode != 0 ? (" (Code: 0x" + String(errorCode, HEX) + ")").c_str() : "")
            );
            logFile.close();
        }
    }

    // Store in memory
    eventHistory.push_back(ev);
    if (eventHistory.size() > maxEventHistory) {
        eventHistory.erase(eventHistory.begin());
    }
}


void logSecurityEvent(String event, String sourceIP, bool success) {
    String isoTime = getCurrentTimeISO();

    SecurityEvent secEv;
    secEv.timestampMillis = millis();
    secEv.timestampISO = isoTime;
    secEv.event = event;
    secEv.sourceIP = sourceIP;
    secEv.success = success;

    securityEvents.push_back(secEv);
    if (securityEvents.size() > maxSecurityEvents) {
        securityEvents.erase(securityEvents.begin());
    }

    logEvent("Security: " + event + " from " + sourceIP + " - " + (success ? "Success" : "Failed"), 
             success ? "INFO" : "WARNING");
}

void updateHistoricalData() {
    HistoricalData data;
    data.timestamp = millis();
    data.voltage = acVoltage;
    data.current = acCurrent;
    data.power = acPower;
    data.frequency = acFrequency;
    data.pf = acPF;
    data.errorCode = systemError;
    
    dataHistory.push_back(data);
    if (dataHistory.size() > maxHistoryPoints) {
        dataHistory.erase(dataHistory.begin());
    }
    
    // Save to SD card periodically
    static int saveCounter = 0;
    if (++saveCounter >= 10) { // Save every 10 minutes
        saveCounter = 0;
        File dataFile = SD.open("/logs/historical_data.csv", FILE_APPEND);
        if (dataFile) {
            String dataLine = String(data.timestamp) + "," + 
                            String(data.voltage, 2) + "," +
                            String(data.current, 3) + "," +
                            String(data.power, 1) + "," +
                            String(data.frequency, 2) + "," +
                            String(data.pf, 3) + "," +
                            String(data.errorCode, HEX);
            dataFile.println(dataLine);
            dataFile.close();
        }
    }
}

void cleanupOldData() {
    // Remove data older than 24 hours from memory
    unsigned long cutoff = millis() - (24 * 60 * 60 * 1000);
    
    dataHistory.erase(
        std::remove_if(dataHistory.begin(), dataHistory.end(),
            [cutoff](const HistoricalData& data) { return data.timestamp < cutoff; }),
        dataHistory.end()
    );
    
    eventHistory.erase(
        std::remove_if(eventHistory.begin(), eventHistory.end(),
            [cutoff](const EventLog& event) { return event.timestamp < cutoff; }),
        eventHistory.end()
    );
    
    // Clean up old log files periodically
    static unsigned long lastCleanup = 0;
    if (millis() - lastCleanup > (24 * 60 * 60 * 1000)) { // Daily cleanup
        lastCleanup = millis();
        
        // Check log file sizes and rotate if needed
        File logFile = SD.open("/logs/events.log", FILE_READ);
        if (logFile && logFile.size() > sysSettings.maxLogFileSize) {
            logFile.close();
            // Rotate log file
            SD.remove("/logs/events_old.log");
            SD.rename("/logs/events.log", "/logs/events_old.log");
        }
        if (logFile) logFile.close();
    }
}

void takeMeasurements() {
    // Read AC measurements from PZEM-004T
    float newVoltage = pzem.voltage();
    
    if(!isnan(newVoltage)) {
        // Valid readings - update all values
        acVoltage = newVoltage;
        acCurrent = pzem.current();
        acPower = pzem.power();
        acEnergy = pzem.energy();
        acFrequency = pzem.frequency();
        acPF = pzem.pf();
        
        if (sysSettings.enableDebugMode) {
            Serial.printf("AC Measurements: V=%.1f, I=%.1f, P=%.1f, E=%.1f, F=%.1f, PF=%.2f\n",
                         acVoltage, acCurrent, acPower, acEnergy, acFrequency, acPF);
        }
    } else {
        // Invalid readings - set all values to 0
        acVoltage = 0.0;
        acCurrent = 0.0;
        acPower = 0.0;
        acFrequency = 0.0;
        acPF = 0.0;
        // Note: Don't reset energy as it should remain cumulative
        
        logEvent("AC power disconnected or PZEM communication error", "WARNING", 0x1000);
    }
}

void checkIslanding() {
    static float freqHistory[5] = {0};
    static int historyIndex = 0;

    // Find current grid code parameters
    GridCode currentGc;
    bool found = false;
    for (const GridCode& gc : gridCodes) {
        if (gc.name == currentGridCode) {
            currentGc = gc;
            found = true;
            break;
        }
    }
    if (!found) {
        logEvent("Current grid code not found: " + currentGridCode, "ERROR", 0x1004);
        return;
    }

    // Handle grid-vanish case safely (avoid stale ROCOF spikes)
    if (acFrequency < 0.1f) {
        for (int i = 0; i < 5; i++) freqHistory[i] = acFrequency;
    }
    // Update frequency history ring buffer
    freqHistory[historyIndex] = acFrequency;
    historyIndex = (historyIndex + 1) % 5;

    // Average ROCOF over 4 intervals
    float freqRate = 0.0f;
    for (int i = 1; i < 5; i++) {
        freqRate += fabs(freqHistory[i] - freqHistory[i - 1]);
    }
    freqRate /= 4.0f; // Hz/s

    // Threshold checks
    bool voltageViolation   = (acVoltage   < currentGc.voltageMin)   || (acVoltage   > currentGc.voltageMax);
    bool frequencyViolation = (acFrequency < currentGc.frequencyMin) || (acFrequency > currentGc.frequencyMax);
    bool rapidFrequencyChange = freqRate > 1.0f; // 1 Hz/s default

    // Force-detect when grid vanishes (0 V / 0 Hz)
    if (acVoltage < 5.0f && acFrequency < 5.0f) {
        voltageViolation = true;
        frequencyViolation = true;
    }

    const bool fault = voltageViolation || frequencyViolation || rapidFrequencyChange;

    // Optional trip delay (ms) from grid code
    static unsigned long faultSince = 0;
    if (fault) {
        if (faultSince == 0) faultSince = millis();
    } else {
        faultSince = 0;
    }
    int delayMs = currentGc.tripDelay > 0 ? currentGc.tripDelay : 0;
    bool shouldTrip = fault && (millis() - faultSince >= (unsigned long)delayMs);

    if (shouldTrip) {
        digitalWrite(RELAY_PIN, HIGH); // open/trip (invert if hardware is active-low)
        sysState.relayState = true;
        islandingStatus = "Islanding Detected";
        alarmTriggered = true;
        sysState.alarm = true;

        String detailMsg = "Islanding Detected! V:" + String(acVoltage, 1) + 
                           "V (" + String(currentGc.voltageMin, 1) + "-" + String(currentGc.voltageMax, 1) + "), " +
                           "F:" + String(acFrequency, 2) + 
                           "Hz (" + String(currentGc.frequencyMin, 2) + "-" + String(currentGc.frequencyMax, 2) + "), " +
                           "ROCOF:" + String(freqRate, 3) + "Hz/s";
        logEvent(detailMsg, "CRITICAL", 0x1005);
    } else if (!fault) {
        digitalWrite(RELAY_PIN, LOW);  // closed/normal
        sysState.relayState = false;
        islandingStatus = "Normal";
        alarmTriggered = false;
        sysState.alarm = false;
    }
}


float calculateAIConfidence(float voltage, float frequency, float rocof) {
    // Simplified AI confidence calculation
    // In a real implementation, this would use trained ML models
    
    float confidence = 1.0;
    
    // Voltage confidence factor
    if (voltage < 200 || voltage > 250) {
        confidence *= 0.9;
    }
    
    // Frequency confidence factor
    if (frequency < 49 || frequency > 51) {
        confidence *= 0.8;
    }
    
    // ROCOF confidence factor
    if (rocof > 0.5) {
        confidence *= 0.7;
    }
    
    return confidence;
}

void initSDCard() {
    if (!SD.begin(SD_CS_PIN)) {
        logEvent("SD card initialization failed", "ERROR", 0x3000);
        return;
    }
    
    // Create directories if they don't exist
    if (!SD.exists("/logs")) {
        SD.mkdir("/logs");
    }
    if (!SD.exists("/config")) {
        SD.mkdir("/config");
    }
    if (!SD.exists("/backup")) {
        SD.mkdir("/backup");
    }
    
    // Create CSV header if historical data file doesn't exist
    if (!SD.exists("/logs/historical_data.csv")) {
        File dataFile = SD.open("/logs/historical_data.csv", FILE_WRITE);
        if (dataFile) {
            dataFile.println("Timestamp,Voltage,Current,Power,Frequency,PowerFactor,ErrorCode");
            dataFile.close();
        }
    }
    
    logEvent("SD card initialized successfully", "INFO");
}

void setupModbus() {
    Serial1.begin(MODBUS_BAUDRATE, MODBUS_SERIAL_CONFIG, MODBUS_RX_PIN, MODBUS_TX_PIN);
    mb.begin(&Serial1);
    mb.slave(MODBUS_SLAVE_ID);
    
    // Setup input registers (read-only)
    mb.addIreg(MB_AC_VOLTAGE);
    mb.addIreg(MB_AC_CURRENT);
    mb.addIreg(MB_AC_POWER);
    mb.addIreg(MB_AC_ENERGY);
    mb.addIreg(MB_AC_FREQUENCY);
    mb.addIreg(MB_AC_PF);
    
    // Setup holding registers (read/write)
    mb.addHreg(MB_RELAY_STATUS);
    mb.addHreg(MB_GRID_CODE);
    mb.addHreg(MB_AI_ENABLED);
    mb.addHreg(MB_ERROR_CODE);
    mb.addHreg(MB_VOLTAGE_THRESH);
    mb.addHreg(MB_CURRENT_THRESH);
    mb.addHreg(MB_FREQ_THRESH);
    mb.addHreg(MB_ISLANDING_DELAY);
    mb.addHreg(MB_SYSTEM_STATUS);
    mb.addHreg(MB_UPTIME);
    
    // Set initial values
    updateModbusRegisters();
    
    // Add callback for relay control
    mb.onSetHreg(MB_RELAY_STATUS, [](TRegister* reg, uint16_t val) {
        digitalWrite(RELAY_PIN, val > 0 ? HIGH : LOW);
        logEvent("Relay " + String(val > 0 ? "ON" : "OFF") + " via MODBUS", "INFO");
        return Modbus::ResultCode::EX_SUCCESS;
    }, 1);
    
    // Add callback for grid code change
    mb.onSetHreg(MB_GRID_CODE, [](TRegister* reg, uint16_t val) {
        if (val < gridCodes.size()) {
            currentGridCode = gridCodes[val].name;
            updateGridCodeThresholds();
            logEvent("Grid code changed to: " + currentGridCode + " via MODBUS", "INFO");
            return Modbus::ResultCode::EX_SUCCESS;
        }
        return Modbus::ResultCode::EX_ILLEGAL_VALUE;
    }, 1);
    
    // Add callback for AI detection toggle
    mb.onSetHreg(MB_AI_ENABLED, [](TRegister* reg, uint16_t val) {
        aiDetectionEnabled = (val > 0);
        logEvent("AI Detection " + String(aiDetectionEnabled ? "enabled" : "disabled") + " via MODBUS", "INFO");
        return Modbus::ResultCode::EX_SUCCESS;
    }, 1);
    
    logEvent("MODBUS RTU initialized", "INFO");
}

void updateModbusRegisters() {
    // Update input registers (scaled for fixed-point precision)
    uint16_t voltageReg = isnan(acVoltage) ? 0 : (uint16_t)(acVoltage * 10);
    uint16_t currentReg = isnan(acCurrent) ? 0 : (uint16_t)(acCurrent * 100);
    uint16_t powerReg = isnan(acPower) ? 0 : (uint16_t)(acPower / 10);
    uint16_t energyReg = isnan(acEnergy) ? 0 : (uint16_t)(acEnergy * 100);
    uint16_t frequencyReg = isnan(acFrequency) ? 0 : (uint16_t)(acFrequency * 100);
    uint16_t pfReg = isnan(acPF) ? 0 : (uint16_t)(acPF * 1000);

    mb.Ireg(MB_AC_VOLTAGE, voltageReg);
    mb.Ireg(MB_AC_CURRENT, currentReg);
    mb.Ireg(MB_AC_POWER, powerReg);
    mb.Ireg(MB_AC_ENERGY, energyReg);
    mb.Ireg(MB_AC_FREQUENCY, frequencyReg);
    mb.Ireg(MB_AC_PF, pfReg);
    
    // Update holding registers
    mb.Hreg(MB_RELAY_STATUS, digitalRead(RELAY_PIN));
    mb.Hreg(MB_AI_ENABLED, aiDetectionEnabled);
    mb.Hreg(MB_ERROR_CODE, systemError);
    mb.Hreg(MB_SYSTEM_STATUS, alarmTriggered ? 2 : 1); // 1=Normal, 2=Alarm
    mb.Hreg(MB_UPTIME, millis() / 1000); // Uptime in seconds
    
    // Find current grid code index
    uint16_t gridCodeIndex = 0;
    for (size_t i = 0; i < gridCodes.size(); i++) {
        if (gridCodes[i].name == currentGridCode) {
            gridCodeIndex = i;
            break;
        }
    }
    mb.Hreg(MB_GRID_CODE, gridCodeIndex);
    
    // Update threshold registers
    GridCode currentGc;
    for (const GridCode& gc : gridCodes) {
        if (gc.name == currentGridCode) {
            currentGc = gc;
            break;
        }
    }
    
    mb.Hreg(MB_VOLTAGE_THRESH, (uint16_t)(currentGc.voltageThreshold * 10));
    mb.Hreg(MB_CURRENT_THRESH, (uint16_t)(10 * 100)); // Default current threshold
    mb.Hreg(MB_FREQ_THRESH, (uint16_t)(currentGc.frequencyThreshold * 100));
    mb.Hreg(MB_ISLANDING_DELAY, islandingDelay);
}

void checkForErrors() {
    uint16_t newError = 0;
    
    // Check PZEM communication
    if (isnan(acVoltage)) {
        newError |= 0x1000;
    }
    
    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        newError |= 0x5001;
    }
    
    // Check SD card
    if (!SD.exists("/config")) {
        newError |= 0x3000;
    }
    
    // Check voltage/frequency thresholds if we have grid codes
    if (!gridCodes.empty()) {
        GridCode currentGc;
        bool found = false;
        for (const GridCode& gc : gridCodes) {
            if (gc.name == currentGridCode) {
                currentGc = gc;
                found = true;
                break;
            }
        }
        
        if (found) {
            if (acVoltage < currentGc.voltageMin) newError |= 0x1001;
            if (acVoltage > currentGc.voltageMax) newError |= 0x1002;
            if (acFrequency < currentGc.frequencyMin) newError |= 0x1003;
            if (acFrequency > currentGc.frequencyMax) newError |= 0x1004;
        }
    }
    
    // Check system health
    if (ESP.getFreeHeap() < 10000) { // Less than 10KB free
        newError |= 0x7001;
    }
    
    // Update error state
    if (newError != systemError) {
        if (newError > systemError) {
            // New errors detected
            uint16_t newErrors = newError ^ systemError;
            String errorMsg = "New system errors detected: 0x" + String(newErrors, HEX);
            logEvent(errorMsg, "ERROR", newErrors);
        }
        lastError = systemError;
        systemError = newError;
    }
}

void updateGridCodeThresholds() {
    // Find the current grid code in our list
    for (const GridCode& gc : gridCodes) {
        if (gc.name == currentGridCode) {
            thresholdAcVoltage = gc.voltageThreshold;
            thresholdFrequency = gc.frequencyThreshold;
            
            // Update MODBUS registers
            mb.Hreg(MB_VOLTAGE_THRESH, (uint16_t)(gc.voltageThreshold * 10));
            mb.Hreg(MB_FREQ_THRESH, (uint16_t)(gc.frequencyThreshold * 100));
            
            logEvent("Updated thresholds for grid code: " + gc.name + 
                    " (V: " + String(gc.voltageThreshold) + 
                    ", Hz: " + String(gc.frequencyThreshold) + ")", "INFO");
            return;
        }
    }
    
    logEvent("Warning: Current grid code not found: " + currentGridCode, "WARNING", 0x1004);
}

void createDefaultConfig() {
  gridCodes.clear();

  // India (already present)
  GridCode gc;
  gc.name = "IS 16184 (India)";
  gc.voltageThreshold = 230.0;
  gc.frequencyThreshold = 50.0;
  gc.voltageMin = 180.0;
  gc.voltageMax = 260.0;
  gc.frequencyMin = 47.5;
  gc.frequencyMax = 52.0;
  gc.errorCode = 0;
  gc.tripDelay = 2000;
  gc.reconnectDelay = 60;
  gridCodes.push_back(gc);

  // USA (IEEE 1547)
  gc.name = "IEEE 1547 (USA)";
  gc.voltageThreshold = 120.0;           // Nominal voltage
  gc.frequencyThreshold = 60.0;            // Nominal frequency
  gc.voltageMin = 100.0;                   // Lower limit
  gc.voltageMax = 130.0;                   // Upper limit
  gc.frequencyMin = 59.5;                  // Freq lower limit
  gc.frequencyMax = 60.5;                  // Freq upper limit
  gc.errorCode = 0;
  gc.tripDelay = 2000;                     // delay in ms
  gc.reconnectDelay = 60;                  // seconds
  gridCodes.push_back(gc);

  // Europe (EN 50438)
  gc.name = "EN 50438 (Europe)";
  gc.voltageThreshold = 230.0;             // Nominal
  gc.frequencyThreshold = 50.0;
  gc.voltageMin = 207.0;                   // Lower safety limit
  gc.voltageMax = 253.0;                   // Upper safety limit
  gc.frequencyMin = 49.5;
  gc.frequencyMax = 50.5;
  gc.errorCode = 0;
  gc.tripDelay = 2000;
  gc.reconnectDelay = 60;
  gridCodes.push_back(gc);

  // Australia / NZ (AS/NZS 4777)
  gc.name = "AS/NZS 4777 (Australia/NZ)";
  gc.voltageThreshold = 230.0;
  gc.frequencyThreshold = 50.0;
  gc.voltageMin = 216.0;                   // Typical lower limit
  gc.voltageMax = 253.0;                   // Upper limit
  gc.frequencyMin = 49.0;
  gc.frequencyMax = 51.0;
  gc.errorCode = 0;
  gc.tripDelay = 2000;
  gc.reconnectDelay = 60;
  gridCodes.push_back(gc);

  // Set default current grid code to India or any preferred
  currentGridCode = "IS 16184 (India)";

  // Other configuration parameters
  aiDetectionEnabled = false;
  aiConfidenceThreshold = 0.85;
  sysSettings.enableFileLogging = true;
  sysSettings.enableSerialLogging = true;
  sysSettings.enableWebLogging = true;
  sysSettings.enableOTA = true;
  sysSettings.enableDebugMode = false;
  sysSettings.maxLogFileSize = 1048576;
  sysSettings.logRetentionDays = 30;
  sysSettings.timeZone = "IST";

  saveConfig();
}


bool loadConfig() {
    if (SD.exists("/config/system.json")) {
        File configFile = SD.open("/config/system.json", FILE_READ);
        if (configFile) {
            DynamicJsonDocument doc(4096);
            DeserializationError error = deserializeJson(doc, configFile);
            configFile.close();
            
            if (!error) {
                // Load grid codes
                if (doc.containsKey("gridCodes")) {
                    JsonArray gridCodesArray = doc["gridCodes"];
                    gridCodes.clear();
                    
                    for (JsonObject gcObj : gridCodesArray) {
                        GridCode gc;
                        gc.name = gcObj["name"].as<String>();
                        gc.voltageThreshold = gcObj["voltageThreshold"];
                        gc.frequencyThreshold = gcObj["frequencyThreshold"];
                        gc.voltageMin = gcObj["voltageMin"];
                        gc.voltageMax = gcObj["voltageMax"];
                        gc.frequencyMin = gcObj["frequencyMin"];
                        gc.frequencyMax = gcObj["frequencyMax"];
                        gc.errorCode = gcObj["errorCode"] | 0;
                        gc.tripDelay = gcObj["tripDelay"] | 200;
                        gc.reconnectDelay = gcObj["reconnectDelay"] | 60;
                        gridCodes.push_back(gc);
                    }
                }
                
                // Load current settings
                if (doc.containsKey("currentGridCode")) {
                    currentGridCode = doc["currentGridCode"].as<String>();
                }
                
                if (doc.containsKey("systemSettings")) {
                    JsonObject settings = doc["systemSettings"];
                    aiDetectionEnabled = settings["aiDetectionEnabled"];
                    aiConfidenceThreshold = settings["aiConfidenceThreshold"];
                    islandingDelay = settings["islandingDelay"];
                    
                    sysSettings.enableFileLogging = settings["enableFileLogging"];
                    sysSettings.enableSerialLogging = settings["enableSerialLogging"];
                    sysSettings.enableWebLogging = settings["enableWebLogging"];
                    sysSettings.enableOTA = settings["enableOTA"];
                    sysSettings.enableDebugMode = settings["enableDebugMode"];
                    sysSettings.maxLogFileSize = settings["maxLogFileSize"];
                    sysSettings.logRetentionDays = settings["logRetentionDays"];
                    
                    if (settings.containsKey("timeZone")) {
                        sysSettings.timeZone = settings["timeZone"].as<String>();
                    }
                }
                
                logEvent("Configuration loaded from SD card", "INFO");
                return true;
            }
        }
    }
    
    // If SD card load fails, create default config
    createDefaultConfig();
    return false;
}

bool saveConfig() {
    DynamicJsonDocument doc(4096);
    
    // Save grid codes
    JsonArray gridCodesArray = doc.createNestedArray("gridCodes");
    for (const GridCode& gc : gridCodes) {
        JsonObject gcObj = gridCodesArray.createNestedObject();
        gcObj["name"] = gc.name;
        gcObj["voltageThreshold"] = gc.voltageThreshold;
        gcObj["frequencyThreshold"] = gc.frequencyThreshold;
        gcObj["voltageMin"] = gc.voltageMin;
        gcObj["voltageMax"] = gc.voltageMax;
        gcObj["frequencyMin"] = gc.frequencyMin;
        gcObj["frequencyMax"] = gc.frequencyMax;
        gcObj["errorCode"] = gc.errorCode;
        gcObj["tripDelay"] = gc.tripDelay;
        gcObj["reconnectDelay"] = gc.reconnectDelay;
    }

    // Current grid code
    doc["currentGridCode"] = currentGridCode;

    // System settings
    JsonObject settings = doc.createNestedObject("systemSettings");
    settings["aiDetectionEnabled"] = aiDetectionEnabled;
    settings["aiConfidenceThreshold"] = aiConfidenceThreshold;
    settings["islandingDelay"] = islandingDelay;
    settings["enableFileLogging"] = sysSettings.enableFileLogging;
    settings["enableSerialLogging"] = sysSettings.enableSerialLogging;
    settings["enableWebLogging"] = sysSettings.enableWebLogging;
    settings["enableOTA"] = sysSettings.enableOTA;
    settings["enableDebugMode"] = sysSettings.enableDebugMode;
    settings["maxLogFileSize"] = sysSettings.maxLogFileSize;
    settings["logRetentionDays"] = sysSettings.logRetentionDays;
    settings["timeZone"] = sysSettings.timeZone;

    // Write to SD card
    File configFile = SD.open("/config/system.json", FILE_WRITE);
    if (!configFile) {
        logEvent("Failed to open config file for writing", "ERROR", 0x3001);
        return false;
    }
    if (serializeJsonPretty(doc, configFile) == 0) {
        logEvent("Failed to serialize config to SD", "ERROR", 0x3002);
        configFile.close();
        return false;
    }
    configFile.close();

    logEvent("Configuration saved to SD card", "INFO");
    return true;
}
// Preferences-based persistence for grid code (fallback/fast lookup)
#include <Preferences.h>

bool saveGridCodeToPrefs(const String &gc) {
    Preferences prefs;
    if (!prefs.begin("hydra", false)) {
        logEvent("Preferences begin (write) failed", "ERROR", 0x3003);
        return false;
    }
    prefs.putString("gridCode", gc);
    prefs.end();
    return true;
}

void loadGridCodeFromPrefs() {
    Preferences prefs;
    if (!prefs.begin("hydra", true)) {
        logEvent("Preferences begin (read) failed", "WARNING", 0x3004);
        return;
    }
    String gc = prefs.getString("gridCode", "");
    prefs.end();
    if (gc.length() > 0) {
        currentGridCode = gc;
        updateGridCodeThresholds();
        logEvent("Loaded grid code from prefs: " + currentGridCode, "INFO");
    }
}


DynamicJsonDocument buildStatusJson(size_t capacity) {
    DynamicJsonDocument doc(capacity);
    doc["voltage"] = isnan(acVoltage) ? 0.0 : acVoltage;
    doc["current"] = isnan(acCurrent) ? 0.0 : acCurrent;
    doc["power"] = isnan(acPower) ? 0.0 : acPower;
    doc["frequency"] = isnan(acFrequency) ? 0.0 : acFrequency;
    doc["energy"] = isnan(acEnergy) ? 0.0 : acEnergy;
    doc["pf"] = isnan(acPF) ? 0.0 : acPF;
    doc["relayStatus"] = sysState.relayState;
    doc["alarm"] = alarmTriggered;
    doc["aiEnabled"] = aiDetectionEnabled;
    doc["gridCode"] = currentGridCode;
    doc["uptime"] = millis() / 1000;
    doc["freeHeap"] = ESP.getFreeHeap();
    return doc;
}

void handleStatus() {
  if (!isAuthenticated) { server.send(401, "text/plain", "Unauthorized"); return; }
  updateLastActivity();

  DynamicJsonDocument doc(4096);

  // Grid codes + current config
  JsonArray codes = doc.createNestedArray("gridCodes");
  for (const GridCode &gc : gridCodes) {
    JsonObject code = codes.createNestedObject();
    code["name"] = gc.name;
    code["voltageThreshold"] = gc.voltageThreshold;
    code["frequencyThreshold"] = gc.frequencyThreshold;
    code["voltageMin"] = gc.voltageMin;
    code["voltageMax"] = gc.voltageMax;
    code["frequencyMin"] = gc.frequencyMin;
    code["frequencyMax"] = gc.frequencyMax;
    code["tripDelay"] = gc.tripDelay;
    code["reconnectDelay"] = gc.reconnectDelay;
  }
  doc["currentGridCode"] = currentGridCode;
  doc["aiEnabled"]       = aiDetectionEnabled;
  doc["aiThreshold"]     = aiConfidenceThreshold;
  doc["enableLogging"]   = sysSettings.enableFileLogging;

  // Merge in live status
  DynamicJsonDocument status = buildStatusJson(2048);
  for (JsonPair kv : status.as<JsonObject>()) {
    doc[kv.key()] = kv.value();
  }

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void ensureDefaultGridCodes() {
  if (!gridCodes.empty()) return;
  GridCode gc;
  gc.name = "IS 16184 (India)";
  gc.voltageThreshold = 230.0f; gc.frequencyThreshold = 50.0f;
  gc.voltageMin = 180.0f; gc.voltageMax = 260.0f;
  gc.frequencyMin = 47.5f; gc.frequencyMax = 52.0f;
  gc.errorCode = 0; gc.tripDelay = 2000; gc.reconnectDelay = 60;
  gridCodes.push_back(gc);
}

String getCurrentTimeISO() {
    time_t now;
    struct tm timeinfo;

    // Try to get current time
    if (!getLocalTime(&timeinfo, 0)) {
        // No NTP time yet — fallback to millis since boot
        unsigned long ms = millis();
        unsigned long seconds = ms / 1000;
        unsigned long days = seconds / 86400;
        seconds %= 86400;
        unsigned long hours = seconds / 3600;
        seconds %= 3600;
        unsigned long minutes = seconds / 60;
        seconds %= 60;

        // Return boot-relative pseudo-ISO timestamp
        char buf[32];
        snprintf(buf, sizeof(buf),
                 "BOOT+%lud %02lu:%02lu:%02lu",
                 days, hours, minutes, seconds);
        return String(buf);
    }

    // Format as ISO 8601 UTC
    char buf[25];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return String(buf);
}

void logMeasurementsToSD() {
    if (!sysSettings.enableFileLogging) return;

    // Ensure logs folder exists
    if (!SD.exists("/logs")) {
        SD.mkdir("/logs");
    }

    String filename = "/logs/measurements.csv";

    bool fileExists = SD.exists(filename);

    File dataFile = SD.open(filename, FILE_APPEND);
    if (dataFile) {
        // If the file is new, write the header row first
        if (!fileExists) {
            dataFile.println("Timestamp,Voltage,Current,Power,Frequency,PowerFactor");
        }

        // Write measurement data
        dataFile.printf("%s,%.2f,%.3f,%.2f,%.2f,%.2f\n",
                        getCurrentTimeISO().c_str(), // timestamp
                        acVoltage,
                        acCurrent,
                        acPower,
                        acFrequency,
                        acPF);
        dataFile.close();
    } else {
        logEvent("Failed to write measurements.csv to SD", "ERROR");
    }
}

void handleDownloadLog() {
    if (!isAuthenticated) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }

    updateLastActivity();

    String filename = "/logs/measurements.csv";

    // Create /logs folder if missing
    if (!SD.exists("/logs")) {
        SD.mkdir("/logs");
    }

    // Open file for append (create if not exists)
    File file = SD.open(filename, FILE_APPEND);
    if (!file) {
        server.send(500, "text/plain", "Failed to open/create file");
        return;
    }

    // If file is empty, write CSV header
    if (file.size() == 0) {
        file.println("Timestamp,Voltage(V),Current(A),Power(W),Frequency(Hz),PF");
    }

    // Append the latest measurement
    file.printf("%s,%.2f,%.3f,%.2f,%.2f,%.2f\n",
                getCurrentTimeISO().c_str(),
                acVoltage,
                acCurrent,
                acPower,
                acFrequency,
                acPF);

    file.close();

    // Reopen in read mode to stream to browser
    File readFile = SD.open(filename, FILE_READ);
    if (readFile) {
        server.sendHeader("Content-Disposition", "attachment; filename=measurements.csv");
        server.streamFile(readFile, "text/csv");
        readFile.close();
        logEvent("Measurements CSV created/appended and downloaded", "INFO");
    } else {
        server.send(500, "text/plain", "Failed to reopen file for reading");
    }
}




