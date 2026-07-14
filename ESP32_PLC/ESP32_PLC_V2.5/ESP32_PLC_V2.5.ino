/***************************************************************************
  Hydra ESP32_PLC Firmware v2.3 - Modern UI with SD Card Logging
  – Fixed WiFi connectivity (STA + AP fallback)
  – Improved responsive UI for both AP and STA modes
  – Fixed sidebar and login screen display issues
  – Enhanced error handling and debugging
***************************************************************************/
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>
#include <ModbusRTU.h>
#include <EEPROM.h>
#include "mbedtls/sha256.h"
#include "driver/ledc.h"
#include <esp_task_wdt.h>

// ========== Pin Definitions ==========
// Digital Input Pins
#define DI0 4
#define DI1 16
#define DI2 17
#define DI3 18
#define DI4 19
#define DI5 21
#define DI6 22
#define DI7 23

// Digital Output Pins
#define DO0 32
#define DO1 33
#define DO2 25
#define DO3 26
#define DO4 27
#define DO5 14
#define DO6 12
#define DO7 13

// Analog Input Pins
#define AI0 36
#define AI1 39
#define AI2 34
#define AI3 35

// Analog Output Pins (PWM)
#define AO0 12  // PWM Channel 0
#define AO1 13  // PWM Channel 1

// Other Pins
#define SD_CS_PIN 5
#define MODBUS_RX_PIN 16
#define MODBUS_TX_PIN 17

// PWM Configuration
#define PWM_FREQ 5000
#define PWM_RESOLUTION LEDC_TIMER_8_BIT

// ========== Configuration ==========
const char *DEFAULT_SSID = "ESP32-PLC";
const char *DEFAULT_PASSWORD = "plc-secure";
const char* DEFAULT_ADMIN_USER = "admin";
const char* DEFAULT_ADMIN_PASS = "admin123";
const char *ssid = "Mr.Gk";
const char *password = "Mr.Gk1992";

// ========== Global Objects ==========
WebServer server(80);
ModbusRTU mb;
File uploadFile;

struct User {
  String username;
  String passwordHash;
  uint8_t permissions;
};

struct Config {
  String wifiSSID;
  String wifiPassword;
  uint8_t modbusSlaveId;
  uint32_t modbusBaudRate;
  String defaultLogicPath;
  User users[5];
  uint8_t userCount = 0;
  bool sdCardPresent = false;
  bool wifiConnected = false;
  bool isAPMode = false;
  String lastError;
};

Config config;
String currentScript;
int scriptType = 0;

