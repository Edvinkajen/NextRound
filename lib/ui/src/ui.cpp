#include "ui.h"
#include "display_constants.h"

namespace {
constexpr uint8_t kMenuItemHeight = kMenuHeight / 2;

constexpr uint8_t kChargeWidth  = 9;
constexpr uint8_t kChargeHeight = 9;
const uint8_t kChargeBitmap[] = {
    0x00, 0x00, 0x08, 0x00, 0x18, 0x00, 0x30, 0x00, 0x7c,
    0x00, 0x18, 0x00, 0x30, 0x00, 0x20, 0x00, 0x00, 0x00,
};

constexpr uint8_t kWifiWidth  = 11;
constexpr uint8_t kWifiHeight = 7;
const uint8_t kWifiBitmap[] = {
    0xfc, 0x01, 0x02, 0x02, 0xf9, 0x04, 0x04,
    0x01, 0x70, 0x00, 0x00, 0x00, 0x20, 0x00,
};

constexpr uint8_t kBleWidth  = 11;
constexpr uint8_t kBleHeight = 11;
const uint8_t kBleBitmap[] = {
    0x00, 0x00, 0x60, 0x00, 0xa0, 0x00, 0x24, 0x01, 0xa8,
    0x00, 0x70, 0x00, 0xa8, 0x00, 0x24, 0x01, 0xa0, 0x00,
    0x60, 0x00, 0x00, 0x00,
};

bool hasBitmap(const UiBitmap& bitmap) {
  return bitmap.data != nullptr && bitmap.width > 0 && bitmap.height > 0;
}
}  // namespace

Ui::Ui(U8G2& display) : display_(display) {}

void Ui::begin() {
  display_.setFont(u8g2_font_6x10_tf);
  display_.setDrawColor(1);
}

void Ui::setAssets(const UiAssets* assets) {
  assets_ = assets;
}

void Ui::render(const AppState& state, const char* const* menuItems, size_t menuCount) {
  display_.clearBuffer();
  drawStatusBar(state);
  drawNavBar();
  drawMenu(state, menuItems, menuCount);
  drawFault(state);
  display_.sendBuffer();
}

void Ui::renderStatusBar(const AppState& state) {
  drawStatusBar(state);
}

void Ui::drawStatusBar(const AppState& state) {
  display_.setDrawColor(1);
  if (assets_ != nullptr && hasBitmap(assets_->statusBar)) {
    drawBitmapMaybe(0, 0, assets_->statusBar);
  }

  const uint8_t batteryWidth    = 20;
  const uint8_t batteryTipWidth = 2;
  const uint8_t batteryX = static_cast<uint8_t>(kDisplayWidth - batteryWidth - batteryTipWidth);

  uint8_t chargeWidth = kChargeWidth;
  if (assets_ != nullptr && hasBitmap(assets_->iconCharge)) {
    chargeWidth = assets_->iconCharge.width;
  }
  const uint8_t chargeX = static_cast<uint8_t>(batteryX - chargeWidth - 2);

  display_.setFont(u8g2_font_6x10_tf);
  uint8_t connectionWidth = 0;
  if (state.connection == ConnectionMode::BLE) {
    connectionWidth = (assets_ != nullptr && hasBitmap(assets_->iconBle))
                          ? assets_->iconBle.width
                          : kBleWidth;
  } else if (state.connection == ConnectionMode::Wifi) {
    connectionWidth = (assets_ != nullptr && hasBitmap(assets_->iconWifi))
                          ? assets_->iconWifi.width
                          : kWifiWidth;
  } else {
    connectionWidth = static_cast<uint8_t>(display_.getStrWidth("OFF"));
  }

  const uint8_t connectionRight =
      state.charging ? static_cast<uint8_t>(chargeX - 4)
                     : static_cast<uint8_t>(batteryX - 4);
  const uint8_t connectionX = static_cast<uint8_t>(connectionRight - connectionWidth);
  const uint8_t separatorX  = static_cast<uint8_t>(connectionX - 5);

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
    const uint8_t batteryWidth  = 20;
    const uint8_t batteryHeight = 8;
    display_.drawFrame(batteryX, y, batteryWidth, batteryHeight);
    display_.drawBox(static_cast<uint8_t>(batteryX + batteryWidth), y + 2, 2, batteryHeight - 4);

    const uint8_t fillWidth =
        static_cast<uint8_t>((batteryWidth - 2) * min<uint8_t>(percent, 100) / 100);
    display_.drawBox(static_cast<uint8_t>(batteryX + 1), y + 1, fillWidth, batteryHeight - 2);
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

  if (mode == ConnectionMode::BLE) {
    if (assets_ != nullptr && hasBitmap(assets_->iconBle)) {
      drawBitmapMaybe(iconX, 0, assets_->iconBle);
      return;
    }
    const uint8_t bleX = static_cast<uint8_t>(iconX + 2);
    display_.drawXBMP(bleX, 0, kBleWidth, kBleHeight, kBleBitmap);
    return;
  }

  if (mode == ConnectionMode::Wifi) {
    if (assets_ != nullptr && hasBitmap(assets_->iconWifi)) {
      drawBitmapMaybe(iconX, iconY, assets_->iconWifi);
      return;
    }
    const uint8_t wifiX = static_cast<uint8_t>(iconX + 1);
    const uint8_t wifiY = iconY + 1;
    display_.drawXBMP(wifiX, wifiY, kWifiWidth, kWifiHeight, kWifiBitmap);
    return;
  }

  display_.setFont(u8g2_font_6x10_tf);
  display_.setCursor(iconX, iconY + 8);
  display_.print("OFF");
}


