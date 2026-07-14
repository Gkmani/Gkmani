#include "WebServerModule.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include "SDCardManager.h"
#include "LogicEngine.h"

using namespace WebServerModule;

static const char* ssid = "YOUR_SSID";
static const char* password = "YOUR_PASS";
static AsyncWebServer server(80);

// very small user database (in firmware) - replace with secure mechanism
static const String adminUser = "admin";
static const String adminPass = "plc123"; // change in production

void WebServerModule::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 40) {
    delay(250);
    Serial.print(".");
    timeout++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi not connected.");
  }

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  }

  // Serve static files from SPIFFS /data
  server.serveStatic("/", SPIFFS, "/data/").setDefaultFile("index.html");

  // status endpoint
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req){
    String json = "{";
    json += "\"di1\":" + String(digitalRead(IOManager::DI1)) + ",";
    json += "\"di2\":" + String(digitalRead(IOManager::DI2)) + ",";
    json += "\"adc\":" + String(IOManager::readAnalog(IOManager::ADC1));
    json += "}";
    req->send(200, "application/json", json);
  });

  // upload endpoint (multipart form) - saves to SD card path /scripts/<name>
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *req){
    // Empty handler for finalization
    req->send(200); 
  }, [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final){
    // This callback is called multiple times for each uploaded file chunk
    static String target;
    if(index==0) {
      // first chunk - determine target path
      target = "/scripts/";
      target += filename;
      Serial.printf("Upload start: %s -> %s\n", filename.c_str(), target.c_str());
      // ensure folder exists
      if (!SD.exists("/scripts")) SD.mkdir("/scripts");
      // remove existing file
      if (SD.exists(target.c_str())) SD.remove(target.c_str());
    }
    // append chunk to file
    File f = SD.open(target.c_str(), FILE_APPEND);
    if (!f) {
      Serial.println("Failed to open target file for writing");
      return;
    }
    f.write(data, len);
    f.close();
    if (final) {
      Serial.printf("Upload finished: %s (%u bytes)\n", target.c_str(), (unsigned)index+len);
    }
  });

  // load script selection endpoint
  server.on("/loadscript", HTTP_POST, [](AsyncWebServerRequest *req){
    if (!req->hasParam("path", true) || !req->hasParam("type", true)) {
      req->send(400, "text/plain", "Missing params");
      return;
    }
    String path = req->getParam("path", true)->value();
    String type = req->getParam("type", true)->value();
    int t = type.toInt();
    bool ok = LogicEngine::loadScript(path.c_str(), (LogicEngine::ScriptType)t);
    req->send(ok?200:500, "text/plain", ok?"Loaded":"Failed");
  });

  server.on("/listscripts", HTTP_GET, [](AsyncWebServerRequest *req){
    // list files in /scripts
    String json = "[";
    File root = SD.open("/scripts");
    if (!root) { req->send(200, "application/json", "[]"); return; }
    File file = root.openNextFile();
    bool first=true;
    while(file) {
      if (!first) json += ","; first=false;
      json += "\"" + String("/scripts/") + String(file.name()) + "\"";
      file = root.openNextFile();
    }
    json += "]";
    req->send(200, "application/json", json);
  });

  server.begin();
  Serial.println("Web server started");
}

void WebServerModule::update() {
  // placeholder for things like session cleanup
}

bool WebServerModule::authenticate(const String& user, const String& pass) {
  return user == adminUser && pass == adminPass;
}