// ========== HTML/CSS Templates ==========
const char loginPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Login | ESP32 PLC</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <link href="https://cdn.jsdelivr.net/npm/bootstrap-icons@1.8.1/font/bootstrap-icons.css" rel="stylesheet">
  <style>
    :root {
      --primary: #4361ee;
      --secondary: #3f37c9;
      --bg-color: #f5f5f5;
      --container-bg: #ffffff;
      --border-color: #ddd;
      --text-color: #333;
      --input-bg: rgba(255, 255, 255, 0.05);
      --error-color: #dc3545;
    }
    
    [data-theme="dark"] {
      --primary: #4cc9f0;
      --secondary: #4895ef;
      --bg-color: #121212;
      --container-bg: rgba(33, 37, 41, 0.9);
      --border-color: rgba(255, 255, 255, 0.1);
      --text-color: #e0e0e0;
      --input-bg: rgba(255, 255, 255, 0.05);
      --error-color: #f72585;
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
      padding: 20px;
      box-sizing: border-box;
    }
    
    .login-container {
      background: var(--container-bg);
      padding: 2rem;
      border-radius: 12px;
      box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
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
      font-size: 1.8rem;
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
    
    .theme-toggle {
      position: absolute;
      top: 20px;
      right: 20px;
      background: var(--container-bg);
      border-radius: 50%;
      width: 40px;
      height: 40px;
      display: flex;
      align-items: center;
      justify-content: center;
      cursor: pointer;
      border: 1px solid var(--border-color);
      transition: all 0.3s ease;
    }
    
    @media (max-width: 480px) {
      .login-container {
        padding: 1.5rem;
      }
      
      h1 {
        font-size: 1.5rem;
      }
      
      .logo {
        width: 60px;
        height: 60px;
      }
    }
  </style>
</head>
<body>
  <div class="theme-toggle" id="themeToggle">
    <i class="bi bi-moon-fill" id="themeIcon"></i>
  </div>
  
  <div class="login-container">
    <svg class="logo" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M12 2L2 7L12 12L22 7L12 2Z" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
      <path d="M2 17L12 22L22 17" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
      <path d="M2 12L12 17L22 12" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
    </svg>
    <h1>ESP32 PLC</h1>
    <form action="/login" method="POST">
      <input type="hidden" name="csrf" value="ESP32_PLC_CSRF_TOKEN">
      <input type="text" name="username" placeholder="Username" required>
      <input type="password" name="password" placeholder="Password" required>
      <button type="submit">Login</button>
    </form>
    <div id="error" class="error"></div>
  </div>

  <script>
    const themeToggle = document.getElementById('themeToggle');
    const themeIcon = document.getElementById('themeIcon');
    const html = document.documentElement;
    
    // Check for saved theme preference or use preferred color scheme
    const savedTheme = localStorage.getItem('theme') || 
                      (window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
    
    // Apply the saved theme
    if (savedTheme === 'dark') {
      html.setAttribute('data-theme', 'dark');
      themeIcon.className = 'bi bi-sun-fill';
    }
    
    // Theme toggle event
    themeToggle.addEventListener('click', function() {
      if (html.getAttribute('data-theme') === 'dark') {
        html.removeAttribute('data-theme');
        localStorage.setItem('theme', 'light');
        themeIcon.className = 'bi bi-moon-fill';
      } else {
        html.setAttribute('data-theme', 'dark');
        localStorage.setItem('theme', 'dark');
        themeIcon.className = 'bi bi-sun-fill';
      }
    });
    
    const urlParams = new URLSearchParams(window.location.search);
    if(urlParams.has('error')) {
      document.getElementById('error').textContent = 'Invalid username or password';
    }
  </script>
</body>
</html>
)rawliteral";

const char dashboardPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>ESP32 PLC Control</title>
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
            --text-muted: #6c757d;
        }
        
        [data-theme="dark"] {
            --primary: #4cc9f0;
            --secondary: #4895ef;
            --dark: #121212;
            --light: #e0e0e0;
            --danger: #f72585;
            --success: #4cc9f0;
            --warning: #ff9e00;
            --info: #4895ef;
            --bg-color: #121212;
            --text-color: #e0e0e0;
            --card-bg: rgba(33, 37, 41, 0.7);
            --card-border: rgba(255, 255, 255, 0.1);
            --navbar-bg: rgba(33, 37, 41, 0.9);
            --sidebar-bg: rgba(33, 37, 41, 0.9);
            --input-bg: rgba(255, 255, 255, 0.05);
            --border-color: rgba(255, 255, 255, 0.1);
            --text-muted: #adb5bd;
        }
        
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-color);
            min-height: 100vh;
            transition: all 0.3s ease;
            overflow-x: hidden;
        }
        
        .navbar {
            background-color: var(--navbar-bg) !important;
            backdrop-filter: blur(10px);
            border-bottom: 1px solid var(--border-color);
            transition: all 0.3s ease;
            position: sticky;
            top: 0;
            z-index: 1020;
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
            transform: translateY(-5px);
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
            cursor: pointer;
        }
        
        .status-card:active {
            transform: scale(0.98);
        }
        
        .status-normal {
            background-color: rgba(40, 167, 69, 0.1);
            border-left: 5px solid var(--success);
        }
        
        .status-alarm {
            background-color: rgba(220, 53, 69, 0.1);
            border-left: 5px solid var(--danger);
            animation: pulse 2s infinite;
        }
        
        @keyframes pulse {
            0% { opacity: 1; }
            50% { opacity: 0.7; }
            100% { opacity: 1; }
        }
        
        .data-item {
            padding: 1rem;
            border-radius: 8px;
            margin-bottom: 0.5rem;
            background-color: rgba(0, 0, 0, 0.02);
            border: 1px solid var(--border-color);
        }
        
        .data-label {
            font-size: 0.9rem;
            color: var(--text-muted);
            margin-bottom: 0.25rem;
            font-weight: 500;
        }
        
        .data-value {
            font-size: 1.25rem;
            font-weight: 600;
            color: var(--text-color);
        }
        
        .chart-container {
            width: 100%;
            height: 300px;
            margin-bottom: 1.5rem;
        }
        
        .btn-primary {
            background-color: var(--primary);
            border-color: var(--primary);
        }
        
        .btn-primary:hover {
            background-color: var(--secondary);
            border-color: var(--secondary);
        }
        
        .settings-panel {
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: rgba(0, 0, 0, 0.8);
            z-index: 1050;
            display: none;
            overflow-y: auto;
            padding: 2rem;
        }
        
        .settings-content {
            background-color: var(--card-bg);
            border-radius: 12px;
            padding: 2rem;
            max-width: 800px;
            margin: 0 auto;
            border: 1px solid var(--border-color);
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
            color: var(--text-muted);
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
            color: var(--text-color);
            text-decoration: none;
            border-radius: 6px;
            margin-bottom: 0.5rem;
            transition: all 0.3s ease;
        }
        
        .sidebar-link:hover, .sidebar-link.active {
            background-color: rgba(67, 97, 238, 0.1);
            color: var(--primary);
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
            width: 100%;
        }
        
        .sidebar-open .main-content {
            margin-left: 250px;
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
            background-color: rgba(0, 0, 0, 0.7);
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
            color: white;
        }
        
        .connection-success {
            background-color: var(--success);
        }
        
        .connection-error {
            background-color: var(--danger);
        }
        
        /* Improved visibility for light mode */
        .text-muted {
            color: var(--text-muted) !important;
        }
        
        .dropdown-menu {
            background-color: var(--card-bg);
            border: 1px solid var(--border-color);
        }
        
        .dropdown-item {
            color: var(--text-color);
        }
        
        .dropdown-item:hover {
            background-color: rgba(67, 97, 238, 0.1);
            color: var(--primary);
        }
        
        .table {
            color: var(--text-color);
        }
        
        .table-striped tbody tr:nth-of-type(odd) {
            background-color: rgba(0, 0, 0, 0.02);
        }
        
        /* Connection badge animation */
        .connection-badge {
            position: relative;
            padding-left: 1.5rem;
        }
        
        .connection-badge:before {
            content: "";
            position: absolute;
            left: 0.5rem;
            top: 50%;
            transform: translateY(-50%);
            width: 0.5rem;
            height: 0.5rem;
            border-radius: 50%;
            background-color: currentColor;
            animation: pulse 2s infinite;
        }
        
        /* Mobile-specific styles */
        @media (max-width: 992px) {
            .sidebar {
                width: 80%;
            }
            
            .sidebar.show + .main-content {
                position: fixed;
                width: 100%;
                left: 80%;
                overflow: hidden;
            }
            
            .main-content {
                margin-left: 0 !important;
            }
        }
        
        @media (max-width: 768px) {
            .sidebar {
                width: 280px;
            }
            
            .sidebar.show + .main-content {
                left: 280px;
            }
            
            .status-card h4 {
                font-size: 1.1rem;
            }
            
            .badge {
                font-size: 0.7rem;
                margin-bottom: 0.3rem;
            }
            
            .data-item {
                padding: 0.5rem;
            }
            
            .data-label {
                font-size: 0.8rem;
            }
            
            .data-value {
                font-size: 1rem;
            }
            
            .navbar-brand {
                font-size: 1rem;
            }
        }
        
        @media (max-width: 576px) {
            .main-content {
                padding: 1rem;
            }
            
            .sidebar {
                width: 100%;
            }
            
            .sidebar.show + .main-content {
                display: none;
            }
            
            .card-body {
                padding: 1rem;
            }
            
            .theme-toggle-container {
                margin-right: 0.5rem;
            }
            
            .theme-toggle-label {
                display: none;
            }
        }
    </style>
