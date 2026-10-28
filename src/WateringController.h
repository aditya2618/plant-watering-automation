#pragma once

#include <Arduino.h>
#include "Settings.h"

enum class PumpStopReason { None, Completed, Manual, Safety, DailyLimit };

class WateringController {
 public:
  explicit WateringController(uint8_t pin);
  void begin();
  bool requestWater(const PlantSettings& settings, bool sensorValid, String& error, bool isManual = false);
  void stop(PumpStopReason reason = PumpStopReason::Manual);
  void update(const PlantSettings& settings, bool sensorValid);
  bool isRunning() const;
  uint32_t remainingMs(const PlantSettings& settings) const;
  uint32_t dailyUsedMs() const;
  PumpStopReason lastStopReason() const;
  bool canWater(const PlantSettings& settings, bool sensorValid, String& error, bool isManual = false) const;

 private:
  uint8_t pin_;
  bool running_ = false;
  uint32_t startedAt_ = 0;
  uint32_t stoppedAt_ = 0;
  uint32_t dayStartedAt_ = 0;
  uint32_t dailyUsedMs_ = 0;
  PumpStopReason lastStopReason_ = PumpStopReason::None;
  void resetDailyLimitIfNeeded();
};
