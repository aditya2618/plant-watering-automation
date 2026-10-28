#include "WateringController.h"

namespace {
constexpr uint32_t DAY_MS = 86400000;
}

WateringController::WateringController(uint8_t pin) : pin_(pin) {}

void WateringController::begin() {
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
  dayStartedAt_ = millis();
}

bool WateringController::canWater(const PlantSettings& settings, bool sensorValid, String& error, bool isManual) const {
  if (running_) {
    error = "Pump already running";
    return false;
  }
  if (dailyUsedMs_ + settings.pumpRuntimeMs > settings.maxDailyPumpMs) {
    error = "Daily watering limit reached";
    return false;
  }
  // Cooldown and sensor validity only restrict automatic mode, never manual user trigger
  if (!isManual) {
    if (!sensorValid) {
      error = "Sensor unavailable";
      return false;
    }
    if (stoppedAt_ != 0 && millis() - stoppedAt_ < settings.cooldownMs) {
      error = "Watering cooldown active";
      return false;
    }
  }
  return true;
}

bool WateringController::requestWater(const PlantSettings& settings, bool sensorValid, String& error, bool isManual) {
  resetDailyLimitIfNeeded();
  if (!canWater(settings, sensorValid, error, isManual)) return false;
  digitalWrite(pin_, HIGH);
  running_ = true;
  startedAt_ = millis();
  lastStopReason_ = PumpStopReason::None;
  return true;
}

void WateringController::stop(PumpStopReason reason) {
  if (running_) {
    const uint32_t elapsed = millis() - startedAt_;
    dailyUsedMs_ += elapsed;
    stoppedAt_ = millis();
  }
  digitalWrite(pin_, LOW);
  running_ = false;
  lastStopReason_ = reason;
}

void WateringController::update(const PlantSettings& settings, bool sensorValid) {
  resetDailyLimitIfNeeded();
  if (running_ && !sensorValid) stop(PumpStopReason::Safety);
  else if (running_ && millis() - startedAt_ >= settings.pumpRuntimeMs) stop(PumpStopReason::Completed);
}

bool WateringController::isRunning() const { return running_; }

uint32_t WateringController::remainingMs(const PlantSettings& settings) const {
  if (!running_) return 0;
  const uint32_t elapsed = millis() - startedAt_;
  return elapsed >= settings.pumpRuntimeMs ? 0 : settings.pumpRuntimeMs - elapsed;
}

uint32_t WateringController::dailyUsedMs() const { return dailyUsedMs_; }
PumpStopReason WateringController::lastStopReason() const { return lastStopReason_; }

void WateringController::resetDailyLimitIfNeeded() {
  if (millis() - dayStartedAt_ >= DAY_MS) {
    dayStartedAt_ = millis();
    dailyUsedMs_ = 0;
  }
}
