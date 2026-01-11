#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

#include "appstate.h"
#include "ui_assets.h"

class Ui {
 public:
  explicit Ui(U8G2 &display);

  void begin();
  void setAssets(const UiAssets *assets);
  void render(const AppState &state, const char *const *menuItems, size_t menuCount);
  void renderStatusBar(const AppState &state);

 private:
  void drawStatusBar(const AppState &state);
  void drawNavBar();
  void drawMenu(const AppState &state, const char *const *menuItems, size_t menuCount);
  void drawFault(const AppState &state);
  void drawBattery(uint8_t percent, bool charging, uint8_t batteryX, uint8_t chargeX);
  void drawConnection(ConnectionMode mode, uint8_t x);
  void drawLastInfo(const AppState &state);

  void drawBitmapMaybe(uint8_t x, uint8_t y, const UiBitmap &bitmap);

  U8G2 &display_;
  const UiAssets *assets_ = nullptr;
};