</head>
<body>
    <nav class="navbar navbar-expand-lg navbar-dark">
        <div class="container-fluid">
            <button class="navbar-toggler me-2" type="button" id="sidebarToggle">
                <span class="navbar-toggler-icon"></span>
            </button>
            <a class="navbar-brand" href="#">
                <i class="bi bi-cpu me-2"></i>
                ESP32 PLC
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
                        <li><a class="dropdown-item" href="#"><i class="bi bi-gear me-2"></i>Settings</a></li>
                        <li><hr class="dropdown-divider"></li>
                        <li><a class="dropdown-item" href="/logout"><i class="bi bi-box-arrow-right me-2"></i>Logout</a></li>
                    </ul>
                </div>
            </div>
        </div>
    </nav>

    <div class="sidebar">
        <div class="sidebar-header mb-3">
            <h5 class="text-center">PLC Menu</h5>
        </div>
        <a href="#" class="sidebar-link active" onclick="showTab('dashboard')">
            <i class="bi bi-speedometer2"></i>
            Dashboard
        </a>
        <a href="#" class="sidebar-link" onclick="showTab('io')">
            <i class="bi bi-sliders"></i>
            I/O Control
        </a>
        <a href="#" class="sidebar-link" onclick="showTab('scripts')">
            <i class="bi bi-code-square"></i>
            Scripts
        </a>
        <a href="#" class="sidebar-link" onclick="showTab('settings')">
            <i class="bi bi-gear"></i>
            Settings
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
        <!-- Dashboard Tab -->
        <div id="dashboard" class="tab-content active">
            <div class="container-fluid">
                <div class="row mb-4">
                    <div class="col-12">
                        <div class="status-card status-normal" id="statusDisplay">
                            <div class="row align-items-center">
                                <div class="col-md-8">
                                    <h4 class="mb-0"><i class="bi bi-check-circle-fill me-2"></i> System Status: <span id="statusText">Normal</span>
                                        <small class="d-block text-muted mt-1" id="statusSubtext">All systems operational</small>
                                    </h4>
                                </div>
                                <div class="col-md-4 text-md-end mt-2 mt-md-0">
                                    <span class="badge bg-primary me-2"><i class="bi bi-cpu me-1"></i> ESP32</span>
                                    <span class="badge bg-success me-2 connection-badge"><i class="bi bi-wifi me-1"></i> <span id="wifiStatus">Connected</span></span>
                                    <span class="badge bg-secondary"><i class="bi bi-clock me-1"></i> <span id="currentTime"></span></span>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
                
                <!-- System Info Panel (Toggleable) -->
                <div class="card mb-4" id="systemInfoCard" style="display: none;">
                    <div class="card-body">
                        <div class="row">
                            <div class="col-md-4 mb-3">
                                <div class="data-item">
                                    <div class="data-label">CPU Usage</div>
                                    <div class="data-value"><span id="cpuUsage">0</span>%</div>
                                </div>
                            </div>
                            <div class="col-md-4 mb-3">
                                <div class="data-item">
                                    <div class="data-label">Memory Free</div>
                                    <div class="data-value"><span id="freeMemory">0</span> KB</div>
                                </div>
                            </div>
                            <div class="col-md-4 mb-3">
                                <div class="data-item">
                                    <div class="data-label">Uptime</div>
                                    <div class="data-value"><span id="systemUptime">0</span></div>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
                
                <div class="row">
                    <div class="col-xl-6 col-md-6 mb-4">
                        <div class="card h-100">
                            <div class="card-body">
                                <h5 class="card-title">Digital Inputs</h5>
                                <div class="row">
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DI0</div>
                                            <div class="data-value"><span id="di0-status">OFF</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DI1</div>
                                            <div class="data-value"><span id="di1-status">OFF</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DI2</div>
                                            <div class="data-value"><span id="di2-status">OFF</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DI3</div>
                                            <div class="data-value"><span id="di3-status">OFF</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DI4</div>
                                            <div class="data-value"><span id="di4-status">OFF</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DI5</div>
                                            <div class="data-value"><span id="di5-status">OFF</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DI6</div>
                                            <div class="data-value"><span id="di6-status">OFF</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DI7</div>
                                            <div class="data-value"><span id="di7-status">OFF</span></div>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                    
                    <div class="col-xl-6 col-md-6 mb-4">
                        <div class="card h-100">
                            <div class="card-body">
                                <h5 class="card-title">Digital Outputs</h5>
                                <div class="row">
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DO0</div>
                                            <div class="data-value"><span id="do0-status">OFF</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DO1</div>
                                            <div class="data-value"><span id="do1-status">OFF</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DO2</div>
                                            <div class="data-value"><span id="do2-status">OFF</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DO3</div>
                                            <div class="data-value"><span id="do3-status">OFF</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DO4</div>
                                            <div class="data-value"><span id="do4-status">OFF</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DO5</div>
                                            <div class="data-value"><span id="do5-status">OFF</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DO6</div>
                                            <div class="data-value"><span id="do6-status">OFF</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">DO7</div>
                                            <div class="data-value"><span id="do7-status">OFF</span></div>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
                
                <div class="row">
                    <div class="col-xl-6 col-md-6 mb-4">
                        <div class="card h-100">
                            <div class="card-body">
                                <h5 class="card-title">Analog Inputs</h5>
                                <div class="row">
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">AI0</div>
                                            <div class="data-value"><span id="ai0-value">0</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">AI1</div>
                                            <div class="data-value"><span id="ai1-value">0</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">AI2</div>
                                            <div class="data-value"><span id="ai2-value">0</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">AI3</div>
                                            <div class="data-value"><span id="ai3-value">0</span></div>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                    
                    <div class="col-xl-6 col-md-6 mb-4">
                        <div class="card h-100">
                            <div class="card-body">
                                <h5 class="card-title">Analog Outputs</h5>
                                <div class="row">
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">AO0</div>
                                            <div class="data-value"><span id="ao0-value">0</span></div>
                                        </div>
                                    </div>
                                    <div class="col-6 mb-3">
                                        <div class="data-item">
                                            <div class="data-label">AO1</div>
                                            <div class="data-value"><span id="ao1-value">0</span></div>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
        
        <!-- I/O Control Tab -->
        <div id="io" class="tab-content" style="display:none;">
            <div class="container-fluid">
                <div class="card mb-4">
                    <div class="card-header bg-primary text-white">
                        <i class="bi bi-sliders"></i> Digital Output Control
                    </div>
                    <div class="card-body">
                        <div class="row">
                            <div class="col-md-3 mb-3">
                                <label class="form-label">DO0</label>
                                <div class="form-check form-switch">
                                    <input class="form-check-input" type="checkbox" id="do0-switch" onchange="updateOutput(0, this.checked)">
                                    <label class="form-check-label" for="do0-switch">Toggle</label>
                                </div>
                            </div>
                            <div class="col-md-3 mb-3">
                                <label class="form-label">DO1</label>
                                <div class="form-check form-switch">
                                    <input class="form-check-input" type="checkbox" id="do1-switch" onchange="updateOutput(1, this.checked)">
                                    <label class="form-check-label" for="do1-switch">Toggle</label>
                                </div>
                            </div>
                            <div class="col-md-3 mb-3">
                                <label class="form-label">DO2</label>
                                <div class="form-check form-switch">
                                    <input class="form-check-input" type="checkbox" id="do2-switch" onchange="updateOutput(2, this.checked)">
                                    <label class="form-check-label" for="do2-switch">Toggle</label>
                                </div>
                            </div>
                            <div class="col-md-3 mb-3">
                                <label class="form-label">DO3</label>
                                <div class="form-check form-switch">
                                    <input class="form-check-input" type="checkbox" id="do3-switch" onchange="updateOutput(3, this.checked)">
                                    <label class="form-check-label" for="do3-switch">Toggle</label>
                                </div>
                            </div>
                            <div class="col-md-3 mb-3">
                                <label class="form-label">DO4</label>
                                <div class="form-check form-switch">
                                    <input class="form-check-input" type="checkbox" id="do4-switch" onchange="updateOutput(4, this.checked)">
                                    <label class="form-check-label" for="do4-switch">Toggle</label>
                                </div>
                            </div>
                            <div class="col-md-3 mb-3">
                                <label class="form-label">DO5</label>
                                <div class="form-check form-switch">
                                    <input class="form-check-input" type="checkbox" id="do5-switch" onchange="updateOutput(5, this.checked)">
                                    <label class="form-check-label" for="do5-switch">Toggle</label>
                                </div>
                            </div>
                            <div class="col-md-3 mb-3">
                                <label class="form-label">DO6</label>
                                <div class="form-check form-switch">
                                    <input class="form-check-input" type="checkbox" id="do6-switch" onchange="updateOutput(6, this.checked)">
                                    <label class="form-check-label" for="do6-switch">Toggle</label>
                                </div>
                            </div>
                            <div class="col-md-3 mb-3">
                                <label class="form-label">DO7</label>
                                <div class="form-check form-switch">
                                    <input class="form-check-input" type="checkbox" id="do7-switch" onchange="updateOutput(7, this.checked)">
                                    <label class="form-check-label" for="do7-switch">Toggle</label>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
                
                <div class="card">
                    <div class="card-header bg-primary text-white">
                        <i class="bi bi-sliders"></i> Analog Output Control
                    </div>
                    <div class="card-body">
                        <div class="row">
                            <div class="col-md-6 mb-3">
                                <label class="form-label">AO0 (PWM)</label>
                                <input type="range" class="form-range" min="0" max="255" id="ao0-pwm" oninput="updatePWM(0, this.value)">
                                <div class="text-center" id="pwm0-value">0</div>
                            </div>
                            <div class="col-md-6 mb-3">
                                <label class="form-label">AO1 (PWM)</label>
                                <input type="range" class="form-range" min="0" max="255" id="ao1-pwm" oninput="updatePWM(1, this.value)">
                                <div class="text-center" id="pwm1-value">0</div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
        
        <!-- Scripts Tab -->
        <div id="scripts" class="tab-content" style="display:none;">
            <div class="container-fluid">
                <div class="card">
                    <div class="card-header bg-primary text-white">
                        <i class="bi bi-file-earmark-code"></i> Script Management
                    </div>
                    <div class="card-body">
                        <div id="script-error" class="alert alert-danger" style="display:none;"></div>
                        
                        <ul class="nav nav-tabs" id="scriptTabs" role="tablist">
                            <li class="nav-item" role="presentation">
                                <button class="nav-link active" id="upload-tab" data-bs-toggle="tab" data-bs-target="#uploadTab" type="button" role="tab">Upload</button>
                            </li>
                            <li class="nav-item" role="presentation">
                                <button class="nav-link" id="manage-tab" data-bs-toggle="tab" data-bs-target="#manageTab" type="button" role="tab">Manage</button>
                            </li>
                            <li class="nav-item" role="presentation">
                                <button class="nav-link" id="editor-tab" data-bs-toggle="tab" data-bs-target="#editorTab" type="button" role="tab">Editor</button>
                            </li>
                        </ul>
                        
                        <div class="tab-content" id="scriptTabContent">
                            <div class="tab-pane fade show active" id="uploadTab" role="tabpanel">
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
                            
                            <div class="tab-pane fade" id="manageTab" role="tabpanel">
                                <button class="btn btn-primary mb-3" onclick="listScripts()">
                                    <i class="bi bi-arrow-clockwise"></i> Refresh List
                                </button>
                                <div class="table-responsive">
                                    <table class="table table-striped">
                                        <thead>
                                            <tr>
                                                <th>Filename</th>
                                                <th>Type</th>
                                                <th>Actions</th>
                                            </tr>
                                        </thead>
                                        <tbody id="script-list">
                                            <!-- Scripts will be loaded here -->
                                        </tbody>
                                    </table>
                                </div>
                            </div>
                            
                            <div class="tab-pane fade" id="editorTab" role="tabpanel">
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
            </div>
        </div>
        
        <!-- Settings Tab -->
        <div id="settings" class="tab-content" style="display:none;">
            <div class="container-fluid">
                <div class="card">
                    <div class="card-header bg-primary text-white">
                        <i class="bi bi-gear"></i> System Settings
                    </div>
                    <div class="card-body">
                        <div class="mb-3">
                            <h5><i class="bi bi-wifi"></i> WiFi Settings</h5>
                            <label for="wifi-ssid" class="form-label">SSID</label>
                            <input type="text" class="form-control" id="wifi-ssid" value="%WIFI_SSID%">
                            <label for="wifi-password" class="form-label">Password</label>
                            <input type="password" class="form-control" id="wifi-password" value="%WIFI_PASSWORD%">
                            <div class="form-text">Current Mode: <span id="wifiMode">%WIFI_MODE%</span></div>
                        </div>
                        
                        <div class="mb-3">
                            <h5><i class="bi bi-plug"></i> Modbus Settings</h5>
                            <label for="modbus-slave-id" class="form-label">Slave ID</label>
                            <input type="number" class="form-control" id="modbus-slave-id" min="1" max="247" value="%MODBUS_SLAVE_ID%">
                            <label for="modbus-baudrate" class="form-label">Baud Rate</label>
                            <select class="form-select" id="modbus-baudrate">
                                <option value="9600" %MODBUS_9600%>9600</option>
                                <option value="19200" %MODBUS_19200%>19200</option>
                                <option value="38400" %MODBUS_38400%>38400</option>
                                <option value="57600" %MODBUS_57600%>57600</option>
                                <option value="115200" %MODBUS_115200%>115200</option>
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
        // Tab management
        function showTab(tabId) {
            document.querySelectorAll('.tab-content').forEach(tab => {
                tab.style.display = 'none';
            });
            document.getElementById(tabId).style.display = 'block';
            
            // Update active sidebar link
            document.querySelectorAll('.sidebar-link').forEach(link => {
                link.classList.remove('active');
            });
            event.currentTarget.classList.add('active');
        }
        
        // Theme toggle functionality
        function initializeTheme() {
            const themeToggle = document.getElementById('themeToggle');
            const html = document.documentElement;
            
            // Check for saved theme preference or use preferred color scheme
            const savedTheme = localStorage.getItem('theme') || 
                             (window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
            
            // Apply the saved theme
            if (savedTheme === 'dark') {
                html.setAttribute('data-theme', 'dark');
                themeToggle.checked = true;
            }
            
            // Theme toggle event
            themeToggle.addEventListener('change', function() {
                if (this.checked) {
                    html.setAttribute('data-theme', 'dark');
                    localStorage.setItem('theme', 'dark');
                } else {
                    html.removeAttribute('data-theme');
                    localStorage.setItem('theme', 'light');
                }
            });
        }
        
        // Sidebar toggle
        function initializeSidebar() {
            document.getElementById('sidebarToggle').addEventListener('click', function() {
                document.querySelector('.sidebar').classList.toggle('show');
                document.getElementById('mainContent').classList.toggle('sidebar-open');
            });
        }
        
        // Update current time
        function updateCurrentTime() {
            const now = new Date();
            const options = { 
                hour: '2-digit', 
                minute: '2-digit', 
                second: '2-digit',
                hour12: true 
            };
            document.getElementById('currentTime').textContent = now.toLocaleTimeString(undefined, options);
        }
        
        // Connection status management
        function updateConnectionStatus(connected) {
            const statusElement = document.getElementById('connectionStatus');
            const statusText = document.getElementById('connectionStatusText');
            
            if (connected) {
                statusElement.className = 'connection-status connection-success';
                statusText.textContent = 'Connected';
            } else {
                statusElement.className = 'connection-status connection-error';
                statusText.textContent = 'Disconnected - Reconnecting...';
            }
            
            statusElement.style.display = 'block';
            setTimeout(() => {
                statusElement.style.display = 'none';
            }, 3000);
        }
        
        // I/O Control functions
        function updateOutput(pin, state) {
            fetch(`/api/io/write?do${pin}=${state ? 1 : 0}`)
                .then(response => {
                    if (!response.ok) throw new Error('Failed to update output');
                    return response.text();
                })
                .then(text => {
                    console.log(text);
                    updateStatus();
                })
                .catch(error => {
                    console.error('Error updating output:', error);
                    showToast('Error updating output', 'danger');
                    document.getElementById(`do${pin}-switch`).checked = !state;
                });
        }
        
        function updatePWM(pin, value) {
            document.getElementById(`pwm${pin}-value`).textContent = value;
            fetch(`/api/io/write?ao${pin}=${value}`)
                .then(response => {
                    if (!response.ok) throw new Error('Failed to update PWM');
                    return response.text();
                })
                .then(text => {
                    console.log(text);
                    updateStatus();
                })
                .catch(error => {
                    console.error('Error updating PWM:', error);
                    showToast('Error updating PWM', 'danger');
                });
        }
        
        // Script management
        function listScripts() {
            fetch('/api/scripts/list')
                .then(response => {
                    if (!response.ok) throw new Error('Failed to list scripts');
                    return response.json();
                })
                .then(data => {
                    const tbody = document.querySelector('#script-list');
                    tbody.innerHTML = '';
                    const selector = document.getElementById('script-selector');
                    selector.innerHTML = '<option value="">Select a script...</option>';
                    
                    if (data.scripts && data.scripts.length > 0) {
                        data.scripts.forEach(script => {
                            // Add to table
                            const row = document.createElement('tr');
                            row.innerHTML = `
                                <td>${script.name}</td>
                                <td>${script.type}</td>
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
                    } else {
                        tbody.innerHTML = '<tr><td colspan="3">No scripts found</td></tr>';
                    }
                })
                .catch(error => {
                    console.error('Error listing scripts:', error);
                    document.querySelector('#script-list').innerHTML = 
                        '<tr><td colspan="3">Error loading scripts: ' + error.message + '</td></tr>';
                });
        }
        
        function loadScriptForEditing() {
            const path = document.getElementById('script-selector').value;
            if (!path) return;
            
            fetch('/api/script/get?path=' + encodeURIComponent(path))
                .then(response => {
                    if (!response.ok) throw new Error('Failed to load script');
                    return response.text();
                })
                .then(text => {
                    document.getElementById('script-editor').value = text;
                })
                .catch(error => {
                    console.error('Error loading script:', error);
                    showToast('Error loading script: ' + error.message, 'danger');
                });
        }
        
        function saveScript() {
            const path = document.getElementById('script-selector').value;
            if (!path) {
                showToast('Please select a script first', 'warning');
                return;
            }
            
            const content = document.getElementById('script-editor').value;
            
            fetch('/api/script/save', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: 'path=' + encodeURIComponent(path) + '&content=' + encodeURIComponent(content)
            })
            .then(response => {
                if (!response.ok) throw new Error('Failed to save script');
                return response.text();
            })
            .then(text => {
                showToast(text, 'success');
            })
            .catch(error => {
                console.error('Error saving script:', error);
                showToast('Error saving script: ' + error.message, 'danger');
            });
        }
        
        function loadAndRunScript(path) {
            if (!path) {
                path = document.getElementById('script-selector').value;
                if (!path) {
                    showToast('Please select a script first', 'warning');
                    return;
                }
            }
            
            fetch('/api/script/load?path=' + encodeURIComponent(path))
                .then(response => {
                    if (!response.ok) throw new Error('Failed to load script');
                    return response.text();
                })
                .then(text => {
                    showToast(text, 'success');
                })
                .catch(error => {
                    console.error('Error loading script:', error);
                    showToast('Error loading script: ' + error.message, 'danger');
                });
        }
        
        function deleteScript(path) {
            if (!confirm('Are you sure you want to delete ' + path + '?')) return;
            
            fetch('/api/script/delete?path=' + encodeURIComponent(path), { 
                method: 'DELETE' 
            })
            .then(response => {
                if (!response.ok) throw new Error('Failed to delete script');
                return response.text();
            })
            .then(text => {
                showToast(text, 'success');
                listScripts();
            })
            .catch(error => {
                console.error('Error deleting script:', error);
                showToast('Error deleting script: ' + error.message, 'danger');
            });
        }
        
        function uploadScript() {
            const fileInput = document.getElementById('script-upload');
            if (!fileInput.files.length) {
                showToast('Please select a file first', 'warning');
                return;
            }
            
            const formData = new FormData();
            formData.append('file', fileInput.files[0]);
            
            fetch('/api/script/upload', {
                method: 'POST',
                body: formData
            })
            .then(response => {
                if (!response.ok) throw new Error('Upload failed');
                return response.text();
            })
            .then(text => {
                showToast(text, 'success');
                listScripts();
                fileInput.value = ''; // Clear file input
            })
            .catch(error => {
                console.error('Error uploading script:', error);
                showToast('Error uploading script: ' + error.message, 'danger');
            });
        }
        
        // Settings functions
        function saveSettings() {
            const ssid = document.getElementById('wifi-ssid').value;
            const password = document.getElementById('wifi-password').value;
            const slaveId = document.getElementById('modbus-slave-id').value;
            const baudrate = document.getElementById('modbus-baudrate').value;
            
            if (!ssid || !slaveId || !baudrate) {
                showToast('Please fill all required fields', 'warning');
                return;
            }
            
            fetch('/api/settings/save', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}&slaveId=${slaveId}&baudrate=${baudrate}`
            })
            .then(response => {
                if (!response.ok) throw new Error('Failed to save settings');
                return response.text();
            })
            .then(text => {
                showToast(text, 'success');
                updateStatus();
            })
            .catch(error => {
                console.error('Error saving settings:', error);
                showToast('Error saving settings: ' + error.message, 'danger');
            });
        }
        
        function restartSystem() {
            if (confirm('Are you sure you want to restart the system?')) {
                fetch('/api/system/restart', { method: 'POST' })
                    .then(() => showToast('Restarting...', 'info'))
                    .catch(error => {
                        console.error('Error restarting:', error);
                        showToast('Error restarting: ' + error.message, 'danger');
                    });
            }
        }
        
        // Status updates
        function updateStatus() {
            fetch('/api/status')
                .then(response => {
                    if (!response.ok) throw new Error('Failed to get status');
                    return response.json();
                })
                .then(data => {
                    // Update all I/O status indicators
                    for (let i = 0; i < 8; i++) {
                        document.getElementById(`di${i}-status`).textContent = data[`di${i}`] ? 'ON' : 'OFF';
                        document.getElementById(`do${i}-status`).textContent = data[`do${i}`] ? 'ON' : 'OFF';
                        if (document.getElementById(`do${i}-switch`)) {
                            document.getElementById(`do${i}-switch`).checked = data[`do${i}`];
                        }
                    }
                    
                    // Update analog values
                    for (let i = 0; i < 4; i++) {
                        document.getElementById(`ai${i}-value`).textContent = data[`ai${i}`] || '0';
                    }
                    
                    // Update PWM values
                    for (let i = 0; i < 2; i++) {
                        document.getElementById(`ao${i}-value`).textContent = data[`ao${i}`] || '0';
                        document.getElementById(`pwm${i}-value`).textContent = data[`ao${i}`] || '0';
                        document.getElementById(`ao${i}-pwm`).value = data[`ao${i}`] || '0';
                    }
                    
                    // Update system status
                    document.getElementById('wifiStatus').textContent = data.wifiConnected ? 'Connected' : 'Disconnected';
                    document.getElementById('wifiMode').textContent = data.isAPMode ? 'Access Point' : 'Station';
                    document.getElementById('systemStatusText').textContent = data.wifiConnected ? 'Operational' : 'Offline';
                    
                    // Update system info if available
                    if (data.cpuUsage) {
                        document.getElementById('cpuUsage').textContent = data.cpuUsage;
                        document.getElementById('systemInfoCard').style.display = 'block';
                    }
                    if (data.freeMemory) {
                        document.getElementById('freeMemory').textContent = data.freeMemory;
                    }
                    if (data.uptime) {
                        document.getElementById('systemUptime').textContent = formatUptime(data.uptime);
                    }
                    
                    // Update connection status
                    updateConnectionStatus(data.wifiConnected);
                })
                .catch(error => {
                    console.error('Error updating status:', error);
                    updateConnectionStatus(false);
                });
        }
        
        // Helper function to format uptime
        function formatUptime(seconds) {
            if (!seconds) return '0s';
            const days = Math.floor(seconds / (3600 * 24));
            seconds %= 3600 * 24;
            const hours = Math.floor(seconds / 3600);
            seconds %= 3600;
            const mins = Math.floor(seconds / 60);
            seconds %= 60;
            
            return `${days > 0 ? days + 'd ' : ''}${hours > 0 ? hours + 'h ' : ''}${mins > 0 ? mins + 'm ' : ''}${seconds}s`;
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
        
        // Initialize the application
        function initializeApp() {
            initializeTheme();
            initializeSidebar();
            
            // Set up periodic updates
            setInterval(updateCurrentTime, 1000);
            updateCurrentTime();
            
            // Initial status update
            updateStatus();
            
            // Set up periodic status refresh
            setInterval(updateStatus, 1000);
            
            // Load scripts list
            listScripts();
            
            // Make status display clickable
            document.getElementById('statusDisplay').addEventListener('click', function() {
                updateStatus();
                showToast('Status refreshed', 'info');
            });
        }
        
        // Start the application when DOM is loaded
        document.addEventListener('DOMContentLoaded', initializeApp);
    </script>
</body>
</html>
)rawliteral";

// ========== Helper Functions ==========
String sha256(const String& password) {
  uint8_t hash[32];
  mbedtls_sha256_context ctx;
  
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, false);
  mbedtls_sha256_update(&ctx, (const uint8_t*)password.c_str(), password.length());
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);
  
  String hashStr;
  for(int i=0; i<32; i++) {
    char buf[3];
    sprintf(buf, "%02x", hash[i]);
    hashStr += buf;
  }
  return hashStr;
}

bool checkAuth(WebServer& server) {
  if(!server.authenticate(DEFAULT_ADMIN_USER, DEFAULT_ADMIN_PASS)) {
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
    config.sdCardPresent = false;
    config.wifiConnected = false;
    config.isAPMode = false;
    config.lastError = "";
    
    config.users[0].username = DEFAULT_ADMIN_USER;
    config.users[0].passwordHash = sha256(DEFAULT_ADMIN_PASS);
    config.users[0].permissions = 0x07; // All permissions
    config.userCount = 1;
    
    saveConfig();
    return;
  }
  
  // Simple JSON parsing
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
  
  config.sdCardPresent = false;
  config.wifiConnected = false;
  config.isAPMode = false;
  config.lastError = "";
}

// ========== IO Functions ==========
void setupIO() {
  // Set up digital inputs
  pinMode(DI0, INPUT);
  pinMode(DI1, INPUT);
  pinMode(DI2, INPUT);
  pinMode(DI3, INPUT);
  pinMode(DI4, INPUT);
  pinMode(DI5, INPUT);
  pinMode(DI6, INPUT);
  pinMode(DI7, INPUT);

  // Set up digital outputs
  pinMode(DO0, OUTPUT);
  pinMode(DO1, OUTPUT);
  pinMode(DO2, OUTPUT);
  pinMode(DO3, OUTPUT);
  pinMode(DO4, OUTPUT);
  pinMode(DO5, OUTPUT);
  pinMode(DO6, OUTPUT);
  pinMode(DO7, OUTPUT);

  // Set up PWM outputs with safety checks
  ledc_timer_config_t timer_conf = {
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .duty_resolution = PWM_RESOLUTION,
    .timer_num = LEDC_TIMER_0,
    .freq_hz = (PWM_FREQ > 0) ? PWM_FREQ : 5000, // Default to 5kHz if 0
    .clk_cfg = LEDC_AUTO_CLK
  };
  ledc_timer_config(&timer_conf);
  
  ledc_channel_config_t ledc_conf = {
    .gpio_num = AO0,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = LEDC_CHANNEL_0,
    .intr_type = LEDC_INTR_DISABLE,
    .timer_sel = LEDC_TIMER_0,
    .duty = 0,
    .hpoint = 0
  };
  ledc_channel_config(&ledc_conf);

  ledc_conf.gpio_num = AO1;
  ledc_conf.channel = LEDC_CHANNEL_1;
  ledc_channel_config(&ledc_conf);
}

bool readDigital(uint8_t pin) {
  switch(pin) {
    case 0: return digitalRead(DI0);
    case 1: return digitalRead(DI1);
    case 2: return digitalRead(DI2);
    case 3: return digitalRead(DI3);
    case 4: return digitalRead(DI4);
    case 5: return digitalRead(DI5);
    case 6: return digitalRead(DI6);
    case 7: return digitalRead(DI7);
    default: return false;
  }
}

void writeDigital(uint8_t pin, bool value) {
  switch(pin) {
    case 0: digitalWrite(DO0, value ? HIGH : LOW); break;
    case 1: digitalWrite(DO1, value ? HIGH : LOW); break;
    case 2: digitalWrite(DO2, value ? HIGH : LOW); break;
    case 3: digitalWrite(DO3, value ? HIGH : LOW); break;
    case 4: digitalWrite(DO4, value ? HIGH : LOW); break;
    case 5: digitalWrite(DO5, value ? HIGH : LOW); break;
    case 6: digitalWrite(DO6, value ? HIGH : LOW); break;
    case 7: digitalWrite(DO7, value ? HIGH : LOW); break;
  }
}

int readAnalog(uint8_t pin) {
  switch(pin) {
    case 0: return analogRead(AI0);
    case 1: return analogRead(AI1);
    case 2: return analogRead(AI2);
    case 3: return analogRead(AI3);
    default: return 0;
  }
}

void writeAnalog(uint8_t pin, int value) {
  value = constrain(value, 0, 255);
  if(pin == 0) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, value);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  } else if(pin == 1) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, value);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  }
}

