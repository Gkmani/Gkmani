#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ModbusMaster.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

/* ------------ CONFIG ------------ */
#define FLOAT_COUNT 9
#define BASE_ADDR   30000

#define EEPROM_SIZE 512
#define LABEL_SIZE  24

#define LOG_SIZE 25   // number of samples stored

ModbusMaster node;
ESP8266WebServer server(80);

/* ------------ DATA ------------ */
float values[FLOAT_COUNT];
String labels[FLOAT_COUNT];

struct LogEntry {
  uint32_t time;
  float v[FLOAT_COUNT];
};

LogEntry logs[LOG_SIZE];
uint8_t logIndex = 0;
uint32_t secondsCounter = 0;

/* ================= EEPROM ================= */

void loadLabels() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < FLOAT_COUNT; i++) {
    char buf[LABEL_SIZE];
    EEPROM.get(i * LABEL_SIZE, buf);
    buf[LABEL_SIZE - 1] = '\0';
    labels[i] = String(buf);
    if (labels[i].length() == 0)
      labels[i] = "Register " + String(BASE_ADDR + i * 2);
  }
}

void saveLabel(uint8_t index, String name) {
  char buf[LABEL_SIZE] = {0};
  name.substring(0, LABEL_SIZE - 1).toCharArray(buf, LABEL_SIZE);
  EEPROM.put(index * LABEL_SIZE, buf);
  EEPROM.commit();
}

/* ================= MODBUS ================= */

void readModbus() {
  if (node.readInputRegisters(0, 18) == node.ku8MBSuccess) {
    for (int i = 0; i < FLOAT_COUNT; i++) {
      uint32_t raw =
        ((uint32_t)node.getResponseBuffer(i * 2) << 16) |
        node.getResponseBuffer(i * 2 + 1);
      memcpy(&values[i], &raw, sizeof(float));
    }
  }
}

/* ================= LOGGING ================= */

void logData() {
  logs[logIndex].time = secondsCounter;
  for (int i = 0; i < FLOAT_COUNT; i++)
    logs[logIndex].v[i] = values[i];

  logIndex = (logIndex + 1) % LOG_SIZE;
}

/* ================= API ================= */

void apiData() {
  StaticJsonDocument<256> doc;
  for (int i = 0; i < FLOAT_COUNT; i++)
    doc[String(BASE_ADDR + i * 2)] = values[i];

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void apiLabels() {
  StaticJsonDocument<256> doc;
  for (int i = 0; i < FLOAT_COUNT; i++)
    doc[String(BASE_ADDR + i * 2)] = labels[i];

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void apiSetLabel() {
  StaticJsonDocument<128> doc;
  deserializeJson(doc, server.arg("plain"));
  uint8_t index = doc["index"];
  labels[index] = doc["name"].as<String>();
  saveLabel(index, labels[index]);
  server.send(200, "application/json", "{\"ok\":true}");
}

void apiDownloadLog() {
  String csv = "Time";
  for (int i = 0; i < FLOAT_COUNT; i++)
    csv += "," + labels[i];
  csv += "\n";

  for (int i = 0; i < LOG_SIZE; i++) {
    csv += String(logs[i].time);
    for (int j = 0; j < FLOAT_COUNT; j++)
      csv += "," + String(logs[i].v[j], 4);
    csv += "\n";
  }

  server.send(200, "text/csv", csv);
}

void apiClearLog() {
  memset(logs, 0, sizeof(logs));
  logIndex = 0;
  server.send(200, "application/json", "{\"cleared\":true}");
}

/* ================= UI ================= */

const char UI_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SysCon Modbus</title>
<style>
body{font-family:Arial;background:#0f172a;color:#e5e7eb;margin:0}
header{background:#020617;padding:14px;text-align:center;font-size:20px}
.card{background:#020617;margin:10px;padding:14px;border-radius:10px}
.value{font-size:26px}
button{padding:10px;margin:5px;width:45%}
input{width:100%;padding:6px;margin-top:6px}
</style>
</head>
<body>
<header>SysCon – Modbus AP</header>
<div id="cards"></div>

<div style="text-align:center">
<button onclick="download()">Download CSV</button>
<button onclick="clearLog()">Clear Log</button>
</div>

<script>
async function load(){
  const d = await fetch('/api/data').then(r=>r.json());
  const l = await fetch('/api/labels').then(r=>r.json());
  let i=0,h='';
  for(let a in d){
    h+=`<div class="card">
      <b>${l[a]}</b>
      <div class="value">${Number(d[a]).toFixed(3)}</div>
      <input value="${l[a]}" onchange="setLabel(${i},this.value)">
    </div>`;
    i++;
  }
  document.getElementById('cards').innerHTML=h;
}

function setLabel(i,n){
  fetch('/api/label',{method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({index:i,name:n})});
}

function download(){
  window.location='/api/log';
}

function clearLog(){
  fetch('/api/clearlog');
}

setInterval(load,2000);
load();
</script>
</body>
</html>
)rawliteral";

void serveUI() {
  server.send_P(200, "text/html", UI_PAGE);
}

/* ================= SETUP ================= */

void setup() {
  loadLabels();

  WiFi.mode(WIFI_AP);
  WiFi.softAP("SysCon", "12345678");

  Serial.begin(9600);        // Modbus UART
  node.begin(1, Serial);

  server.on("/", serveUI);
  server.on("/api/data", apiData);
  server.on("/api/labels", apiLabels);
  server.on("/api/label", HTTP_POST, apiSetLabel);
  server.on("/api/log", apiDownloadLog);
  server.on("/api/clearlog", apiClearLog);

  server.begin();
}

/* ================= LOOP ================= */

void loop() {
  readModbus();
  logData();
  server.handleClient();
  delay(1000);
  secondsCounter++;
}

