#pragma once

#include <Arduino.h>

struct PlantSettings {
  int dryRaw = 3000;
  int wetRaw = 1500;
  uint8_t threshold = 30;
  uint32_t pumpRuntimeMs = 3000;
  uint32_t cooldownMs = 300000;
  uint32_t soakMs = 30000;
  uint32_t maxDailyPumpMs = 30000;
  bool autoMode = false;
};

#include "CloudClient.h"

struct NetworkSettings {
  String wifiSsid;
  String wifiPassword;
  String dashboardPassword;
};

class SettingsStore {
 public:
  bool begin();
  const PlantSettings& get() const;
  bool update(const PlantSettings& settings, String& error);
  const NetworkSettings& network() const;
  const CloudConfig& cloud() const;
  bool isProvisioned() const;
  bool provision(const NetworkSettings& network, String& error);
  bool updateCloud(const CloudConfig& cloud, String& error);
  bool reset(String& error);

 private:
  PlantSettings settings_;
  NetworkSettings network_;
  CloudConfig cloud_;
  bool save();
  bool valid(const PlantSettings& settings, String& error) const;
};

