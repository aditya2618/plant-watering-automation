#include "CloudClient.h"
#include <WiFi.h>

CloudClient::CloudClient() : connected_(false), lastPingMs_(0), lastReconnectAttemptMs_(0), currentReconnectDelayMs_(1000) {}

void CloudClient::begin(const CloudConfig& config) {
  config_ = config;
  if (!config_.enabled || config_.host.isEmpty() || config_.deviceId.isEmpty() || config_.deviceSecret.isEmpty()) {
    Serial.println("[Cloud] Cloud integration is disabled or missing credentials.");
    return;
  }

  connect();
}

void CloudClient::updateConfig(const CloudConfig& config) {
  config_ = config;
  wsClient_.disconnect();
  connected_ = false;
  if (config_.enabled) {
    connect();
  }
}

void CloudClient::connect() {
  if (config_.host.isEmpty() || config_.deviceId.isEmpty()) return;

  // Port 80 is strictly plain HTTP/WS; port 443 is HTTPS/WSS
  if (config_.port == 80) {
    config_.useSsl = false;
  }

  String url = "/ws/iot/?device_id=" + config_.deviceId + "&secret=" + config_.deviceSecret;
  Serial.printf("[Cloud] Connecting to %s:%u (SSL: %s) -> %s\n",
                config_.host.c_str(), config_.port,
                config_.useSsl ? "YES" : "NO", url.c_str());

  wsClient_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    this->onWebSocketEvent(type, payload, length);
  });

  if (config_.useSsl) {
    wsClient_.beginSSL(config_.host.c_str(), config_.port, url.c_str());
  } else {
    wsClient_.begin(config_.host.c_str(), config_.port, url.c_str());
  }

  wsClient_.setReconnectInterval(5000);
}

void CloudClient::loop() {
  if (!config_.enabled) return;
  if (WiFi.status() != WL_CONNECTED) {
    connected_ = false;
    return;
  }

  wsClient_.loop();

  // Heartbeat ping every 30s
  if (connected_ && (millis() - lastPingMs_ >= 30000)) {
    sendPing();
    lastPingMs_ = millis();
  }
}

bool CloudClient::isConnected() const {
  return connected_;
}

void CloudClient::sendPing() {
  JsonDocument doc;
  doc["type"] = "ping";
  doc["timestamp"] = millis();

  String jsonStr;
  serializeJson(doc, jsonStr);
  wsClient_.sendTXT(jsonStr);
}

bool CloudClient::publishTelemetry(const JsonDocument& doc) {
  if (!connected_) return false;

  JsonDocument wrapper;
  wrapper["type"] = "telemetry";
  wrapper["payload"] = doc;

  String jsonStr;
  serializeJson(wrapper, jsonStr);
  return wsClient_.sendTXT(jsonStr);
}

bool CloudClient::publishTelemetryJson(const String& jsonStr) {
  if (!connected_) return false;

  String msg = "{\"type\":\"telemetry\",\"payload\":" + jsonStr + "}";
  return wsClient_.sendTXT(msg);
}

bool CloudClient::sendAck(const String& commandId, const String& status, const JsonDocument& resultDoc) {
  if (!connected_) return false;

  JsonDocument doc;
  doc["type"] = "ack";
  doc["command_id"] = commandId;
  doc["status"] = status;
  doc["payload"] = resultDoc;

  String jsonStr;
  serializeJson(doc, jsonStr);
  return wsClient_.sendTXT(jsonStr);
}

bool CloudClient::sendAck(const String& commandId, const String& status, const String& message) {
  JsonDocument doc;
  doc["message"] = message;
  return sendAck(commandId, status, doc);
}

void CloudClient::onCommand(CommandHandler handler) {
  commandHandler_ = handler;
}

void CloudClient::onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      connected_ = true;
      Serial.printf("[Cloud] Connected to Eyum Cloud: %s\n", (char*)payload);
      break;

    case WStype_DISCONNECTED:
      connected_ = false;
      Serial.println("[Cloud] Disconnected from Eyum Cloud.");
      break;

    case WStype_TEXT: {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, payload, length);
      if (err) {
        Serial.printf("[Cloud] JSON parse error: %s\n", err.c_str());
        return;
      }

      const char* msgType = doc["type"] | "";

      if (strcmp(msgType, "connected") == 0) {
        Serial.printf("[Cloud] Cloud server confirmed device session: %s\n", doc["device_id"].as<const char*>());
      } else if (strcmp(msgType, "pong") == 0) {
        // Ping acknowledged
      } else if (strcmp(msgType, "command") == 0) {
        String cmdId = doc["command_id"] | "";
        String action = doc["action"] | "";
        JsonObjectConst params = doc["params"].as<JsonObjectConst>();

        Serial.printf("[Cloud] Inbound Command received: action='%s', id='%s'\n", action.c_str(), cmdId.c_str());

        if (commandHandler_) {
          commandHandler_(cmdId, action, params);
        } else {
          sendAck(cmdId, "failed", "No command handler registered on device");
        }
      }
      break;
    }

    case WStype_ERROR:
      Serial.printf("[Cloud] WebSocket error occurred: %s\n", (char*)payload);
      break;

    default:
      break;
  }
}
