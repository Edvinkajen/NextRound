#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

namespace WifiServer {

struct DashboardData {
  uint8_t     batteryPercent  = 0;
  bool        charging        = false;
  float       lastMeasurement = 0.0f;
  const char* lastUser        = nullptr;
  float       tempC           = 25.0f;
  float       humidity        = 50.0f;
};

// Call once early in normal firmware to confirm the running image is valid
// and cancel any OTA rollback timer.
bool markAppValid();

// Enters blocking WiFi mode:
// - Starts SoftAP with random SSID/password
// - Shows QR code + IP on OLED
// - Serves HTML dashboard at http://192.168.4.1
// - Accepts OTA firmware upload at /update
// - Returns when user presses button for 2+ seconds or OTA update reboots
void enterBlocking(U8G2& display, const DashboardData& data);

}  // namespace WifiServer