// ========== Modbus Functions ==========
void setupModbus() {
  // Ensure baud rate is valid
  if(config.modbusBaudRate == 0) {
    config.modbusBaudRate = 9600; // Default safe value
    Serial.println("Warning: Modbus baud rate was 0, using 9600");
  }
  
  Serial2.begin(config.modbusBaudRate, SERIAL_8N1, MODBUS_RX_PIN, MODBUS_TX_PIN);
  mb.begin(&Serial2);
  mb.slave(config.modbusSlaveId);
  
  // Setup registers with safe defaults
  mb.addCoil(0, false); // DO0
  mb.addCoil(1, false); // DO1
  mb.addIreg(0, 0);     // DI0
  mb.addIreg(1, 0);     // DI1
  mb.addIreg(2, 0);     // AI0
  mb.addHreg(0, 0);     // Holding register 0
  mb.addHreg(1, 0);     // Holding register 1
}

void updateModbus() {
  mb.task();
  
  // Update input registers with safety checks
  int ai0 = readAnalog(0);
  mb.Ireg(0, readDigital(0) ? 1 : 0);
  mb.Ireg(1, readDigital(1) ? 1 : 0);
  mb.Ireg(2, (ai0 >= 0) ? (ai0 >> 2) : 0); // Scale to 0-1023 with bounds check
  
  // Sync coils with outputs
  if(mb.Coil(0) != readDigital(0)) {
    writeDigital(0, mb.Coil(0));
  }
  if(mb.Coil(1) != readDigital(1)) {
    writeDigital(1, mb.Coil(1));
  }
}

