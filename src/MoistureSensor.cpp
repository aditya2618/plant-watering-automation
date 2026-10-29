#include "MoistureSensor.h"

#include <algorithm>

MoistureSensor::MoistureSensor(uint8_t analogPin, int8_t digitalPin)
    : analogPin_(analogPin), digitalPin_(digitalPin) {}

void MoistureSensor::begin() {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  if (digitalPin_ >= 0) {
    pinMode(digitalPin_, INPUT);
  }
}

SensorReading MoistureSensor::sample(int dryRaw, int wetRaw) {
  SensorReading result;
  int readings[7];
  for (int index = 0; index < 7; ++index) {
    readings[index] = analogRead(analogPin_);
    delay(2);
  }
  std::sort(readings, readings + 7);
  result.raw = readings[3];

  if (digitalPin_ >= 0) {
    result.digitalTrigger = digitalRead(digitalPin_);
  }

  // Probe disconnected/pulled completely low
  if (result.raw < 5) {
    result.fault = "Sensor disconnected";
    result.valid = false;
    return result;
  }

  if (dryRaw == wetRaw) {
    result.fault = "Needs calibration";
    result.valid = false;
    return result;
  }

  // Calculate percentage smoothly. High raw = dry air, Low raw = wet soil
  const float percent = 100.0f * static_cast<float>(dryRaw - result.raw) / static_cast<float>(dryRaw - wetRaw);
  result.moisture = constrain(static_cast<int>(percent + 0.5f), 0, 100);
  result.valid = true;
  return result;
}
