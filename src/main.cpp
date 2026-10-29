#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ESPmDNS.h>

#include "MoistureSensor.h"
#include "Settings.h"
#include "WateringController.h"
#include "CloudClient.h"

namespace {
constexpr uint8_t SOIL_PIN = 35;       // Sensor AO (Analog Output pin)
constexpr int8_t SOIL_DO_PIN = 32;     // Sensor DO (Digital Output pin; set to -1 if left disconnected)
constexpr uint8_t PUMP_PIN = 26;       // MOSFET driver gate / Relay IN
constexpr int8_t RESERVOIR_PIN = -1;   // Optional float switch (pin 34 or -1)
constexpr uint32_t SAMPLE_INTERVAL_MS = 2000;
constexpr char DASHBOARD_USER[] = "admin";

WebServer server(80);
SettingsStore settingsStore;
MoistureSensor sensor(SOIL_PIN, SOIL_DO_PIN);
WateringController watering(PUMP_PIN);
CloudClient cloudClient;
SensorReading reading;
uint32_t lastSampleAt = 0;
uint32_t lastTelemetryAt = 0;
bool setupMode = true;

String setupNetworkName() {
  const uint64_t chipId = ESP.getEfuseMac();
  char name[25];
  snprintf(name, sizeof(name), "Eyum-Setup-%04X", static_cast<uint16_t>(chipId));
  return String(name);
}

String setupNetworkPassword() {
  return "12345678";
}

void startSetupAccessPoint() {
  setupMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(setupNetworkName().c_str(), setupNetworkPassword().c_str());
  Serial.printf("Setup Wi-Fi: %s\nSetup password: %s\n", setupNetworkName().c_str(), setupNetworkPassword().c_str());
  Serial.printf("Provisioning page: http://%s\n", WiFi.softAPIP().toString().c_str());
}

void connectProvisionedNetwork() {
  const NetworkSettings& network = settingsStore.network();
  setupMode = false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(network.wifiSsid.c_str(), network.wifiPassword.c_str());
  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000) delay(250);
  if (WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin("eyum-plant")) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("mDNS responder started: http://eyum-plant.local");
    }
    Serial.printf("Dashboard: http://%s\n", WiFi.localIP().toString().c_str());
    if (settingsStore.cloud().enabled) {
      cloudClient.begin(settingsStore.cloud());
    }
    return;
  }
  Serial.println("Saved Wi-Fi unavailable; starting setup access point.");
  startSetupAccessPoint();
}

bool isReservoirOk();

void sendCloudTelemetry() {
  if (!cloudClient.isConnected()) return;
  JsonDocument doc;
  doc["valid"] = reading.valid;
  doc["moisture"] = reading.moisture;
  doc["raw"] = reading.raw;
  doc["pump"] = watering.isRunning();
  doc["auto"] = settingsStore.get().autoMode;
  doc["reservoirOk"] = isReservoirOk();
  doc["rssi"] = WiFi.RSSI();
  doc["uptimeSec"] = millis() / 1000;
  doc["fw"] = "1.3.0";
  cloudClient.publishTelemetry(doc);
}

void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
}

void sendJson(int code, const String& body) {
  addCorsHeaders();
  server.send(code, "application/json", body);
}

void sendError(int code, const String& message) {
  sendJson(code, "{\"error\":\"" + message + "\"}");
}

