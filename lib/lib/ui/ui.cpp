#include "ui.h"

namespace {
constexpr uint8_t kDisplayWidth = 128;
constexpr uint8_t kDisplayHeight = 64;
constexpr uint8_t kStatusBarHeight = 14;
constexpr uint8_t kNavBarHeight = 0;
constexpr uint8_t kMenuTop = kStatusBarHeight + 2;
constexpr uint8_t kMenuBottom = kDisplayHeight - kNavBarHeight - 2;
constexpr uint8_t kMenuAreaHeight = kMenuBottom - kMenuTop;
constexpr uint8_t kMenuItemHeight = kMenuAreaHeight / 2;
constexpr uint8_t kChargeWidth = 9;
constexpr uint8_t kChargeHeight = 9;
const uint8_t kChargeBitmap[] = {
    0x00, 0x00, 0x08, 0x00, 0x18, 0x00, 0x30, 0x00, 0x7c,
    0x00, 0x18, 0x00, 0x30, 0x00, 0x20, 0x00, 0x00, 0x00,
};
constexpr uint8_t kWifiWidth = 11;
constexpr uint8_t kWifiHeight = 7;
const uint8_t kWifiBitmap[] = {
    0xfc, 0x01, 0x02, 0x02, 0xf9, 0x04, 0x04,
    0x01, 0x70, 0x00, 0x00, 0x00, 0x20, 0x00,
};
constexpr uint8_t kBleWidth = 11;
constexpr uint8_t kBleHeight = 11;
const uint8_t kBleBitmap[] = {
    0x00, 0x00, 0x60, 0x00, 0xa0, 0x00, 0x24, 0x01, 0xa8,
    0x00, 0x70, 0x00, 0xa8, 0x00, 0x24, 0x01, 0xa0, 0x00,
    0x60, 0x00, 0x00, 0x00,
};

bool hasBitmap(const UiBitmap &bitmap) {
  return bitmap.data != nullptr && bitmap.width > 0 && bitmap.height > 0;
}

uint8_t menuVisibleCount() {
  return 2;
}
}

Ui::Ui(U8G2 &display) : display_(display) {}

void Ui::begin() {
  display_.setFont(u8g2_font_6x10_tf);
  display_.setDrawColor(1);
}

void Ui::setAssets(const UiAssets *assets) {
  assets_ = assets;
}

void Ui::render(const AppState &state, const char *const *menuItems, size_t menuCount) {
  display_.clearBuffer();
  drawStatusBar(state);
  drawNavBar();
  drawMenu(state, menuItems, menuCount);
  drawFault(state);
  display_.sendBuffer();
}

void Ui::renderStatusBar(const AppState &state) {
  drawStatusBar(state);
}

void Ui::drawStatusBar(const AppState &state) {
  display_.setDrawColor(1);
  if (assets_ != nullptr && hasBitmap(assets_->statusBar)) {
    drawBitmapMaybe(0, 0, assets_->statusBar);
  }

  const uint8_t batteryWidth = 20;
  const uint8_t batteryTipWidth = 2;
  const uint8_t batteryX = kDisplayWidth - (batteryWidth + batteryTipWidth) - 2;
  uint8_t chargeWidth = kChargeWidth;
  if (assets_ != nullptr && hasBitmap(assets_->iconCharge)) {
    chargeWidth = assets_->iconCharge.width;
  }
  const uint8_t chargeX = static_cast<uint8_t>(batteryX - chargeWidth - 2);

  display_.setFont(u8g2_font_6x10_tf);
  const char *label = "OFF";
  if (state.connection == ConnectionMode::BLE) {
    label = "BLE";
  } else if (state.connection == ConnectionMode::Wifi) {
    label = "WiFi";
  }
  uint8_t connectionWidth = static_cast<uint8_t>(display_.getStrWidth(label));
  if (state.connection == ConnectionMode::BLE) {
    if (assets_ != nullptr && hasBitmap(assets_->iconBle)) {
      connectionWidth = assets_->iconBle.width;
    } else {
      connectionWidth = kBleWidth;
    }
  } else if (state.connection == ConnectionMode::Wifi) {
    if (assets_ != nullptr && hasBitmap(assets_->iconWifi)) {
      connectionWidth = assets_->iconWifi.width;
    } else {
      connectionWidth = kWifiWidth;
    }
  }
  const uint8_t connectionRight =
      state.charging ? chargeX - 4 : static_cast<uint8_t>(batteryX - 4);
  const uint8_t connectionX = static_cast<uint8_t>(connectionRight - connectionWidth);
  const uint8_t separatorX = static_cast<uint8_t>(connectionX - 5);

  drawBattery(state.batteryPercent, state.charging, batteryX, chargeX);
  drawConnection(state.connection, connectionX);
  drawLastInfo(state);
  display_.drawLine(separatorX, 1, separatorX, kStatusBarHeight - 5);
}

