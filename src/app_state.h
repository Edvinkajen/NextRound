#pragma once

#include <Arduino.h>

enum class ConnectionMode : uint8_t {
  None = 0,
  BLE,
  Wifi,
};

struct AppState {
  uint8_t batteryPercent = 0;
  bool charging = false;
  ConnectionMode connection = ConnectionMode::None;
  const char *lastUser = nullptr;
  float lastMeasurement = 0.0f;
  uint8_t menuIndex = 0;
  uint8_t chargerFaultReason = 0;
  uint8_t chargerTsFault = 0;
};
