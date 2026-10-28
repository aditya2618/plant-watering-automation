#pragma once

#include <Arduino.h>

struct SensorReading {
  int raw = 0;
  int moisture = 0;
  bool valid = false;
  String fault;
  int digitalTrigger = -1; // -1 if not wired, 0 = LOW, 1 = HIGH
};

class MoistureSensor {
 public:
  explicit MoistureSensor(uint8_t analogPin, int8_t digitalPin = -1);
  void begin();
  SensorReading sample(int dryRaw, int wetRaw);

 private:
  uint8_t analogPin_;
  int8_t digitalPin_;
};