void Ui::drawNavBar() {
  if (kNavBarHeight == 0) {
    return;
  }
  const uint8_t y = kDisplayHeight - kNavBarHeight;
  if (assets_ != nullptr && hasBitmap(assets_->navBar)) {
    drawBitmapMaybe(0, y, assets_->navBar);
  }
}

void Ui::drawBattery(uint8_t percent, bool charging, uint8_t batteryX, uint8_t chargeX) {
  const uint8_t y = 1;

  if (assets_ != nullptr && hasBitmap(assets_->iconBattery)) {
    drawBitmapMaybe(batteryX, y, assets_->iconBattery);
  } else {
    const uint8_t batteryWidth = 20;
    const uint8_t batteryHeight = 8;
    display_.drawFrame(batteryX, y, batteryWidth, batteryHeight);
    display_.drawBox(batteryX + batteryWidth, y + 2, 2, batteryHeight - 4);

    const uint8_t fillWidth = (batteryWidth - 2) * min<uint8_t>(percent, 100) / 100;
    display_.drawBox(batteryX + 1, y + 1, fillWidth, batteryHeight - 2);
  }

  if (charging) {
    if (assets_ != nullptr && hasBitmap(assets_->iconCharge)) {
      drawBitmapMaybe(chargeX, y, assets_->iconCharge);
    } else {
      display_.drawXBMP(chargeX, y, kChargeWidth, kChargeHeight, kChargeBitmap);
    }
  }
}

void Ui::drawConnection(ConnectionMode mode, uint8_t iconX) {
  const uint8_t iconY = 1;
  const char *label = "OFF";

  if (mode == ConnectionMode::BLE) {
    if (assets_ != nullptr && hasBitmap(assets_->iconBle)) {
      drawBitmapMaybe(iconX, iconY, assets_->iconBle);
      return;
    }
    const int16_t bleX = static_cast<int16_t>(iconX) + 2;
    const int16_t bleY = static_cast<int16_t>(iconY) - 2;
    const uint8_t drawBleX = bleX < 0 ? 0 : static_cast<uint8_t>(bleX);
    const uint8_t drawBleY = bleY < 0 ? 0 : static_cast<uint8_t>(bleY);
    display_.drawXBMP(drawBleX, drawBleY, kBleWidth, kBleHeight, kBleBitmap);
    return;
  } else if (mode == ConnectionMode::Wifi) {
    if (assets_ != nullptr && hasBitmap(assets_->iconWifi)) {
      drawBitmapMaybe(iconX, iconY, assets_->iconWifi);
      return;
    }
    const int16_t wifiX = static_cast<int16_t>(iconX) + 1;
    const int16_t wifiY = static_cast<int16_t>(iconY) + 1;
    const uint8_t drawWifiX = wifiX < 0 ? 0 : static_cast<uint8_t>(wifiX);
    const uint8_t drawWifiY = wifiY < 0 ? 0 : static_cast<uint8_t>(wifiY);
    display_.drawXBMP(drawWifiX, drawWifiY, kWifiWidth, kWifiHeight, kWifiBitmap);
    return;
  }

  display_.setFont(u8g2_font_6x10_tf);
  display_.setCursor(iconX, iconY + 8);
  display_.print(label);
}

