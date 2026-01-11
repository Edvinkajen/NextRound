#pragma once

#include <Arduino.h>

struct UiBitmap {
  const uint8_t *data = nullptr;
  uint8_t width = 0;
  uint8_t height = 0;
};

struct UiAssets {
  UiBitmap statusBar;
  UiBitmap navBar;
  UiBitmap menuBackground;
  UiBitmap iconBle;
  UiBitmap iconWifi;
  UiBitmap iconCharge;
  UiBitmap iconBattery;
};
