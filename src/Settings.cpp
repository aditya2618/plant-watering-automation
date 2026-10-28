#include "Settings.h"

#include <Preferences.h>

namespace {
constexpr char NAMESPACE[] = "eyum";
constexpr uint32_t MIN_RUNTIME_MS = 250;
constexpr uint32_t MAX_RUNTIME_MS = 5000;
constexpr uint32_t MIN_COOLDOWN_MS = 30000;
constexpr uint32_t MAX_COOLDOWN_MS = 86400000;
constexpr uint32_t MAX_DAILY_MS = 300000;
}  // namespace

bool SettingsStore::begin() {
  Preferences preferences;
  if (!preferences.begin(NAMESPACE, true)) return false;
  if (preferences.isKey("initialized")) {
    settings_.dryRaw = preferences.getInt("dry", settings_.dryRaw);
    settings_.wetRaw = preferences.getInt("wet", settings_.wetRaw);
    settings_.threshold = preferences.getUChar("threshold", settings_.threshold);
    settings_.pumpRuntimeMs = preferences.getUInt("runtime", settings_.pumpRuntimeMs);
    settings_.cooldownMs = preferences.getUInt("cooldown", settings_.cooldownMs);
    settings_.soakMs = preferences.getUInt("soak", settings_.soakMs);
    settings_.maxDailyPumpMs = preferences.getUInt("daily", settings_.maxDailyPumpMs);
    settings_.autoMode = preferences.getBool("auto", settings_.autoMode);
    network_.wifiSsid = preferences.getString("ssid", "");
    network_.wifiPassword = preferences.getString("wifiPass", "");
    network_.dashboardPassword = preferences.getString("dashPass", "");
    cloud_.enabled = preferences.getBool("cloudEn", false);
    cloud_.host = preferences.getString("cloudHost", "");
    cloud_.port = preferences.getUShort("cloudPort", 80);
    cloud_.useSsl = preferences.getBool("cloudSsl", false);
    cloud_.deviceId = preferences.getString("cloudDevId", "");
    cloud_.deviceSecret = preferences.getString("cloudSec", "");
    cloud_.telemetryIntervalMs = preferences.getUInt("cloudInt", 30000);
  }
  preferences.end();

  String error;
  if (!valid(settings_, error)) {
    settings_ = PlantSettings{};
    return save();
  }
  return true;
}

const PlantSettings& SettingsStore::get() const { return settings_; }
const NetworkSettings& SettingsStore::network() const { return network_; }
const CloudConfig& SettingsStore::cloud() const { return cloud_; }
bool SettingsStore::isProvisioned() const {
  return !network_.wifiSsid.isEmpty() && network_.dashboardPassword.length() >= 10;
}

bool SettingsStore::update(const PlantSettings& settings, String& error) {
  if (!valid(settings, error)) return false;
  settings_ = settings;
  if (!save()) {
    error = "Could not save settings";
    return false;
  }
  return true;
}

bool SettingsStore::updateCloud(const CloudConfig& cloud, String& error) {
  if (cloud.enabled && (cloud.host.isEmpty() || cloud.deviceId.isEmpty() || cloud.deviceSecret.isEmpty())) {
    error = "Cloud host, device ID, and device secret are required when cloud is enabled";
    return false;
  }
  cloud_ = cloud;
  if (!save()) {
    error = "Could not save cloud configuration";
    return false;
  }
  return true;
}

bool SettingsStore::reset(String& error) { return update(PlantSettings{}, error); }

bool SettingsStore::provision(const NetworkSettings& network, String& error) {
  if (network.wifiSsid.isEmpty() || network.wifiSsid.length() > 32) {
    error = "Wi-Fi name must be 1 to 32 characters";
  } else if (network.wifiPassword.length() > 63) {
    error = "Wi-Fi password must be 63 characters or fewer";
  } else if (network.dashboardPassword.length() < 10 || network.dashboardPassword.length() > 63) {
    error = "Dashboard password must be 10 to 63 characters";
  } else if (network.wifiSsid.indexOf('"') >= 0 || network.wifiSsid.indexOf('\\') >= 0 ||
             network.wifiPassword.indexOf('"') >= 0 || network.wifiPassword.indexOf('\\') >= 0 ||
             network.dashboardPassword.indexOf('"') >= 0 || network.dashboardPassword.indexOf('\\') >= 0) {
    error = "Quotes and backslashes are not supported in credentials";
  } else {
    network_ = network;
    if (!save()) { error = "Could not save provisioning"; return false; }
    return true;
  }
  return false;
}

bool SettingsStore::save() {
  Preferences preferences;
  if (!preferences.begin(NAMESPACE, false)) return false;
  preferences.putInt("dry", settings_.dryRaw);
  preferences.putInt("wet", settings_.wetRaw);
  preferences.putUChar("threshold", settings_.threshold);
  preferences.putUInt("runtime", settings_.pumpRuntimeMs);
  preferences.putUInt("cooldown", settings_.cooldownMs);
  preferences.putUInt("soak", settings_.soakMs);
  preferences.putUInt("daily", settings_.maxDailyPumpMs);
  preferences.putBool("auto", settings_.autoMode);
  preferences.putString("ssid", network_.wifiSsid);
  preferences.putString("wifiPass", network_.wifiPassword);
  preferences.putString("dashPass", network_.dashboardPassword);
  preferences.putBool("cloudEn", cloud_.enabled);
  preferences.putString("cloudHost", cloud_.host);
  preferences.putUShort("cloudPort", cloud_.port);
  preferences.putBool("cloudSsl", cloud_.useSsl);
  preferences.putString("cloudDevId", cloud_.deviceId);
  preferences.putString("cloudSec", cloud_.deviceSecret);
  preferences.putUInt("cloudInt", cloud_.telemetryIntervalMs);
  preferences.putBool("initialized", true);
  preferences.end();
  return true;
}

bool SettingsStore::valid(const PlantSettings& settings, String& error) const {
  if (settings.dryRaw < 0 || settings.dryRaw > 4095 || settings.wetRaw < 0 ||
      settings.wetRaw > 4095 || settings.dryRaw == settings.wetRaw) {
    error = "Calibration values must be different ADC readings";
  } else if (settings.threshold < 5 || settings.threshold > 80) {
    error = "Threshold must be between 5 and 80";
  } else if (settings.pumpRuntimeMs < MIN_RUNTIME_MS || settings.pumpRuntimeMs > MAX_RUNTIME_MS) {
    error = "Pump runtime must be 250 to 5000 ms";
  } else if (settings.cooldownMs < MIN_COOLDOWN_MS || settings.cooldownMs > MAX_COOLDOWN_MS) {
    error = "Cooldown must be 30 seconds to 24 hours";
  } else if (settings.soakMs > settings.cooldownMs) {
    error = "Soak delay cannot exceed cooldown";
  } else if (settings.maxDailyPumpMs < settings.pumpRuntimeMs || settings.maxDailyPumpMs > MAX_DAILY_MS) {
    error = "Daily limit must cover one cycle and be at most 5 minutes";
  } else {
    return true;
  }
  return false;
}