void Ui::drawLastInfo(const AppState &state) {
  const uint8_t infoY = 1;

  display_.setFont(u8g2_font_5x8_tf);
  const char *name = state.lastUser != nullptr ? state.lastUser : "--";
  display_.setCursor(2, infoY + 8);
  display_.print(name);

  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%.2f", state.lastMeasurement);
  const uint8_t nameWidth = static_cast<uint8_t>(display_.getStrWidth(name));
  const uint8_t promilleX = static_cast<uint8_t>(2 + nameWidth + 6);
  display_.setCursor(promilleX, infoY + 8);
  display_.print(buffer);
}

void Ui::drawMenu(const AppState &state, const char *const *menuItems, size_t menuCount) {
  if (assets_ != nullptr && hasBitmap(assets_->menuBackground)) {
    drawBitmapMaybe(0, kMenuTop - 1, assets_->menuBackground);
  }

  const uint8_t totalItems = static_cast<uint8_t>(menuCount);
  const uint8_t visibleCount = menuVisibleCount();
  uint8_t firstIndex = 0;
  if (state.menuIndex >= visibleCount) {
    firstIndex = state.menuIndex - (visibleCount - 1);
  }
  if (firstIndex + visibleCount > totalItems) {
    if (totalItems > visibleCount) {
      firstIndex = totalItems - visibleCount;
    } else {
      firstIndex = 0;
    }
  }

  for (uint8_t i = 0; i < visibleCount; ++i) {
    const uint8_t itemIndex = firstIndex + i;
    if (itemIndex >= totalItems) {
      break;
    }

    const uint8_t rowTop = kMenuTop + i * kMenuItemHeight;
    const bool selected = (itemIndex == state.menuIndex);

    if (selected) {
      display_.setDrawColor(1);
      display_.drawFrame(0, rowTop, kDisplayWidth, kMenuItemHeight);
    }

    display_.setDrawColor(1);
    display_.setFont(u8g2_font_6x10_tf);
    const uint8_t iconSpace = 16;
    const uint8_t textX = iconSpace + 20;
    const int16_t ascent = display_.getAscent();
    const int16_t descent = display_.getDescent();
    const int16_t textHeight = ascent - descent;
    const int16_t textY = rowTop + (kMenuItemHeight - textHeight) / 2 + ascent;
    display_.setCursor(textX, static_cast<uint8_t>(textY));
    display_.print(menuItems[itemIndex]);
  }

  display_.setDrawColor(1);
}

void Ui::drawFault(const AppState &state) {
  if (state.chargerFaultReason == 0) {
    return;
  }

  const uint8_t boxX = 6;
  const uint8_t boxY = 18;
  const uint8_t boxW = kDisplayWidth - 12;
  const uint8_t boxH = 30;

  display_.setDrawColor(1);
  display_.drawBox(boxX, boxY, boxW, boxH);
  display_.setDrawColor(0);

  const char *title = "Batterifel";
  const char *detail = "Okant fel";
  switch (state.chargerFaultReason) {
    case 1:
      detail = "TS-fel";
      if (state.chargerTsFault == 2) {
        detail = "Too hot";
      } else if (state.chargerTsFault == 1) {
        detail = "Too cold";
      }
      break;
    case 2:
      detail = "Input-fel";
      break;
    case 3:
      detail = "Overtemp";
      break;
    case 4:
      detail = "Safety timer";
      break;
    case 5:
      detail = "Batterifel";
      break;
    case 6:
      detail = "Laddfel";
      break;
    default:
      break;
  }

  display_.setFont(u8g2_font_6x10_tf);
  display_.setCursor(boxX + 6, boxY + 12);
  display_.print(title);
  display_.setCursor(boxX + 6, boxY + 24);
  display_.print(detail);

  display_.setDrawColor(1);
}

void Ui::drawBitmapMaybe(uint8_t x, uint8_t y, const UiBitmap &bitmap) {
  if (!hasBitmap(bitmap)) {
    return;
  }
  display_.drawXBMP(x, y, bitmap.width, bitmap.height, bitmap.data);
}