// ========== Script Engine ==========
bool loadScript(const String& path) {
  if(!config.sdCardPresent || !SD.exists(path.c_str())) {
    config.lastError = "SD card not available or file not found";
    return false;
  }
  
  File file = SD.open(path.c_str(), FILE_READ);
  if(!file) {
    config.lastError = "Failed to open script file";
    return false;
  }
  
  // Limit file size to prevent memory issues
  if(file.size() > 10240) { // 10KB max
    file.close();
    config.lastError = "Script file too large";
    return false;
  }
  
  currentScript = file.readString();
  file.close();
  
  if(path.endsWith(".lua")) scriptType = 3;
  else if(path.endsWith(".py")) scriptType = 2;
  else scriptType = 1; // Default to ladder
  
  return true;
}

void executeScript() {
  if(scriptType == 0 || currentScript.length() == 0) return;
  
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
        if(arg == "DI0") acc = readDigital(0);
        else if(arg == "DI1") acc = readDigital(1);
      }
      else if(line.startsWith("AND ")) {
        String arg = line.substring(4);
        arg.trim();
        if(arg == "DI0") acc &= readDigital(0);
        else if(arg == "DI1") acc &= readDigital(1);
      }
      else if(line.startsWith("OR ")) {
        String arg = line.substring(3);
        arg.trim();
        if(arg == "DI0") acc |= readDigital(0);
        else if(arg == "DI1") acc |= readDigital(1);
      }
      else if(line.startsWith("OUT ")) {
        String arg = line.substring(4);
        arg.trim();
        if(arg == "DO0") writeDigital(0, acc);
        else if(arg == "DO1") writeDigital(1, acc);
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
        
        if(left == "DO0") writeDigital(0, right.toInt() != 0);
        else if(left == "DO1") writeDigital(1, right.toInt() != 0);
      }
    }
  }
}