void Ui::drawMenu(const AppState& state, const char* const* menuItems, size_t menuCount) {
  if (assets_ != nullptr && hasBitmap(assets_->menuBackground)) {
    drawBitmapMaybe(0, kMenuTop - 1, assets_->menuBackground);
  }

  const uint8_t totalItems   = static_cast<uint8_t>(menuCount);
  constexpr uint8_t kVisible = 2;
  uint8_t firstIndex = 0;
  if (state.menuIndex >= kVisible) {
    firstIndex = state.menuIndex - (kVisible - 1);
  }
  if (firstIndex + kVisible > totalItems && totalItems > kVisible) {
    firstIndex = totalItems - kVisible;
  }

  for (uint8_t i = 0; i < kVisible; ++i) {
    const uint8_t idx = firstIndex + i;
    if (idx >= totalItems) break;

    const uint8_t rowTop  = kMenuTop + i * kMenuItemHeight;
    const bool    selected = (idx == state.menuIndex);

    if (selected) {
      display_.setDrawColor(1);
      display_.drawFrame(0, rowTop, kDisplayWidth, kMenuItemHeight);
    }

    display_.setDrawColor(1);
    display_.setFont(u8g2_font_6x10_tf);
    const uint8_t textX    = 36;
    const int16_t ascent   = display_.getAscent();
    const int16_t descent  = display_.getDescent();
    const int16_t textH    = ascent - descent;
    const int16_t textY    = rowTop + (kMenuItemHeight - textH) / 2 + ascent;
    display_.setCursor(textX, static_cast<uint8_t>(textY));
    display_.print(menuItems[idx]);
  }

  display_.setDrawColor(1);
}

void Ui::drawFault(const AppState& state) {
  if (state.chargerFaultReason == 0) {
    return;
  }

  constexpr uint8_t kBoxX = 6;
  constexpr uint8_t kBoxY = 18;
  constexpr uint8_t kBoxW = kDisplayWidth - 12;
  constexpr uint8_t kBoxH = 30;

  display_.setDrawColor(1);
  display_.drawBox(kBoxX, kBoxY, kBoxW, kBoxH);
  display_.setDrawColor(0);

  const char* detail = "Unknown fault";
  switch (state.chargerFaultReason) {
    case 1:
      detail = (state.chargerTsFault == 2) ? "Too hot"
             : (state.chargerTsFault == 1) ? "Too cold"
             : "Thermal fault";
      break;
    case 2: detail = "Input fault";    break;
    case 3: detail = "Overtemp";       break;
    case 4: detail = "Safety timer";   break;
    case 5: detail = "Battery fault";  break;
    case 6: detail = "Charge fault";   break;
    default: break;
  }

  display_.setFont(u8g2_font_6x10_tf);
  display_.setCursor(kBoxX + 6, kBoxY + 12);
  display_.print("Charger fault");
  display_.setCursor(kBoxX + 6, kBoxY + 24);
  display_.print(detail);

  display_.setDrawColor(1);
}

void Ui::drawLastInfo(const AppState& state) {
  const uint8_t infoY = 1;
  display_.setFont(u8g2_font_5x8_tf);
  const char* name = state.lastUser != nullptr ? state.lastUser : "--";
  display_.setCursor(2, infoY + 8);
  display_.print(name);

  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%.2f", state.lastMeasurement);
  const uint8_t nameWidth = static_cast<uint8_t>(display_.getStrWidth(name));
  const uint8_t promilleX = static_cast<uint8_t>(2 + nameWidth + 6);
  display_.setCursor(promilleX, infoY + 8);
  display_.print(buffer);
}

void Ui::drawBitmapMaybe(uint8_t x, uint8_t y, const UiBitmap& bitmap) {
  if (!hasBitmap(bitmap)) return;
  display_.drawXBMP(x, y, bitmap.width, bitmap.height, bitmap.data);
}