bool requireAuth() {
  if (server.authenticate(DASHBOARD_USER, settingsStore.network().dashboardPassword.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "Eyum Plant Care");
  return false;
}

bool jsonBool(const String& body, const char* key, bool& value) {
  const String prefix = String("\"") + key + "\":";
  const int start = body.indexOf(prefix);
  if (start < 0) return false;
  const String candidate = body.substring(start + prefix.length());
  if (candidate.startsWith("true")) { value = true; return true; }
  if (candidate.startsWith("false")) { value = false; return true; }
  return false;
}

bool jsonInt(const String& body, const char* key, int& value) {
  const String prefix = String("\"") + key + "\":";
  const int start = body.indexOf(prefix);
  if (start < 0) return false;
  const int cursor = start + prefix.length();
  if (cursor >= static_cast<int>(body.length()) || (!isDigit(body[cursor]) && body[cursor] != '-')) return false;
  value = body.substring(cursor).toInt();
  return true;
}

bool jsonString(const String& body, const char* key, String& value) {
  const String prefix = String("\"") + key + "\":\"";
  const int start = body.indexOf(prefix);
  if (start < 0) return false;
  const int valueStart = start + prefix.length();
  const int valueEnd = body.indexOf('"', valueStart);
  if (valueEnd < 0) return false;
  value = body.substring(valueStart, valueEnd);
  return true;
}

String stopReason() {
  switch (watering.lastStopReason()) {
    case PumpStopReason::Completed: return "completed";
    case PumpStopReason::Manual: return "manual";
    case PumpStopReason::Safety: return "safety";
    case PumpStopReason::DailyLimit: return "daily_limit";
    default: return "none";
  }
}

bool isReservoirOk() {
  if (RESERVOIR_PIN < 0) return true;
  return digitalRead(RESERVOIR_PIN) == LOW;
}

void serveFile(const char* path, const char* type) {
  if (!LittleFS.exists(path)) { sendError(500, "Dashboard files unavailable"); return; }
  File file = LittleFS.open(path, "r");
  server.streamFile(file, type);
  file.close();
}

void setupRoutes() {
  server.on("/", HTTP_GET, [] {
    if (setupMode) serveFile("/setup.html", "text/html");
    else if (requireAuth()) serveFile("/index.html", "text/html");
  });
  server.on("/style.css", HTTP_GET, [] { serveFile("/style.css", "text/css"); });
  server.on("/app.js", HTTP_GET, [] { if (!setupMode && requireAuth()) serveFile("/app.js", "application/javascript"); });
  server.on("/setup.js", HTTP_GET, [] { if (setupMode) serveFile("/setup.js", "application/javascript"); });

  // CORS Preflight OPTIONS routes
  server.on("/api/provision", HTTP_OPTIONS, [] { addCorsHeaders(); server.send(204); });
  server.on("/api/status", HTTP_OPTIONS, [] { addCorsHeaders(); server.send(204); });
  server.on("/api/settings", HTTP_OPTIONS, [] { addCorsHeaders(); server.send(204); });
  server.on("/api/water", HTTP_OPTIONS, [] { addCorsHeaders(); server.send(204); });
  server.on("/api/stop", HTTP_OPTIONS, [] { addCorsHeaders(); server.send(204); });
  server.on("/api/auto", HTTP_OPTIONS, [] { addCorsHeaders(); server.send(204); });
  server.on("/api/calibration", HTTP_OPTIONS, [] { addCorsHeaders(); server.send(204); });

  server.on("/api/provision", HTTP_POST, [] {
    if (!setupMode && !requireAuth()) return;
    NetworkSettings network;
    const String body = server.arg("plain");
    if (!jsonString(body, "wifiSsid", network.wifiSsid) || !jsonString(body, "wifiPassword", network.wifiPassword) ||
        !jsonString(body, "dashboardPassword", network.dashboardPassword)) {
      sendError(400, "Missing provisioning fields"); return;
    }
    String error;
    if (!settingsStore.provision(network, error)) { sendError(400, error); return; }
    sendJson(200, "{\"ok\":true,\"message\":\"Saved. Device is restarting.\"}");
    delay(400);
    ESP.restart();
  });

  server.on("/api/status", HTTP_GET, [] {
    if (!requireAuth()) return;
    const PlantSettings& settings = settingsStore.get();
    String json = "{\"valid\":" + String(reading.valid ? "true" : "false");
    json += ",\"raw\":" + String(reading.raw);
    json += ",\"moisture\":" + String(reading.moisture);
    json += ",\"fault\":\"" + reading.fault + "\"";
    json += ",\"digitalTrigger\":" + String(reading.digitalTrigger);
    json += ",\"pump\":" + String(watering.isRunning() ? "true" : "false");
    json += ",\"remainingMs\":" + String(watering.remainingMs(settings));
    json += ",\"auto\":" + String(settings.autoMode ? "true" : "false");
    json += ",\"lastStop\":\"" + stopReason() + "\"";
    json += ",\"reservoirOk\":" + String(isReservoirOk() ? "true" : "false");
    json += ",\"uptimeSec\":" + String(millis() / 1000);
    json += ",\"rssi\":" + String(WiFi.RSSI());
    json += ",\"ip\":\"" + (setupMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\"";
    json += ",\"mdns\":\"eyum-plant.local\"}";
    sendJson(200, json);
  });

  server.on("/api/settings", HTTP_GET, [] {
    if (!requireAuth()) return;
    const PlantSettings& s = settingsStore.get();
    sendJson(200, "{\"dryRaw\":" + String(s.dryRaw) + ",\"wetRaw\":" + String(s.wetRaw) +
                  ",\"threshold\":" + String(s.threshold) + ",\"pumpRuntimeMs\":" + String(s.pumpRuntimeMs) +
                  ",\"cooldownMs\":" + String(s.cooldownMs) + ",\"soakMs\":" + String(s.soakMs) +
                  ",\"maxDailyPumpMs\":" + String(s.maxDailyPumpMs) + ",\"dailyUsedMs\":" + String(watering.dailyUsedMs()) + "}");
  });

  server.on("/api/settings", HTTP_POST, [] {
    if (!requireAuth()) return;
    PlantSettings updated = settingsStore.get(); int value; const String body = server.arg("plain");
    if (jsonInt(body, "threshold", value)) updated.threshold = value;
    if (jsonInt(body, "pumpRuntimeMs", value)) updated.pumpRuntimeMs = value;
    if (jsonInt(body, "cooldownMs", value)) updated.cooldownMs = value;
    if (jsonInt(body, "soakMs", value)) updated.soakMs = value;
    if (jsonInt(body, "maxDailyPumpMs", value)) updated.maxDailyPumpMs = value;
    String error;
    if (!settingsStore.update(updated, error)) { sendError(400, error); return; }
    sendJson(200, "{\"ok\":true}");
  });

  server.on("/api/water", HTTP_POST, [] {
    if (!requireAuth()) return;
    if (!isReservoirOk()) { sendError(400, "Water reservoir is empty"); return; }
    String error;
    if (!watering.requestWater(settingsStore.get(), reading.valid, error, true)) { sendError(409, error); return; }
    sendJson(200, "{\"ok\":true}");
  });

  server.on("/api/stop", HTTP_POST, [] { if (requireAuth()) { watering.stop(); sendJson(200, "{\"ok\":true}"); } });

  server.on("/api/auto", HTTP_POST, [] {
    if (!requireAuth()) return;
    PlantSettings updated = settingsStore.get();
    if (!jsonBool(server.arg("plain"), "enabled", updated.autoMode)) { sendError(400, "Expected enabled boolean"); return; }
    String error;
    if (!settingsStore.update(updated, error)) { sendError(400, error); return; }
    sendJson(200, "{\"ok\":true}");
  });

  server.on("/api/calibration", HTTP_POST, [] {
    if (!requireAuth()) return;
    PlantSettings updated = settingsStore.get(); int value; const String body = server.arg("plain");
    if (!jsonInt(body, "dryRaw", value)) { sendError(400, "Missing dryRaw"); return; } updated.dryRaw = value;
    if (!jsonInt(body, "wetRaw", value)) { sendError(400, "Missing wetRaw"); return; } updated.wetRaw = value;
    String error;
    if (!settingsStore.update(updated, error)) { sendError(400, error); return; }
    reading = sensor.sample(updated.dryRaw, updated.wetRaw);
    sendJson(200, "{\"ok\":true}");
  });

  server.on("/api/cloud", HTTP_OPTIONS, [] { addCorsHeaders(); server.send(204); });
  server.on("/api/cloud", HTTP_GET, [] {
    if (!requireAuth()) return;
    const CloudConfig& c = settingsStore.cloud();
    String json = "{\"enabled\":" + String(c.enabled ? "true" : "false");
    json += ",\"host\":\"" + c.host + "\"";
    json += ",\"port\":" + String(c.port);
    json += ",\"useSsl\":" + String(c.useSsl ? "true" : "false");
    json += ",\"deviceId\":\"" + c.deviceId + "\"";
    json += ",\"connected\":" + String(cloudClient.isConnected() ? "true" : "false");
    json += ",\"telemetryIntervalMs\":" + String(c.telemetryIntervalMs) + "}";
    sendJson(200, json);
  });

  server.on("/api/cloud", HTTP_POST, [] {
    if (!requireAuth()) return;
    CloudConfig c = settingsStore.cloud();
    const String body = server.arg("plain");
    bool en; if (jsonBool(body, "enabled", en)) c.enabled = en;
    String str;
    if (jsonString(body, "host", str)) c.host = str;
    if (jsonString(body, "deviceId", str)) c.deviceId = str;
    if (jsonString(body, "deviceSecret", str)) c.deviceSecret = str;
    int val;
    if (jsonInt(body, "port", val)) c.port = static_cast<uint16_t>(val);
    if (jsonInt(body, "telemetryIntervalMs", val)) c.telemetryIntervalMs = static_cast<uint32_t>(val);
    bool ssl; if (jsonBool(body, "useSsl", ssl)) c.useSsl = ssl;

    String error;
    if (!settingsStore.updateCloud(c, error)) { sendError(400, error); return; }
    cloudClient.updateConfig(settingsStore.cloud());
    sendJson(200, "{\"ok\":true,\"message\":\"Cloud settings updated\"}");
  });

  server.onNotFound([] { if (setupMode || requireAuth()) sendError(404, "Not found"); });
}
}  // namespace

void setup() {
  watering.begin();
  Serial.begin(115200);
  settingsStore.begin();
  sensor.begin();
  if (RESERVOIR_PIN >= 0) pinMode(RESERVOIR_PIN, INPUT_PULLUP);
  LittleFS.begin(true);

  // Setup CloudClient command dispatch
  cloudClient.onCommand([](const String& cmdId, const String& action, JsonObjectConst params) {
    if (action == "water") {
      if (!isReservoirOk()) {
        cloudClient.sendAck(cmdId, "failed", "Water reservoir is empty");
        return;
      }
      String err;
      if (watering.requestWater(settingsStore.get(), reading.valid, err, true)) {
        cloudClient.sendAck(cmdId, "acknowledged", "Watering started");
        sendCloudTelemetry();
      } else {
        cloudClient.sendAck(cmdId, "failed", err);
      }
    } else if (action == "stop") {
      watering.stop();
      cloudClient.sendAck(cmdId, "acknowledged", "Watering stopped");
      sendCloudTelemetry();
    } else if (action == "auto") {
      bool enabled = params["enabled"] | false;
      PlantSettings updated = settingsStore.get();
      updated.autoMode = enabled;
      String err;
      if (settingsStore.update(updated, err)) {
        cloudClient.sendAck(cmdId, "acknowledged", enabled ? "Auto mode enabled" : "Auto mode disabled");
        sendCloudTelemetry();
      } else {
        cloudClient.sendAck(cmdId, "failed", err);
      }
    } else if (action == "set_threshold") {
      int th = params["threshold"] | -1;
      if (th >= 5 && th <= 80) {
        PlantSettings updated = settingsStore.get();
        updated.threshold = th;
        String err;
        if (settingsStore.update(updated, err)) {
          cloudClient.sendAck(cmdId, "acknowledged", "Threshold updated");
          sendCloudTelemetry();
        } else {
          cloudClient.sendAck(cmdId, "failed", err);
        }
      } else {
        cloudClient.sendAck(cmdId, "failed", "Invalid threshold (5-80)");
      }
    } else {
      cloudClient.sendAck(cmdId, "failed", "Unknown action: " + action);
    }
  });

  if (settingsStore.isProvisioned()) connectProvisionedNetwork();
  else startSetupAccessPoint();
  setupRoutes();
  server.begin();
  reading = sensor.sample(settingsStore.get().dryRaw, settingsStore.get().wetRaw);
  lastSampleAt = millis();
  lastTelemetryAt = millis();
}

void loop() {
  server.handleClient();
  cloudClient.loop();

  const uint32_t now = millis();
  if (now - lastSampleAt >= SAMPLE_INTERVAL_MS) {
    lastSampleAt = now;
    reading = sensor.sample(settingsStore.get().dryRaw, settingsStore.get().wetRaw);
  }

  // Periodic Cloud Telemetry Reporting
  if (cloudClient.isConnected() && (now - lastTelemetryAt >= settingsStore.cloud().telemetryIntervalMs)) {
    lastTelemetryAt = now;
    sendCloudTelemetry();
  }

  const PlantSettings& settings = settingsStore.get();
  watering.update(settings, reading.valid);
  if (settings.autoMode && reading.valid && isReservoirOk() && !watering.isRunning() && reading.moisture < settings.threshold) {
    String ignored;
    if (watering.requestWater(settings, true, ignored, false)) {
      sendCloudTelemetry();
    }
  }
}