// ========== Web Server Handlers ==========
void handleRoot() {
  if(!checkAuth(server)) return;
  
  String html = String(dashboardPage);
  
  // Replace placeholders with actual values
  html.replace("%WIFI_SSID%", config.wifiSSID);
  html.replace("%WIFI_PASSWORD%", config.wifiPassword);
  html.replace("%MODBUS_SLAVE_ID%", String(config.modbusSlaveId));
  html.replace("%WIFI_MODE%", config.isAPMode ? "Access Point" : "Station");
  
  // Set selected baud rate
  html.replace("%MODBUS_9600%", config.modbusBaudRate == 9600 ? "selected" : "");
  html.replace("%MODBUS_19200%", config.modbusBaudRate == 19200 ? "selected" : "");
  html.replace("%MODBUS_38400%", config.modbusBaudRate == 38400 ? "selected" : "");
  html.replace("%MODBUS_57600%", config.modbusBaudRate == 57600 ? "selected" : "");
  html.replace("%MODBUS_115200%", config.modbusBaudRate == 115200 ? "selected" : "");
  
  server.send(200, "text/html", html);
}

void handleLogin() {
  if(server.hasArg("username") && server.hasArg("password")) {
    String username = server.arg("username");
    String password = server.arg("password");
    
    bool authenticated = false;
    for(int i=0; i<config.userCount; i++) {
      if(username == config.users[i].username && sha256(password) == config.users[i].passwordHash) {
        authenticated = true;
        break;
      }
    }
    
    if(authenticated) {
      server.sendHeader("Location", "/");
      server.send(302);
      return;
    }
  }
  
  server.sendHeader("Location", "/login?error=1");
  server.send(302);
}

