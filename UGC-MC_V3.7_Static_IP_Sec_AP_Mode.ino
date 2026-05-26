0

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




