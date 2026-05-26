/*
 * ESP32 Master - Temperature & Humidity Controller
 * Receives data from ESP8266 nodes via HTTP POST
 * Stores data on SD card and serves web dashboard
 * 
 * Hardware:
 * - ESP32 DevKit
 * - MicroSD Card Module
 * - Optional LCD/OLED for local display
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>

// WiFi credentials
//const char *ssid = "PSI_Corp";
//const char *password = "Pass1234";

const char *ssid = "Airtel_mani_4630";
const char *password = "Air@66825";

//const char* ssid = "Mr.GK.Mani";
//const char* password = "Mr.Gk1992";

// Web server on port 80
WebServer server(80);

// Static IP configuration
IPAddress local_IP(192, 168, 1, 10);    // Set your desired static IP
IPAddress gateway(192, 168, 1, 1);       // Set your network gateway
IPAddress subnet(255, 255, 255, 0);      // Set your subnet mask
IPAddress dns(8, 8, 8, 8);               // (Optional) Set DNS

// SD card pins (adjust according to your wiring)
#define SD_CS 5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK 18

// Data storage arrays for recent readings
struct SensorData {
  String nodeId;
  float temperature;
  float humidity;
  unsigned long timestamp;
  bool isValid;
};

// Store last 100 readings per node (circular buffer)
const int MAX_READINGS = 100;
const int MAX_NODES = 5;
SensorData sensorBuffer[MAX_NODES][MAX_READINGS];
int bufferIndex[MAX_NODES] = {0};
String nodeIds[MAX_NODES];
int nodeCount = 0;

// NTP Time configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;

void handleReadingsAPI();
void handleNodesAPI();
void handleCSS();
void handleJS();
void handleNotFound();


void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Master Temperature & Humidity Controller");

  // Initialize SD card
  if (!initSDCard()) {
    Serial.println("SD Card initialization failed!");
  }

  // Connect to WiFi
  connectWiFi();

  // Configure time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Setup web server routes
  setupWebServer();

  Serial.println("Master Controller Ready");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  server.handleClient();
  delay(10);
}

bool initSDCard() {
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  
  if (!SD.begin(SD_CS)) {
    return false;
  }

  // Create data file with headers if it doesn't exist
  if (!SD.exists("/sensor_data.csv")) {
    File file = SD.open("/sensor_data.csv", FILE_WRITE);
    if (file) {
      file.println("Timestamp,NodeID,Temperature,Humidity");
      file.close();
      Serial.println("Created sensor_data.csv");
    }
  }
  return true;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);

  // Static IP for STA mode
  WiFi.config(local_IP, gateway, subnet, dns);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi as Station!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect. Switching to Access Point Mode...");

    WiFi.mode(WIFI_AP);

    // Static IP for AP Mode
    WiFi.softAPConfig(local_IP, gateway, subnet);

    WiFi.softAP("SysCon", "12345678"); // AP SSID & password

    Serial.println("AP Mode Started!");
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
  }

  Serial.println();
  Serial.print("Connected to: ");
  Serial.println(ssid);
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void setupWebServer() {
  // API endpoint to receive data from ESP8266 nodes
  server.on("/api/data", HTTP_POST, handleSensorData);

  // Dashboard endpoints
  server.on("/", HTTP_GET, handleDashboard);
  server.on("/api/readings", HTTP_GET, handleReadingsAPI);
  server.on("/api/nodes", HTTP_GET, handleNodesAPI);

  // Static files
  server.on("/style.css", HTTP_GET, handleCSS);
  server.on("/script.js", HTTP_GET, handleJS);

  // Handle not found
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Web server started");
}

void handleSensorData() {
  if (server.hasArg("plain") == false) {
    server.send(400, "application/json", "{\"error\": \"No body\"}");
    return;
  }

  String body = server.arg("plain");
  Serial.println("Received data: " + body);

  // Parse JSON
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    server.send(400, "application/json", "{\"error\": \"Invalid JSON\"}");
    return;
  }

  // Extract data
  String nodeId = doc["nodeId"].as<String>();
  float temperature = doc["temperature"];
  float humidity = doc["humidity"];

  if (nodeId.length() == 0 || isnan(temperature) || isnan(humidity)) {
    server.send(400, "application/json", "{\"error\": \"Invalid data\"}");
    return;
  }

  // Store data
  storeSensorData(nodeId, temperature, humidity);

  // Log to SD card
  logToSD(nodeId, temperature, humidity);

  // Send success response
  server.send(200, "application/json", "{\"status\": \"success\"}");
}

void storeSensorData(String nodeId, float temperature, float humidity) {
  // Find or create node index
  int nodeIndex = -1;
  for (int i = 0; i < nodeCount; i++) {
    if (nodeIds[i] == nodeId) {
      nodeIndex = i;
      break;
    }
  }

  if (nodeIndex == -1 && nodeCount < MAX_NODES) {
    nodeIndex = nodeCount;
    nodeIds[nodeIndex] = nodeId;
    nodeCount++;
  }

  if (nodeIndex >= 0) {
    // Store in circular buffer
    sensorBuffer[nodeIndex][bufferIndex[nodeIndex]].nodeId = nodeId;
    sensorBuffer[nodeIndex][bufferIndex[nodeIndex]].temperature = temperature;
    sensorBuffer[nodeIndex][bufferIndex[nodeIndex]].humidity = humidity;
    sensorBuffer[nodeIndex][bufferIndex[nodeIndex]].timestamp = millis();
    sensorBuffer[nodeIndex][bufferIndex[nodeIndex]].isValid = true;

    bufferIndex[nodeIndex] = (bufferIndex[nodeIndex] + 1) % MAX_READINGS;
  }
}

void logToSD(String nodeId, float temperature, float humidity) {
  File file = SD.open("/sensor_data.csv", FILE_APPEND);
  if (file) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char timestamp[64];
      strftime(timestamp, 64, "%Y-%m-%d %H:%M:%S", &timeinfo);

      file.print(timestamp);
      file.print(",");
      file.print(nodeId);
      file.print(",");
      file.print(temperature, 2);
      file.print(",");
      file.println(humidity, 2);
    }
    file.close();
    Serial.println("Data logged to SD card");
  } else {
    Serial.println("Error opening file for writing");
  }
}

void handleDashboard() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Ambient Monitoring Dashboard</title>
    <link rel="stylesheet" href="/style.css">
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
</head>
<body>
    <div class="container">
        <header class="header">
            <div class="header-content">
                <h1><i class="fas fa-temperature-high"></i> Ambient Monitoring System</h1>
                <div class="status-indicator">
                    <span class="status-dot" id="statusDot"></span>
                    <span id="statusText">Connecting...</span>
                </div>
            </div>
            <p class="subtitle">Real-time temperature and humidity monitoring</p>
        </header>

        <div class="dashboard-grid">
            <div class="sensors-panel" id="sensorsPanel">
                <h2><i class="fas fa-sensor"></i> Sensor Nodes</h2>
                <div class="sensors-container" id="sensorsContainer">
                    <div class="sensor-placeholder">
                        <i class="fas fa-search"></i>
                        <p>Searching for sensors...</p>
                    </div>
                </div>
            </div>

            <div class="charts-panel">
                <div class="chart-card">
                    <div class="chart-header">
                        <h3><i class="fas fa-thermometer-half"></i> Temperature Trends</h3>
                        <div class="time-filter">
                            <button class="time-btn active" data-hours="24">24H</button>
                            <button class="time-btn" data-hours="12">12H</button>
                            <button class="time-btn" data-hours="6">6H</button>
                        </div>
                    </div>
                    <div class="chart-container">
                        <canvas id="temperatureChart"></canvas>
                    </div>
                </div>

                <div class="chart-card">
                    <div class="chart-header">
                        <h3><i class="fas fa-tint"></i> Humidity Trends</h3>
                        <div class="time-filter">
                            <button class="time-btn active" data-hours="24">24H</button>
                            <button class="time-btn" data-hours="12">12H</button>
                            <button class="time-btn" data-hours="6">6H</button>
                        </div>
                    </div>
                    <div class="chart-container">
                        <canvas id="humidityChart"></canvas>
                    </div>
                </div>
            </div>
        </div>

        <div class="actions-panel">
            <button class="action-btn primary" onclick="refreshData()">
                <i class="fas fa-sync-alt"></i> Refresh Data
            </button>
            <button class="action-btn secondary" onclick="downloadData()">
                <i class="fas fa-download"></i> Export Data
            </button>
            <button class="action-btn" onclick="showSettings()">
                <i class="fas fa-cog"></i> Settings
            </button>
        </div>
    </div>

    <div class="modal" id="settingsModal">
        <div class="modal-content">
            <div class="modal-header">
                <h2>System Settings</h2>
                <span class="close" onclick="hideSettings()">&times;</span>
            </div>
            <div class="modal-body">
                <div class="setting-item">
                    <label>Update Interval</label>
                    <select id="updateInterval">
                        <option value="10">10 seconds</option>
                        <option value="30" selected>30 seconds</option>
                        <option value="60">1 minute</option>
                    </select>
                </div>
                <div class="setting-item">
                    <label>Temperature Unit</label>
                    <select id="tempUnit">
                        <option value="c" selected>°C</option>
                        <option value="f">°F</option>
                    </select>
                </div>
            </div>
        </div>
    </div>

    <script src="/script.js"></script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleCSS() {
  String css = R"rawliteral(
:root {
    --primary: #4361ee;
    --secondary: #3a0ca3;
    --success: #4cc9f0;
    --warning: #f72585;
    --info: #7209b7;
    --light: #f8f9fa;
    --dark: #212529;
    --gray: #6c757d;
    --bg-light: #f1f5f9;
    --card-shadow: 0 4px 20px rgba(0, 0, 0, 0.08);
}

* {
    margin: 0;
    padding: 0;
    box-sizing: border-box;
}

body {
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    background: var(--bg-light);
    color: var(--dark);
    line-height: 1.6;
}

.container {
    max-width: 1400px;
    margin: 0 auto;
    padding: 20px;
}

.header {
    background: linear-gradient(135deg, var(--primary), var(--secondary));
    color: white;
    padding: 25px;
    border-radius: 16px;
    margin-bottom: 25px;
    box-shadow: var(--card-shadow);
}

.header-content {
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 15px;
}

.header h1 {
    font-size: 2.2rem;
    display: flex;
    align-items: center;
    gap: 12px;
}

.subtitle {
    margin-top: 10px;
    opacity: 0.9;
    font-size: 1.1rem;
}

.status-indicator {
    display: flex;
    align-items: center;
    gap: 8px;
    background: rgba(255, 255, 255, 0.2);
    padding: 8px 16px;
    border-radius: 20px;
}

.status-dot {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    background: #ff6b6b;
}

.status-dot.online {
    background: #51cf66;
    animation: pulse 1.5s infinite;
}

@keyframes pulse {
    0% { opacity: 1; }
    50% { opacity: 0.5; }
    100% { opacity: 1; }
}

.dashboard-grid {
    display: grid;
    grid-template-columns: 1fr 2fr;
    gap: 25px;
    margin-bottom: 25px;
}

@media (max-width: 1024px) {
    .dashboard-grid {
        grid-template-columns: 1fr;
    }
}

.sensors-panel, .charts-panel {
    background: white;
    border-radius: 16px;
    padding: 25px;
    box-shadow: var(--card-shadow);
}

.sensors-panel h2, .chart-header h3 {
    margin-bottom: 20px;
    color: var(--dark);
    display: flex;
    align-items: center;
    gap: 10px;
}

.sensors-container {
    display: flex;
    flex-direction: column;
    gap: 15px;
}

.sensor-card {
    background: var(--light);
    border-radius: 12px;
    padding: 20px;
    display: flex;
    flex-direction: column;
    gap: 15px;
    transition: transform 0.2s, box-shadow 0.2s;
}

.sensor-card:hover {
    transform: translateY(-2px);
    box-shadow: 0 6px 15px rgba(0, 0, 0, 0.1);
}

.sensor-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
}

.sensor-name {
    font-weight: 600;
    font-size: 1.1rem;
    display: flex;
    align-items: center;
    gap: 8px;
}

.sensor-status {
    font-size: 0.85rem;
    padding: 4px 10px;
    border-radius: 12px;
    background: var(--success);
    color: white;
}

.sensor-data {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 15px;
}

.data-item {
    display: flex;
    flex-direction: column;
    gap: 5px;
}

.data-label {
    font-size: 0.9rem;
    color: var(--gray);
}

.data-value {
    font-size: 1.4rem;
    font-weight: 700;
}

.temp-value {
    color: #e74c3c;
}

.hum-value {
    color: #3498db;
}

.sensor-placeholder {
    text-align: center;
    padding: 40px 20px;
    color: var(--gray);
}

.sensor-placeholder i {
    font-size: 3rem;
    margin-bottom: 15px;
    opacity: 0.5;
}

.charts-panel {
    display: flex;
    flex-direction: column;
    gap: 25px;
}

.chart-card {
    display: flex;
    flex-direction: column;
}

.chart-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 20px;
    flex-wrap: wrap;
    gap: 15px;
}

.time-filter {
    display: flex;
    gap: 8px;
    background: var(--light);
    padding: 4px;
    border-radius: 12px;
}

.time-btn {
    padding: 6px 12px;
    border: none;
    background: none;
    border-radius: 8px;
    cursor: pointer;
    font-size: 0.9rem;
    transition: background 0.2s;
}

.time-btn.active {
    background: var(--primary);
    color: white;
}

.chart-container {
    height: 300px;
    position: relative;
}

.actions-panel {
    display: flex;
    gap: 15px;
    justify-content: center;
    flex-wrap: wrap;
}

.action-btn {
    padding: 12px 24px;
    border: none;
    border-radius: 12px;
    cursor: pointer;
    font-weight: 600;
    display: flex;
    align-items: center;
    gap: 8px;
    transition: all 0.2s;
}

.action-btn.primary {
    background: var(--primary);
    color: white;
}

.action-btn.secondary {
    background: var(--secondary);
    color: white;
}

.action-btn:hover {
    transform: translateY(-2px);
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}

.modal {
    display: none;
    position: fixed;
    top: 0;
    left: 0;
    width: 100%;
    height: 100%;
    background: rgba(0, 0, 0, 0.5);
    z-index: 1000;
    align-items: center;
    justify-content: center;
}

.modal-content {
    background: white;
    border-radius: 16px;
    width: 90%;
    max-width: 500px;
    overflow: hidden;
}

.modal-header {
    background: var(--primary);
    color: white;
    padding: 20px;
    display: flex;
    justify-content: space-between;
    align-items: center;
}

.modal-header .close {
    font-size: 1.8rem;
    cursor: pointer;
}

.modal-body {
    padding: 25px;
}

.setting-item {
    margin-bottom: 20px;
}

.setting-item label {
    display: block;
    margin-bottom: 8px;
    font-weight: 600;
}

.setting-item select {
    width: 100%;
    padding: 10px;
    border-radius: 8px;
    border: 1px solid #ddd;
}

@media (max-width: 768px) {
    .container {
        padding: 15px;
    }
    
    .header h1 {
        font-size: 1.8rem;
    }
    
    .sensor-data {
        grid-template-columns: 1fr;
    }
    
    .actions-panel {
        flex-direction: column;
    }
    
    .action-btn {
        justify-content: center;
    }
}
)rawliteral";

  server.send(200, "text/css", css);
}

void handleJS() {
  String js = R"rawliteral(
let tempChart, humChart;
let updateInterval = 30000;
let tempUnit = 'c';

document.addEventListener('DOMContentLoaded', function() {
    initCharts();
    loadData();
    setupEventListeners();
    
    // Start periodic updates
    setInterval(loadData, updateInterval);
});

function initCharts() {
    const tempCtx = document.getElementById('temperatureChart').getContext('2d');
    const humCtx = document.getElementById('humidityChart').getContext('2d');
    
    const chartOptions = {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
            legend: {
                position: 'top',
            },
            tooltip: {
                mode: 'index',
                intersect: false
            }
        },
        scales: {
            x: {
                grid: {
                    display: false
                }
            },
            y: {
                grid: {
                    color: 'rgba(0, 0, 0, 0.05)'
                }
            }
        },
        elements: {
            line: {
                tension: 0.3
            },
            point: {
                radius: 0,
                hoverRadius: 6
            }
        },
        interaction: {
            mode: 'nearest',
            axis: 'x',
            intersect: false
        }
    };
    
    tempChart = new Chart(tempCtx, {
        type: 'line',
        data: {
            labels: [],
            datasets: []
        },
        options: {
            ...chartOptions,
            plugins: {
                ...chartOptions.plugins,
                title: {
                    display: false,
                    text: 'Temperature'
                }
            }
        }
    });
    
    humChart = new Chart(humCtx, {
        type: 'line',
        data: {
            labels: [],
            datasets: []
        },
        options: {
            ...chartOptions,
            plugins: {
                ...chartOptions.plugins,
                title: {
                    display: false,
                    text: 'Humidity'
                }
            },
            scales: {
                ...chartOptions.scales,
                y: {
                    ...chartOptions.scales.y,
                    min: 0,
                    max: 100
                }
            }
        }
    });
}

function setupEventListeners() {
    // Time filter buttons
    document.querySelectorAll('.time-btn').forEach(btn => {
        btn.addEventListener('click', function() {
            document.querySelectorAll('.time-btn').forEach(b => b.classList.remove('active'));
            this.classList.add('active');
            loadData();
        });
    });
    
    // Settings changes
    document.getElementById('updateInterval').addEventListener('change', function() {
        updateInterval = this.value * 1000;
        clearInterval(window.updateIntervalId);
        window.updateIntervalId = setInterval(loadData, updateInterval);
    });
    
    document.getElementById('tempUnit').addEventListener('change', function() {
        tempUnit = this.value;
        updateSensorCards(window.currentNodes);
    });
}

async function loadData() {
    try {
        const [nodesResponse, readingsResponse] = await Promise.all([
            fetch('/api/nodes'),
            fetch('/api/readings')
        ]);
        
        if (!nodesResponse.ok || !readingsResponse.ok) {
            throw new Error('API request failed');
        }
        
        const nodes = await nodesResponse.json();
        const readings = await readingsResponse.json();
        
        updateSensorCards(nodes);
        updateCharts(readings);
        updateStatus('online');
        
        // Store for later use
        window.currentNodes = nodes;
        window.currentReadings = readings;
        
    } catch (error) {
        console.error('Error loading data:', error);
        updateStatus('offline');
    }
}

function updateSensorCards(nodes) {
    const container = document.getElementById('sensorsContainer');
    
    if (!nodes || nodes.length === 0) {
        container.innerHTML = `
            <div class="sensor-placeholder">
                <i class="fas fa-search"></i>
                <p>No sensors detected. Waiting for data...</p>
            </div>
        `;
        return;
    }
    
    container.innerHTML = nodes.map(node => {
        const temp = tempUnit === 'c' ? 
            `${node.temperature.toFixed(1)}°C` : 
            `${(node.temperature * 9/5 + 32).toFixed(1)}°F`;
            
        return `
            <div class="sensor-card">
                <div class="sensor-header">
                    <div class="sensor-name">
                        <i class="fas fa-microchip"></i>
                        ${node.nodeId}
                    </div>
                    <div class="sensor-status">Online</div>
                </div>
                <div class="sensor-data">
                    <div class="data-item">
                        <span class="data-label">Temperature</span>
                        <span class="data-value temp-value">${temp}</span>
                    </div>
                    <div class="data-item">
                        <span class="data-label">Humidity</span>
                        <span class="data-value hum-value">${node.humidity.toFixed(1)}%</span>
                    </div>
                </div>
                <div class="sensor-footer">
                    <small>Last update: ${formatRelativeTime(node.lastUpdate)}</small>
                </div>
            </div>
        `;
    }).join('');
}

function updateCharts(readings) {
    if (!readings || !readings.labels || !readings.nodes) return;
    
    const colors = [
        '#4361ee', '#f72585', '#4cc9f0', '#7209b7', '#3a0ca3',
        '#ff6b6b', '#51cf66', '#fcc419', '#ae3ec9', '#228be6'
    ];
    
    // Update temperature chart
    tempChart.data.labels = readings.labels;
    tempChart.data.datasets = readings.nodes.map((node, index) => {
        let data = node.temperature;
        if (tempUnit === 'f') {
            data = data.map(temp => temp * 9/5 + 32);
        }
        
        return {
            label: node.nodeId,
            data: data,
            borderColor: colors[index % colors.length],
            backgroundColor: colors[index % colors.length] + '20',
            borderWidth: 2,
            fill: false
        };
    });
    tempChart.update();
    
    // Update humidity chart
    humChart.data.labels = readings.labels;
    humChart.data.datasets = readings.nodes.map((node, index) => ({
        label: node.nodeId,
        data: node.humidity,
        borderColor: colors[index % colors.length],
        backgroundColor: colors[index % colors.length] + '20',
        borderWidth: 2,
        fill: false
    }));
    humChart.update();
}

function updateStatus(status) {
    const statusDot = document.getElementById('statusDot');
    const statusText = document.getElementById('statusText');
    
    if (status === 'online') {
        statusDot.className = 'status-dot online';
        statusText.textContent = 'Online';
    } else {
        statusDot.className = 'status-dot';
        statusText.textContent = 'Offline';
    }
}

function formatRelativeTime(timestamp) {
    const now = Date.now();
    const diff = now - timestamp;
    
    if (diff < 60000) return 'Just now';
    if (diff < 3600000) return `${Math.floor(diff / 60000)}m ago`;
    if (diff < 86400000) return `${Math.floor(diff / 3600000)}h ago`;
    return `${Math.floor(diff / 86400000)}d ago`;
}

function refreshData() {
    loadData();
    showNotification('Data refreshed', 'success');
}

function downloadData() {
    const link = document.createElement('a');
    link.href = '/sensor_data.csv';
    link.download = 'sensor_data.csv';
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    showNotification('Download started', 'success');
}

function showSettings() {
    document.getElementById('settingsModal').style.display = 'flex';
}

function hideSettings() {
    document.getElementById('settingsModal').style.display = 'none';
}

function showNotification(message, type) {
    // Create notification element
    const notification = document.createElement('div');
    notification.className = `notification ${type}`;
    notification.innerHTML = `
        <span>${message}</span>
        <button onclick="this.parentElement.remove()">&times;</button>
    `;
    
    // Add styles if not already added
    if (!document.getElementById('notification-styles')) {
        const styles = document.createElement('style');
        styles.id = 'notification-styles';
        styles.textContent = `
            .notification {
                position: fixed;
                top: 20px;
                right: 20px;
                padding: 15px 20px;
                border-radius: 8px;
                color: white;
                display: flex;
                align-items: center;
                gap: 10px;
                z-index: 1001;
                animation: slideIn 0.3s ease;
            }
            .notification.success { background: #51cf66; }
            .notification.error { background: #ff6b6b; }
            .notification button {
                background: none;
                border: none;
                color: white;
                cursor: pointer;
                font-size: 1.2rem;
            }
            @keyframes slideIn {
                from { transform: translateX(100px); opacity: 0; }
                to { transform: translateX(0); opacity: 1; }
            }
        `;
        document.head.appendChild(styles);
    }
    
    document.body.appendChild(notification);
    
    // Auto remove after 3 seconds
    setTimeout(() => {
        if (notification.parentElement) {
            notification.remove();
        }
    }, 3000);
}

// Close modal if clicked outside
window.addEventListener('click', function(event) {
    const modal = document.getElementById('settingsModal');
    if (event.target === modal) {
        hideSettings();
    }
});
)rawliteral";

  server.send(200, "application/javascript", js);
}

void handleNodesAPI() {
  DynamicJsonDocument doc(2048);
  JsonArray nodesArray = doc.to<JsonArray>();

  for (int i = 0; i < nodeCount; i++) {
    // Find the most recent reading
    int lastIndex = (bufferIndex[i] - 1 + MAX_READINGS) % MAX_READINGS;
    if (sensorBuffer[i][lastIndex].isValid) {
      JsonObject node = nodesArray.createNestedObject();
      node["nodeId"] = sensorBuffer[i][lastIndex].nodeId;
      node["temperature"] = sensorBuffer[i][lastIndex].temperature;
      node["humidity"] = sensorBuffer[i][lastIndex].humidity;
      node["lastUpdate"] = sensorBuffer[i][lastIndex].timestamp;
    }
  }

  String response;
  serializeJson(nodesArray, response);
  server.send(200, "application/json", response);
}

void handleReadingsAPI() {
  DynamicJsonDocument doc(8192);
  JsonArray nodesArray = doc.createNestedArray("nodes");
  JsonArray labelsArray = doc.createNestedArray("labels");

  // Generate time labels for last 24 readings
  for (int i = 23; i >= 0; i--) {
    labelsArray.add(String(i) + "h ago");
  }

  for (int nodeIdx = 0; nodeIdx < nodeCount; nodeIdx++) {
    JsonObject nodeObj = nodesArray.createNestedObject();
    nodeObj["nodeId"] = nodeIds[nodeIdx];

    JsonArray tempArray = nodeObj.createNestedArray("temperature");
    JsonArray humArray = nodeObj.createNestedArray("humidity");

    // Get last 24 readings
    int startIdx = (bufferIndex[nodeIdx] - 24 + MAX_READINGS) % MAX_READINGS;
    for (int i = 0; i < 24; i++) {
      int idx = (startIdx + i) % MAX_READINGS;
      if (sensorBuffer[nodeIdx][idx].isValid) {
        tempArray.add(sensorBuffer[nodeIdx][idx].temperature);
        humArray.add(sensorBuffer[nodeIdx][idx].humidity);
      } else {
        tempArray.add((float)0.0);
        humArray.add((float)0.0);
      }
    }
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
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
  
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  
  server.send(404, "text/plain", message);
}
