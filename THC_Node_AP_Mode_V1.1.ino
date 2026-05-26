/*
 * ESP8266 Room Node - Temperature & Humidity Sensor
 * Reads DHT22 sensor and POSTs data to ESP32 Master
 * 
 * Hardware:
 * - ESP8266 (NodeMCU/Wemos D1 Mini)
 * - DHT22 sensor
 * - 10kΩ pull-up resistor (if not included in DHT22 module)
 * 
 * Connections:
 * - DHT22 VCC -> 3.3V
 * - DHT22 GND -> GND
 * - DHT22 DATA -> D4 (GPIO2)
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// STA credentials
const char *ssid = "SysCon";
const char *password = "12345678";

// ESP32 Master IP (update after ESP32 gets IP address)
const char* masterIP = "192.168.4.1";  // Replace with actual ESP32 IP
const int masterPort = 80;
const char* apiEndpoint = "/api/data";

// DHT22 Configuration
#define DHT_PIN 2        // GPIO2 (D4 on NodeMCU)
#define DHT_TYPE DHT11   // Changed from DHT11 to DHT22
DHT dht(DHT_PIN, DHT_TYPE);

// Node Configuration
const String NODE_ID = "Room_03";  // Unique identifier for this node
const unsigned long SEND_INTERVAL = 30000; // 30 seconds
const unsigned long RETRY_INTERVAL = 5000;  // 5 seconds for retry
const unsigned long WIFI_RECONNECT_INTERVAL = 30000; // 30 seconds for WiFi reconnect

// Status variables
unsigned long lastSendTime = 0;
unsigned long lastRetryTime = 0;
unsigned long lastWifiReconnectAttempt = 0;
bool lastSendSuccess = true;
int failedAttempts = 0;
const int MAX_FAILED_ATTEMPTS = 5;

// Built-in LED for status indication
#define STATUS_LED LED_BUILTIN

// Sensor reading retry
const int SENSOR_READ_RETRIES = 3;
const unsigned long SENSOR_READ_DELAY = 500;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("ESP8266 Room Node Starting...");
  Serial.println("Node ID: " + NODE_ID);

  // Initialize status LED
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH); // LED off (inverted logic)

  // Initialize DHT sensor
  dht.begin();
  Serial.println("DHT22 sensor initialized");

  // Connect to WiFi
  connectWiFi();

  // Test initial sensor reading
  testSensor();

  Serial.println("Room Node Ready");
  delay(3000);
}

void loop() {
  unsigned long currentTime = millis();

  // Check WiFi connection and reconnect if needed
  if (WiFi.status() != WL_CONNECTED) {
    if (currentTime - lastWifiReconnectAttempt >= WIFI_RECONNECT_INTERVAL) {
      Serial.println("WiFi disconnected, attempting reconnection...");
      connectWiFi();
      lastWifiReconnectAttempt = currentTime;
    }
  }

  // Check if it's time to send data or retry failed attempt
  bool shouldSend = (currentTime - lastSendTime >= SEND_INTERVAL);
  bool shouldRetry = (!lastSendSuccess && currentTime - lastRetryTime >= RETRY_INTERVAL);

  if ((shouldSend || shouldRetry) && WiFi.status() == WL_CONNECTED) {
    sendSensorData();
  }

  // Update status LED
  updateStatusLED();

  delay(100);
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(1000);

  IPAddress localIP(192,168,4,22);   // CHANGE for each node
  IPAddress gateway(192,168,4,1);
  IPAddress subnet(255,255,255,0);
  WiFi.config(localIP, gateway, subnet);

  Serial.print("Connecting to ESP32 AP: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Connected to ESP32 AP");
    Serial.print("Node IP: ");
    Serial.println(WiFi.localIP());
    digitalWrite(STATUS_LED, HIGH);
  } else {
    Serial.println("\n❌ Failed to connect to ESP32 AP");
  }
}

void testSensor() {
  Serial.println("Testing DHT22 sensor...");

  float temperature = readTemperatureWithRetry();
  float humidity = readHumidityWithRetry();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("❌ DHT22 sensor test failed!");
    Serial.println("Check wiring and connections");
  } else {
    Serial.println("✅ DHT22 sensor test passed");
    Serial.printf("Temperature: %.1f°C, Humidity: %.1f%%\n", temperature, humidity);
  }
}

float readTemperatureWithRetry() {
  for (int i = 0; i < SENSOR_READ_RETRIES; i++) {
    float temp = dht.readTemperature();
    if (!isnan(temp)) return temp;
    delay(SENSOR_READ_DELAY);
  }
  return NAN;
}

float readHumidityWithRetry() {
  for (int i = 0; i < SENSOR_READ_RETRIES; i++) {
    float hum = dht.readHumidity();
    if (!isnan(hum)) return hum;
    delay(SENSOR_READ_DELAY);
  }
  return NAN;
}

void sendSensorData() {
  Serial.println("\n--- Reading Sensor Data ---");

  // Read sensor data with retry
  float temperature = readTemperatureWithRetry();
  float humidity = readHumidityWithRetry();

  // Check if readings are valid
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("❌ Failed to read from DHT sensor after multiple attempts!");
    lastSendSuccess = false;
    lastRetryTime = millis();
    failedAttempts++;
    return;
  }

  Serial.printf("Temperature: %.2f°C\n", temperature);
  Serial.printf("Humidity: %.2f%%\n", humidity);

  // Create JSON payload
  DynamicJsonDocument doc(200);
  doc["nodeId"] = NODE_ID;
  doc["temperature"] = round(temperature * 100.0) / 100.0; // Round to 2 decimal places
  doc["humidity"] = round(humidity * 100.0) / 100.0;
  doc["timestamp"] = millis();

  String jsonString;
  serializeJson(doc, jsonString);

  Serial.println("Payload: " + jsonString);

  // Send HTTP POST request
  if (sendHTTPPost(jsonString)) {
    Serial.println("✅ Data sent successfully");
    lastSendSuccess = true;
    lastSendTime = millis();
    failedAttempts = 0;
  } else {
    Serial.println("❌ Failed to send data");
    lastSendSuccess = false;
    lastRetryTime = millis();
    failedAttempts++;

    if (failedAttempts >= MAX_FAILED_ATTEMPTS) {
      Serial.println("Max failed attempts reached, restarting...");
      ESP.restart();
    }
  }
}

bool sendHTTPPost(String payload) {
  WiFiClient client;
  HTTPClient http;

  String url = "http://" + String(masterIP) + ":" + String(masterPort) + apiEndpoint;
  Serial.println("POST URL: " + url);

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("User-Agent", "ESP8266-" + NODE_ID);
  http.setTimeout(10000); // 10 second timeout
  http.setReuse(true); // Allow connection reuse

  int httpResponseCode = http.POST(payload);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("HTTP Response: %d\n", httpResponseCode);
    if (response.length() > 0) {
      Serial.println("Response: " + response);
    }

    http.end();
    return (httpResponseCode == 200);
  } else {
    Serial.printf("HTTP Request failed: %s\n", http.errorToString(httpResponseCode).c_str());
    http.end();
    return false;
  }
}

void updateStatusLED() {
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  unsigned long currentTime = millis();

  if (WiFi.status() != WL_CONNECTED) {
    // Fast blink - WiFi disconnected (2Hz)
    if (currentTime - lastBlink >= 250) {
      ledState = !ledState;
      digitalWrite(STATUS_LED, ledState ? LOW : HIGH); // Inverted logic
      lastBlink = currentTime;
    }
  } else if (!lastSendSuccess) {
    // Medium blink - Send failed (1Hz)
    if (currentTime - lastBlink >= 500) {
      ledState = !ledState;
      digitalWrite(STATUS_LED, ledState ? LOW : HIGH); // Inverted logic
      lastBlink = currentTime;
    }
  } else if (currentTime - lastSendTime < 2000) {
    // Quick blink after successful send
    digitalWrite(STATUS_LED, LOW); // LED on
    if (currentTime - lastSendTime > 1000) {
      digitalWrite(STATUS_LED, HIGH); // LED off
    }
  } else {
    // Solid off - All good (LED off with inverted logic)
    digitalWrite(STATUS_LED, HIGH);
  }
}

// Optional: Add deep sleep functionality for battery-powered nodes
void enterDeepSleep() {
  /*
  // Uncomment for deep sleep functionality
  Serial.println("Entering deep sleep for " + String(SEND_INTERVAL/1000) + " seconds");
  ESP.deepSleep(SEND_INTERVAL * 1000); // Convert to microseconds
  */
}

// Optional: Web server for local diagnostics
void startDiagnosticServer() {
  /*
  // This function can be called from setup() if you want local diagnostics
  // Requires additional libraries and code
  */
}