void handleLoginPage() {
  server.send(200, "text/html", loginPage);
}

void handleLogout() {
  server.sendHeader("Location", "/login");
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("Set-Cookie", "ESPSESSIONID=0");
  server.send(301);
}

void handleStatus() {
  if(!checkAuth(server)) return;
  
  String json = "{";
  
  // Digital inputs
  for(int i=0; i<8; i++) {
    json += "\"di" + String(i) + "\":" + String(readDigital(i)) + ",";
  }
  
  // Digital outputs
  for(int i=0; i<8; i++) {
    json += "\"do" + String(i) + "\":" + String(digitalRead(i == 0 ? DO0 : 
                                                         i == 1 ? DO1 :
                                                         i == 2 ? DO2 :
                                                         i == 3 ? DO3 :
                                                         i == 4 ? DO4 :
                                                         i == 5 ? DO5 :
                                                         i == 6 ? DO6 : DO7)) + ",";
  }
  
  // Analog inputs
  for(int i=0; i<4; i++) {
    json += "\"ai" + String(i) + "\":" + String(readAnalog(i)) + ",";
  }
  
  // Analog outputs (PWM)
  uint32_t duty0 = ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  uint32_t duty1 = ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  json += "\"ao0\":" + String(duty0) + ",";
  json += "\"ao1\":" + String(duty1) + ",";
  
  // System status
  json += "\"wifiConnected\":" + String(config.wifiConnected ? "true" : "false") + ",";
  json += "\"isAPMode\":" + String(config.isAPMode ? "true" : "false") + ",";
  json += "\"scriptType\":" + String(scriptType) + ",";
  json += "\"lastError\":\"" + config.lastError + "\"";
  
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleIOWrite() {
  if(!checkAuth(server)) return;
  
  // Handle digital outputs
  for(int i=0; i<8; i++) {
    if(server.hasArg("do" + String(i))) {
      writeDigital(i, server.arg("do" + String(i)).toInt() != 0);
    }
  }
  
  // Handle analog outputs
  for(int i=0; i<2; i++) {
    if(server.hasArg("ao" + String(i))) {
      writeAnalog(i, server.arg("ao" + String(i)).toInt());
    }
  }
  
  server.send(200, "text/plain", "Outputs updated");
}

void handleScriptList() {
  if(!checkAuth(server)) return;
  
  if(!config.sdCardPresent) {
    server.send(500, "application/json", "{\"error\":\"SD card not detected\"}");
    return;
  }

  // Add card check
  if(SD.cardType() == CARD_NONE) {
    server.send(500, "application/json", "{\"error\":\"No SD card present\"}");
    return;
  }

  String json = "{\"scripts\":[";
  bool first = true;
  
  File root = SD.open("/");
  if(!root) {
    server.send(500, "application/json", "{\"error\":\"Failed to open root directory\"}");
    return;
  }
  
  File file = root.openNextFile();
  while(file) {
    if(!file.isDirectory()) {
      if(!first) json += ",";
      first = false;
      
      String filename = String(file.name());
      // Skip system files
      if(filename.startsWith(".") || filename.equalsIgnoreCase("System Volume Information")) {
        file = root.openNextFile();
        continue;
      }

      String type = "Unknown";
      if(filename.endsWith(".lua")) type = "Lua";
      else if(filename.endsWith(".py")) type = "Python";
      else if(filename.endsWith(".txt")) type = "Ladder";
      
      json += "{\"name\":\"" + filename + "\",";
      json += "\"path\":\"" + filename + "\",";
      json += "\"type\":\"" + type + "\"}";
    }
    file = root.openNextFile();
  }
  
  root.close();
  json += "]}";
  server.send(200, "application/json", json);
}

void handleScriptGet() {
  if(!checkAuth(server)) return;
  
  if(!server.hasArg("path")) {
    server.send(400, "text/plain", "Missing path parameter");
    return;
  }
  
  if(!config.sdCardPresent) {
    server.send(500, "text/plain", "SD card not detected");
    return;
  }

  String path = server.arg("path");
  // Validate path
  if(path.length() > 64 || path.indexOf("..") >= 0) {
    server.send(400, "text/plain", "Invalid path");
    return;
  }

  if(!SD.exists(path.c_str())) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  
  File file = SD.open(path.c_str(), FILE_READ);
  if(!file) {
    server.send(500, "text/plain", "Failed to open file");
    return;
  }

  // Limit file size
  if(file.size() > 10240) { // 10KB max
    file.close();
    server.send(413, "text/plain", "File too large");
    return;
  }

  String content = file.readString();
  file.close();
  
  // Check for memory allocation failure
  if(content.length() == 0 && file.size() > 0) {
    server.send(500, "text/plain", "Memory allocation failed");
    return;
  }
  
  server.send(200, "text/plain", content);
}

void handleScriptSave() {
  if(!checkAuth(server)) return;
  
  if(!server.hasArg("path") || !server.hasArg("content")) {
    server.send(400, "text/plain", "Missing parameters");
    return;
  }
  
  if(!config.sdCardPresent) {
    server.send(500, "text/plain", "SD card not detected");
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
  
  if(!config.sdCardPresent) {
    server.send(500, "text/plain", "SD card not detected");
    return;
  }
  
  String path = server.arg("path");
  if(loadScript(path)) {
    server.send(200, "text/plain", "Script loaded successfully");
  } else {
    server.send(500, "text/plain", config.lastError);
  }
}

void handleScriptDelete() {
  if(!checkAuth(server)) return;
  
  if(server.method() != HTTP_DELETE || !server.hasArg("path")) {
    server.send(400, "text/plain", "Missing path parameter");
    return;
  }

  if(!config.sdCardPresent) {
    server.send(500, "text/plain", "SD card not detected");
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
    server.send(500, "text/plain", "Failed to delete file");
  }
}

void handleScriptUpload() {
  if(!checkAuth(server)) return;

  HTTPUpload& upload = server.upload();
  
  if(upload.status == UPLOAD_FILE_START) {
    if(SD.exists(upload.filename)) {
      SD.remove(upload.filename);
    }
    uploadFile = SD.open(upload.filename, FILE_WRITE);
    if(!uploadFile) {
      config.lastError = "Failed to create file";
    }
  } 
  else if(upload.status == UPLOAD_FILE_WRITE) {
    if(uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } 
  else if(upload.status == UPLOAD_FILE_END) {
    if(uploadFile) {
      uploadFile.close();
    }
    server.send(200, "text/plain", "Upload complete");
  }
}

void handleSettingsSave() {
  if(!checkAuth(server)) return;

  if(server.hasArg("ssid")) config.wifiSSID = server.arg("ssid");
  if(server.hasArg("password")) config.wifiPassword = server.arg("password");
  if(server.hasArg("slaveId")) config.modbusSlaveId = server.arg("slaveId").toInt();
  if(server.hasArg("baudrate")) config.modbusBaudRate = server.arg("baudrate").toInt();

  saveConfig();
  server.send(200, "text/plain", "Settings saved. Restart to apply.");
}

void handleSystemRestart() {
  if(!checkAuth(server)) return;
  
  server.send(200, "text/plain", "System restarting...");
  delay(1000);
  ESP.restart();
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  
  for(uint8_t i=0; i<server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  
  server.send(404, "text/plain", message);
}

// ========== Setup & Loop ==========
void setup() {
  Serial.begin(115200);
  
  // Initialize watchdog timer
  //esp_task_wdt_init(10, true); // 10 second timeout
  //esp_task_wdt_add(NULL); // Add current thread to watchdog

  // Initialize EEPROM and load config
  EEPROM.begin(512);
  loadConfig();
  
  // Initialize SD card with error handling
  if(!SD.begin(SD_CS_PIN)) {
    config.sdCardPresent = false;
    config.lastError = "SD card initialization failed";
    Serial.println("SD Card initialization failed!");
  } else {
    config.sdCardPresent = true;
    Serial.println("SD Card initialized successfully");
  }
  
  // Initialize I/O
  setupIO();
  
  // Initialize Modbus
  setupModbus();
  
  // Connect to WiFi or start AP
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  //WiFi.begin(config.wifiSSID.c_str(), config.wifiPassword.c_str());
  
  Serial.print("Connecting to WiFi");
  unsigned long startTime = millis();
  
  // Wait for connection with timeout
  while(WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
    delay(500);
    Serial.print(".");
  }
  
  if(WiFi.status() != WL_CONNECTED) {
    // Fall back to AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAP(DEFAULT_SSID, DEFAULT_PASSWORD);
    config.wifiConnected = true;
    config.isAPMode = true;
    Serial.println("\nFailed to connect to WiFi, starting AP mode");
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
  } else {
    config.wifiConnected = true;
    config.isAPMode = false;
    Serial.println("\nConnected to WiFi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
  
  // Start web server
  server.on("/", handleRoot);
  server.on("/login", HTTP_GET, handleLoginPage);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/logout", handleLogout);
  server.on("/api/status", handleStatus);
  server.on("/api/io/write", handleIOWrite);
  server.on("/api/scripts/list", handleScriptList);
  server.on("/api/script/get", handleScriptGet);
  server.on("/api/script/save", HTTP_POST, handleScriptSave);
  server.on("/api/script/load", handleScriptLoad);
  server.on("/api/script/delete", HTTP_DELETE, handleScriptDelete);
  server.on("/api/script/upload", HTTP_POST, [](){
    server.send(200, "text/plain", "");
  }, handleScriptUpload);
  server.on("/api/settings/save", HTTP_POST, handleSettingsSave);
  server.on("/api/system/restart", HTTP_POST, handleSystemRestart);
  server.onNotFound(handleNotFound);
  
  // Start server
  server.begin();
  Serial.println("HTTP server started");
  
  // Load default script if available
  if(config.sdCardPresent && SD.exists(config.defaultLogicPath.c_str())) {
    if(loadScript(config.defaultLogicPath)) {
      Serial.println("Default script loaded successfully");
    } else {
      Serial.println("Failed to load default script: " + config.lastError);
    }
  }
}

void loop() {
  // Reset watchdog timer
  //esp_task_wdt_reset();
  
  server.handleClient();
  updateModbus();
  
  // Execute script if loaded
  if(scriptType != 0) {
    executeScript();
  }
  
  // Handle WiFi reconnection (only in STA mode)
  static unsigned long lastWifiCheck = 0;
  if(!config.isAPMode && millis() - lastWifiCheck > 10000) {
    lastWifiCheck = millis();
    
    if(WiFi.status() != WL_CONNECTED) {
      config.wifiConnected = false;
      WiFi.reconnect();
      delay(1000); // Give some time to reconnect
      
      if(WiFi.status() == WL_CONNECTED) {
        config.wifiConnected = true;
        Serial.println("Reconnected to WiFi");
      }
    } else if(!config.wifiConnected) {
      config.wifiConnected = true;
      Serial.println("WiFi reconnected");
    }
  }
}
