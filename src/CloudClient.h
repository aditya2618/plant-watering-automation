#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <functional>

struct CloudConfig {
  bool enabled = false;
  String host = "";         // e.g. "80.225.212.191" or "cloud.eyum.com"
  uint16_t port = 80;       // 80 for WS, 443 for WSS
  bool useSsl = false;
  String deviceId = "";     // "esp32-plant-care-01"
  String deviceSecret = ""; // Secret token from cloud registration
  uint32_t telemetryIntervalMs = 30000; // Default 30s
};

class CloudClient {
 public:
  using CommandHandler = std::function<void(const String& commandId, const String& action, JsonObjectConst params)>;

  CloudClient();
  void begin(const CloudConfig& config);
  void loop();
  bool isConnected() const;

  // Telemetry dispatch
  bool publishTelemetry(const JsonDocument& doc);
  bool publishTelemetryJson(const String& jsonStr);

  // Command ACK dispatch
  bool sendAck(const String& commandId, const String& status, const JsonDocument& resultDoc);
  bool sendAck(const String& commandId, const String& status, const String& message);

  // Register command callback
  void onCommand(CommandHandler handler);

  void updateConfig(const CloudConfig& config);
  const CloudConfig& config() const { return config_; }

 private:
  CloudConfig config_;
  WebSocketsClient wsClient_;
  bool connected_ = false;
  uint32_t lastPingMs_ = 0;
  uint32_t lastReconnectAttemptMs_ = 0;
  uint32_t currentReconnectDelayMs_ = 1000;
  CommandHandler commandHandler_;

  void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length);
  void connect();
  void sendPing();
};
