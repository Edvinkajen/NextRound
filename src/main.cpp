#include <Arduino.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <LittleFS.h>
#include <math.h>
#include <time.h>

#include "ui/ui.h"
#include "qrcode_bitmap.h"
#include "pins.h"
#include "minigames.h"

constexpr uint32_t kButtonDebounceMs = 30;
constexpr uint32_t kButtonLongPressMs = 800;
constexpr uint32_t kButtonExtraLongMs = 7000;
constexpr uint32_t kUiTickMs = 30;
constexpr uint32_t kSleepCountdownMs = 3000;
constexpr uint32_t kResetCountdownMs = 10000;
constexpr uint16_t kSleepTimeoutMinSec = 0;
constexpr uint16_t kSleepTimeoutMaxSec = 900;
constexpr uint16_t kSleepTimeoutStepSec = 30;
constexpr uint8_t kDisplayWidth = 128;
constexpr uint8_t kDisplayHeight = 64;
constexpr uint8_t kStatusBarHeight = 14;
constexpr uint8_t kNavBarHeight = 0;
constexpr uint8_t kMenuTop = kStatusBarHeight + 2;
constexpr uint8_t kMenuBottom = kDisplayHeight - kNavBarHeight - 2;
constexpr uint8_t kMenuHeight = kMenuBottom - kMenuTop;
constexpr uint32_t kMeasureCountdownSec = 20;
constexpr uint32_t kRoulettePromilleMs = 3000;
constexpr uint32_t kRouletteResultMs = 3000;
constexpr uint32_t kDuelResultMs = 5000;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R2, U8X8_PIN_NONE);
Ui ui(display);

const char *const kMenuItems[] = {
    "Measure",
    "History",
    "Party",
    "Turn OFF",
    "Settings",
    "About",
};
constexpr uint8_t kSleepMenuIndex = 3;
constexpr uint8_t kAboutMenuIndex = 5;
constexpr uint8_t kSettingsMenuIndex = 4;
constexpr uint8_t kPartyMenuIndex = 2;

const char *const kHistoryMenuItems[] = {
    "Return",
    "Latest",
    "Week",
};

const char *const kPartyMenuItems[] = {
    "Return",
    "Duel Mode",
    "Roulette",
    "Simon Says",
};
constexpr uint8_t kPartyDuelIndex = 1;
constexpr uint8_t kPartyRouletteIndex = 2;

const char *const kSettingsMenuItems[] = {
    "Return",
    "Calib",
    "LED",
    "Buzzer",
    "Vibration",
    "WIFI/BLE",
    "Sensor",
    "OFF Timer",
    "Reset",
};
constexpr uint8_t kSettingCalibIndex = 1;
constexpr uint8_t kSettingLedIndex = 2;
constexpr uint8_t kSettingBuzzIndex = 3;
constexpr uint8_t kSettingVibIndex = 4;
constexpr uint8_t kSettingWifiBleIndex = 5;
constexpr uint8_t kSettingSensIndex = 6;
constexpr uint8_t kSettingSleepIndex = 7;
constexpr uint8_t kSettingResetIndex = 8;

const char *const kWifiBleMenuItems[] = {
    "Return",
    "BLE",
    "WiFi",
};

struct ButtonState {
  bool lastLevel = false;
  uint32_t pressedMs = 0;
  bool longReported = false;
  bool extraReported = false;
  bool pendingShort = false;
};

AppState appState;
uint8_t mainMenuIndex = 0;
uint8_t submenuIndex = 0;
bool inSubmenu = false;
bool showingQr = false;
bool inWifiBleMenu = false;
ButtonState button;
uint32_t lastUiTickMs = 0;
bool faultButtonLast = true;
uint32_t faultButtonLastMs = 0;
bool measuringActive = false;
uint32_t measureStartMs = 0;
bool sleepPending = false;
uint32_t sleepStartMs = 0;
bool adjustingSetting = false;
uint8_t adjustingSettingSlot = 0;
uint8_t settingValues[3] = {5, 5, 5};
uint8_t settingSensValue = 10;
uint8_t wifiBleMenuIndex = 0;
bool settingsDirty = false;
bool resetPending = false;
uint32_t resetStartMs = 0;
const char kUsersFile[] = "/users.json";
bool rouletteEnabled = false;
RussianRoulette roulette;
enum class RoulettePhase : uint8_t { Idle, ShowPromille, ShowResult };
RoulettePhase roulettePhase = RoulettePhase::Idle;
uint32_t roulettePhaseStartMs = 0;
bool rouletteLastHit = false;
bool duelEnabled = false;
DuelMode duel;
enum class DuelPhase : uint8_t { Idle, Versus, FirstStart, SecondStart, ShowResult };
DuelPhase duelPhase = DuelPhase::Idle;
uint32_t duelPhaseStartMs = 0;
String duelMessage;
String duelUserA;
String duelUserB;
float duelMeasA = 0.0f;
float duelMeasB = 0.0f;
uint16_t sleepTimeoutSec = 300;
uint32_t lastInteractionMs = 0;

void scanI2c() {
  Serial.println("I2C scan start");
  uint8_t count = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print("I2C device at 0x");
      if (address < 16) {
        Serial.print('0');
      }
      Serial.println(address, HEX);
      ++count;
    }
  }
  Serial.print("I2C scan done, found ");
  Serial.println(count);
}


bool hasSubmenuForMain(uint8_t index) {
  return index == 1 || index == 2 || index == kSettingsMenuIndex;
}

const char *const *submenuItemsForMain(uint8_t index, size_t &count) {
  switch (index) {
    case 1:
      count = sizeof(kHistoryMenuItems) / sizeof(kHistoryMenuItems[0]);
      return kHistoryMenuItems;
    case 2:
      count = sizeof(kPartyMenuItems) / sizeof(kPartyMenuItems[0]);
      return kPartyMenuItems;
    case kSettingsMenuIndex:
      count = sizeof(kSettingsMenuItems) / sizeof(kSettingsMenuItems[0]);
      return kSettingsMenuItems;
    default:
      count = 0;
      return nullptr;
  }
}


void renderMeasurement(uint32_t nowMs) {
  display.clearBuffer();
  ui.renderStatusBar(appState);
  display.setDrawColor(0);
  display.drawBox(0, kMenuTop, kDisplayWidth, kMenuHeight);
  display.setDrawColor(1);

  const uint32_t elapsedMs = nowMs - measureStartMs;
  const bool heating = elapsedMs < kMeasureCountdownSec * 1000UL;
  const char *title = heating ? "Heating" : "Blow";

  display.setFont(u8g2_font_9x15_tf);
  const uint8_t titleWidth = display.getStrWidth(title);
  const uint8_t titleX = (kDisplayWidth - titleWidth) / 2;
  display.setCursor(titleX, kMenuTop + 20);
  display.print(title);

  if (heating) {
    const uint32_t remainingMs = kMeasureCountdownSec * 1000UL - elapsedMs;
    const uint32_t remainingSec = (remainingMs + 999) / 1000;
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(remainingSec));
    display.setFont(u8g2_font_6x10_tf);
    const uint8_t countWidth = display.getStrWidth(buffer);
    const uint8_t countX = (kDisplayWidth - countWidth) / 2;
    display.setCursor(countX, kMenuTop + 40);
    display.print(buffer);
  }

  display.sendBuffer();
}

void renderQrCode() {
  display.clearBuffer();
  const uint8_t x = kDisplayWidth - kQrCodeWidth;
  const uint8_t y = (kDisplayHeight - kQrCodeHeight) / 2;
  display.drawXBMP(x, y, kQrCodeWidth, kQrCodeHeight, kQrCodeBits);

  display.setFont(u8g2_font_5x8_tf);
  const uint8_t textX = 2;
  uint8_t textY = 14;
  display.setCursor(textX, textY);
  display.print("Alkoblas V2");
  textY += 12;
  display.setCursor(textX, textY);
  display.print("HW Rev A");
  textY += 12;
  display.setCursor(textX, textY);
  display.print("Firm: 1.0.0");
  textY += 12;
  display.setCursor(textX, textY);
  display.print("Date Q1-26");

  display.sendBuffer();
}

void renderRoulettePromille() {
  display.clearBuffer();
  ui.renderStatusBar(appState);
  display.setDrawColor(0);
  display.drawBox(0, kMenuTop, kDisplayWidth, kMenuHeight);
  display.setDrawColor(1);

  display.setFont(u8g2_font_9x15_tf);
  display.setCursor(18, kMenuTop + 20);
  display.print("Promille");

  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%.2f", appState.lastMeasurement);
  display.setFont(u8g2_font_6x10_tf);
  const uint8_t valueWidth = display.getStrWidth(buffer);
  const uint8_t valueX = (kDisplayWidth - valueWidth) / 2;
  display.setCursor(valueX, kMenuTop + 40);
  display.print(buffer);

  display.sendBuffer();
}

void renderRouletteResult() {
  display.clearBuffer();
  ui.renderStatusBar(appState);
  display.setDrawColor(0);
  display.drawBox(0, kMenuTop, kDisplayWidth, kMenuHeight);
  display.setDrawColor(1);

  display.setFont(u8g2_font_9x15_tf);
  display.setCursor(20, kMenuTop + 30);
  display.print(rouletteLastHit ? "BANG!" : "Blank");

  display.sendBuffer();
}

void renderDuelResult() {
  display.clearBuffer();
  ui.renderStatusBar(appState);
  display.setDrawColor(0);
  display.drawBox(0, kMenuTop, kDisplayWidth, kMenuHeight);
  display.setDrawColor(1);

  display.setFont(u8g2_font_6x10_tf);
  display.setCursor(6, kMenuTop + 22);
  display.print(duelMessage);

  display.sendBuffer();
}

void renderDuelVersus(const String &userA, const String &userB) {
  display.clearBuffer();
  ui.renderStatusBar(appState);
  display.setDrawColor(0);
  display.drawBox(0, kMenuTop, kDisplayWidth, kMenuHeight);
  display.setDrawColor(1);

  display.setFont(u8g2_font_6x10_tf);
  display.setCursor(10, kMenuTop + 18);
  display.print(userA);
  display.setCursor(10, kMenuTop + 32);
  display.print("VS");
  display.setCursor(10, kMenuTop + 46);
  display.print(userB);

  display.sendBuffer();
}

void renderDuelPrompt(const String &user, const char *label) {
  display.clearBuffer();
  ui.renderStatusBar(appState);
  display.setDrawColor(0);
  display.drawBox(0, kMenuTop, kDisplayWidth, kMenuHeight);
  display.setDrawColor(1);

  display.setFont(u8g2_font_6x10_tf);
  display.setCursor(10, kMenuTop + 22);
  display.print(user);
  display.setCursor(10, kMenuTop + 38);
  display.print(label);

  display.sendBuffer();
}

constexpr uint8_t kIcon16 = 16;
constexpr uint8_t kIcon15 = 15;
const uint8_t kIconMeasure[] = {
    0x00, 0x00, 0x02, 0x00, 0x07, 0x00, 0x82, 0x3f, 0x42, 0x00, 0x22,
    0x00, 0x22, 0x00, 0x12, 0x00, 0x12, 0x00, 0x12, 0x00, 0x0a,
    0x00, 0x0a, 0x00, 0x0a, 0x00, 0x06, 0x20, 0xfe, 0x7f, 0x00,
    0x20,
};
const uint8_t kIconHistory[] = {
    0xe0, 0x07, 0x18, 0x18, 0x84, 0x20, 0x82, 0x40, 0x82, 0x40, 0x81,
    0x80, 0x81, 0x80, 0x81, 0x80, 0x81, 0xbf, 0x01, 0x80, 0x01,
    0x80, 0x02, 0x40, 0x02, 0x40, 0x04, 0x20, 0x18, 0x18, 0xe0,
    0x07,
};
const uint8_t kIconParty[] = {
    0x80, 0x00, 0x08, 0x12, 0x10, 0x0a, 0x00, 0x21, 0x20, 0x10, 0x70,
    0xc8, 0xc8, 0x20, 0x88, 0x01, 0x0c, 0x63, 0x1c, 0x86, 0x32,
    0x0c, 0x62, 0x24, 0xc1, 0x83, 0xc1, 0x00, 0x31, 0x00, 0x0f,
    0x00,
};
const uint8_t kIconSleep[] = {
    0x00, 0x00, 0xe0, 0x03, 0x18, 0x0c, 0x84, 0x10, 0x84, 0x10, 0xa2,
    0x22, 0x92, 0x24, 0x12, 0x24, 0x12, 0x24, 0x22, 0x22, 0xc4,
    0x11, 0x04, 0x10, 0x18, 0x0c, 0xe0, 0x03, 0x00, 0x00,
};
const uint8_t kIconSettings[] = {
    0x00, 0x18, 0x00, 0x1c, 0x00, 0x0e, 0x00, 0xc6, 0x00, 0xe6, 0x00,
    0x7f, 0x80, 0x3f, 0xc0, 0x07, 0xe0, 0x03, 0xf0, 0x01, 0xfc,
    0x00, 0x76, 0x00, 0x22, 0x00, 0x32, 0x00, 0x1e, 0x00, 0x00,
    0x00,
};
const uint8_t kIconAbout[] = {
    0xe0, 0x07, 0x18, 0x18, 0x04, 0x20, 0x82, 0x41, 0x82, 0x41, 0x01,
    0x80, 0x01, 0x80, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x82, 0x41, 0x82, 0x41, 0x04, 0x20, 0x18, 0x18, 0xe0,
    0x07,
};
const uint8_t kIconDuel[] = {
    0x00, 0x00, 0x0e, 0x70, 0x16, 0x68, 0x2a, 0x54, 0x54, 0x2a, 0xa8, 
    0x15, 0x50, 0x09, 0xa0, 0x06, 0x60, 0x05, 0x94, 0x2a, 0xac, 0x35,
    0x58, 0x1a, 0x3c, 0x3c, 0x6e, 0x76, 0x07, 0xe0, 0x03, 0xc0
};
const uint8_t kIconRoulette[] = {
    0x00, 0x00, 0x00, 0x00, 0xf3, 0xc7, 0x3e, 0xfc, 0xec, 0xff, 0x38,
    0xfc, 0xf8, 0x07, 0xb8, 0x02, 0x3c, 0x02, 0xfc, 0x01, 0x3c, 0x00,
    0x3e, 0x00, 0x3e, 0x00, 0x3e, 0x00, 0x1c, 0x00, 0x00, 0x00,
};
const uint8_t kIconReturn[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x18, 0x00, 0xfc,
    0x03, 0xf8, 0x07, 0x10, 0x0e, 0x00, 0x1c, 0x00, 0x18, 0x00, 0x18,
    0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

void renderPartyMenu(uint8_t selectedIndex) {
  display.clearBuffer();
  ui.renderStatusBar(appState);
  display.setDrawColor(0);
  display.drawBox(0, kMenuTop, kDisplayWidth, kMenuHeight);
  display.setDrawColor(1);

  const uint8_t itemHeight = kMenuHeight / 2;
  const uint8_t itemCount = sizeof(kPartyMenuItems) / sizeof(kPartyMenuItems[0]);
  const uint8_t visibleCount = 2;
  uint8_t firstIndex = 0;
  if (selectedIndex >= visibleCount) {
    firstIndex = selectedIndex - (visibleCount - 1);
  }
  if (firstIndex + visibleCount > itemCount) {
    firstIndex = itemCount - visibleCount;
  }

  for (uint8_t i = 0; i < visibleCount; ++i) {
    const uint8_t itemIndex = firstIndex + i;
    if (itemIndex >= itemCount) {
      break;
    }
    const uint8_t rowTop = kMenuTop + i * itemHeight;
    if (itemIndex == selectedIndex) {
      display.setDrawColor(1);
      display.drawFrame(0, rowTop, kDisplayWidth, itemHeight);
    }

    display.setDrawColor(1);
    display.setFont(u8g2_font_6x10_tf);
    const uint8_t textX = 36;
    const int16_t ascent = display.getAscent();
    const int16_t descent = display.getDescent();
    const int16_t textHeight = ascent - descent;
    const int16_t textY = rowTop + (itemHeight - textHeight) / 2 + ascent;
    display.setCursor(textX, static_cast<uint8_t>(textY));
    display.print(kPartyMenuItems[itemIndex]);

    const uint8_t iconX = 8;
    if (itemIndex == 0) {
      const uint8_t iconY = rowTop + (itemHeight - kIcon16) / 2;
      display.drawXBMP(iconX, iconY, kIcon16, kIcon16, kIconReturn);
    } else if (itemIndex == kPartyDuelIndex) {
      const uint8_t iconY = rowTop + (itemHeight - kIcon16) / 2;
      display.drawXBMP(iconX, iconY, kIcon16, kIcon16, kIconDuel);
    } else if (itemIndex == kPartyRouletteIndex) {
      const uint8_t iconY = rowTop + (itemHeight - kIcon16) / 2;
      display.drawXBMP(iconX, iconY, kIcon16, kIcon16, kIconRoulette);
    }

    if (itemIndex == kPartyDuelIndex || itemIndex == kPartyRouletteIndex) {
      const uint8_t boxSize = 10;
      const uint8_t boxX = kDisplayWidth - 16;
      const uint8_t boxY = rowTop + (itemHeight - boxSize) / 2;
      display.drawFrame(boxX, boxY, boxSize, boxSize);
      if (itemIndex == kPartyDuelIndex && duelEnabled) {
        display.drawBox(boxX + 2, boxY + 2, boxSize - 4, boxSize - 4);
      } else if (itemIndex == kPartyRouletteIndex && rouletteEnabled) {
        display.drawBox(boxX + 2, boxY + 2, boxSize - 4, boxSize - 4);
      }
    }
  }

  display.sendBuffer();
}

void renderSubmenuWithReturnIcon(const char *const *menuItems, size_t menuCount,
                                 uint8_t selectedIndex) {
  display.clearBuffer();
  ui.renderStatusBar(appState);
  display.setDrawColor(0);
  display.drawBox(0, kMenuTop, kDisplayWidth, kMenuHeight);
  display.setDrawColor(1);

  const uint8_t itemHeight = kMenuHeight / 2;
  const uint8_t totalItems = static_cast<uint8_t>(menuCount);
  const uint8_t visibleCount = 2;
  uint8_t firstIndex = 0;
  if (selectedIndex >= visibleCount) {
    firstIndex = selectedIndex - (visibleCount - 1);
  }
  if (firstIndex + visibleCount > totalItems) {
    firstIndex = totalItems - visibleCount;
  }

  for (uint8_t i = 0; i < visibleCount; ++i) {
    const uint8_t itemIndex = firstIndex + i;
    if (itemIndex >= totalItems) {
      break;
    }
    const uint8_t rowTop = kMenuTop + i * itemHeight;
    if (itemIndex == selectedIndex) {
      display.setDrawColor(1);
      display.drawFrame(0, rowTop, kDisplayWidth, itemHeight);
    }

    display.setDrawColor(1);
    display.setFont(u8g2_font_6x10_tf);
    const uint8_t textX = 36;
    const int16_t ascent = display.getAscent();
    const int16_t descent = display.getDescent();
    const int16_t textHeight = ascent - descent;
    const int16_t textY = rowTop + (itemHeight - textHeight) / 2 + ascent;
    display.setCursor(textX, static_cast<uint8_t>(textY));
    display.print(menuItems[itemIndex]);

    if (itemIndex == 0) {
      const uint8_t iconX = 8;
      const uint8_t iconY = rowTop + (itemHeight - kIcon16) / 2;
      display.drawXBMP(iconX, iconY, kIcon16, kIcon16, kIconReturn);
    }
  }

  display.sendBuffer();
}

void renderMainMenuWithMeasureIcon(uint8_t selectedIndex) {
  display.clearBuffer();
  ui.renderStatusBar(appState);
  display.setDrawColor(0);
  display.drawBox(0, kMenuTop, kDisplayWidth, kMenuHeight);
  display.setDrawColor(1);

  const uint8_t itemHeight = kMenuHeight / 2;
  const uint8_t itemCount = sizeof(kMenuItems) / sizeof(kMenuItems[0]);
  const uint8_t visibleCount = 2;
  uint8_t firstIndex = 0;
  if (selectedIndex >= visibleCount) {
    firstIndex = selectedIndex - (visibleCount - 1);
  }
  if (firstIndex + visibleCount > itemCount) {
    firstIndex = itemCount - visibleCount;
  }

  for (uint8_t i = 0; i < visibleCount; ++i) {
    const uint8_t itemIndex = firstIndex + i;
    if (itemIndex >= itemCount) {
      break;
    }
    const uint8_t rowTop = kMenuTop + i * itemHeight;
    if (itemIndex == selectedIndex) {
      display.setDrawColor(1);
      display.drawFrame(0, rowTop, kDisplayWidth, itemHeight);
    }

    display.setDrawColor(1);
    display.setFont(u8g2_font_6x10_tf);
    const uint8_t textX = 36;
    const int16_t ascent = display.getAscent();
    const int16_t descent = display.getDescent();
    const int16_t textHeight = ascent - descent;
    const int16_t textY = rowTop + (itemHeight - textHeight) / 2 + ascent;
    display.setCursor(textX, static_cast<uint8_t>(textY));
    display.print(kMenuItems[itemIndex]);

    const uint8_t iconX = 8;
    if (itemIndex == 0) {
      const uint8_t iconY = rowTop + (itemHeight - kIcon16) / 2;
      display.drawXBMP(iconX, iconY, kIcon16, kIcon16, kIconMeasure);
    } else if (itemIndex == 1) {
      const uint8_t iconY = rowTop + (itemHeight - kIcon16) / 2;
      display.drawXBMP(iconX, iconY, kIcon16, kIcon16, kIconHistory);
    } else if (itemIndex == 2) {
      const uint8_t iconY = rowTop + (itemHeight - kIcon16) / 2;
      display.drawXBMP(iconX, iconY, kIcon16, kIcon16, kIconParty);
    } else if (itemIndex == kSleepMenuIndex) {
      const uint8_t iconY = rowTop + (itemHeight - kIcon15) / 2 - 1;
      display.drawXBMP(iconX, iconY, kIcon15, kIcon15, kIconSleep);
    } else if (itemIndex == kSettingsMenuIndex) {
      const uint8_t iconY = rowTop + (itemHeight - kIcon16) / 2;
      display.drawXBMP(iconX, iconY, kIcon16, kIcon16, kIconSettings);
    } else if (itemIndex == kAboutMenuIndex) {
      const uint8_t iconY = rowTop + (itemHeight - kIcon16) / 2;
      display.drawXBMP(iconX, iconY, kIcon16, kIcon16, kIconAbout);
    }
  }

  display.sendBuffer();
}
void applyDefaultSettings() {
  settingValues[0] = 5;
  settingValues[1] = 5;
  settingValues[2] = 5;
  settingSensValue = 10;
  appState.connection = ConnectionMode::Wifi;
  sleepTimeoutSec = 300;
}

void saveSettings() {
  JsonDocument doc;
  doc["led"] = settingValues[0];
  doc["buzz"] = settingValues[1];
  doc["vib"] = settingValues[2];
  doc["sens"] = settingSensValue;
  doc["sleepTimeout"] = sleepTimeoutSec;
  doc["connection"] = appState.connection == ConnectionMode::Wifi ? "wifi" : "ble";

  File file = LittleFS.open("/settings.json", "w");
  if (!file) {
    return;
  }
  serializeJson(doc, file);
  file.close();
}

bool loadSettings() {
  if (!LittleFS.exists("/settings.json")) {
    return false;
  }
  File file = LittleFS.open("/settings.json", "r");
  if (!file) {
    return false;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    return false;
  }

  settingValues[0] = doc["led"] | settingValues[0];
  settingValues[1] = doc["buzz"] | settingValues[1];
  settingValues[2] = doc["vib"] | settingValues[2];
  settingSensValue = doc["sens"] | settingSensValue;
  sleepTimeoutSec = doc["sleepTimeout"] | sleepTimeoutSec;
  if (sleepTimeoutSec > kSleepTimeoutMaxSec) {
    sleepTimeoutSec = kSleepTimeoutMaxSec;
  }
  const char *conn = doc["connection"] | "ble";
  if (strcmp(conn, "wifi") == 0) {
    appState.connection = ConnectionMode::Wifi;
  } else {
    appState.connection = ConnectionMode::BLE;
  }
  return true;
}

void saveUserMeasurement(const char *userId, float measurement, time_t timestampSec) {
  if (userId == nullptr || userId[0] == '\0') {
    return;
  }

  JsonDocument doc;
  if (LittleFS.exists(kUsersFile)) {
    File file = LittleFS.open(kUsersFile, "r");
    if (file) {
      deserializeJson(doc, file);
      file.close();
    }
  }

  JsonObject users = doc["users"].is<JsonObject>() ? doc["users"].as<JsonObject>()
                                                   : doc["users"].to<JsonObject>();
  JsonObject user = users[userId].to<JsonObject>();
  if (!user["color"].is<const char *>()) {
    user["color"] = "";
  }
  if (!user["streak"].is<int>()) {
    user["streak"] = 0;
  }
  if (!user["roulette"].is<bool>()) {
    user["roulette"] = 0;
  }
  if (!user["duel"].is<bool>()) {
    user["duel"] = 0;
  }
  user["measurement"] = measurement;
  user["timestamp"] = timestampSec;

  File outFile = LittleFS.open(kUsersFile, "w");
  if (!outFile) {
    return;
  }
  serializeJson(doc, outFile);
  outFile.close();
}

bool loadUsers(JsonDocument &doc) {
  if (!LittleFS.exists(kUsersFile)) {
    return false;
  }
  File file = LittleFS.open(kUsersFile, "r");
  if (!file) {
    return false;
  }
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    return false;
  }
  return doc["users"].is<JsonObject>();
}

size_t collectUserIds(const JsonObject &users, String *ids, size_t maxCount) {
  size_t count = 0;
  for (JsonPair kv : users) {
    if (count >= maxCount) {
      break;
    }
    ids[count++] = String(kv.key().c_str());
  }
  return count;
}

size_t collectEligibleUserIds(const JsonObject &users, const char *flagKey, String *ids,
                              size_t maxCount) {
  size_t count = 0;
  for (JsonPair kv : users) {
    if (count >= maxCount) {
      break;
    }
    JsonObject user = kv.value().as<JsonObject>();
    const bool enabled = user[flagKey] | false;
    if (enabled) {
      ids[count++] = String(kv.key().c_str());
    }
  }
  return count;
}

bool userFlagEnabled(const JsonObject &users, const String &userId, const char *flagKey) {
  JsonObject user = users[userId].as<JsonObject>();
  if (!user) {
    return false;
  }
  return user[flagKey] | false;
}

String pickWeightedUser(const String *ids, size_t count, const DuelMode &mode,
                        const String &exclude) {
  uint16_t total = 0;
  for (size_t i = 0; i < count; ++i) {
    if (ids[i] == exclude) {
      continue;
    }
    total = static_cast<uint16_t>(total + mode.weightFor(ids[i]));
  }
  if (total == 0) {
    return String();
  }
  uint16_t roll = static_cast<uint16_t>(random(total));
  for (size_t i = 0; i < count; ++i) {
    if (ids[i] == exclude) {
      continue;
    }
    uint8_t weight = mode.weightFor(ids[i]);
    if (roll < weight) {
      return ids[i];
    }
    roll = static_cast<uint16_t>(roll - weight);
  }
  return String();
}

float recentMeasurement(const JsonObject &users, const String &userId, time_t now) {
  JsonObject user = users[userId].as<JsonObject>();
  if (!user) {
    return 0.0f;
  }
  const time_t ts = user["timestamp"] | 0;
  if (ts == 0 || now < ts || (now - ts) > 15 * 60) {
    return 0.0f;
  }
  return user["measurement"] | 0.0f;
}

void startResetCountdown(uint32_t nowMs) {
  resetPending = true;
  resetStartMs = nowMs;
}

void cancelResetCountdown() {
  resetPending = false;
}

void renderResetCountdown(uint32_t nowMs) {
  uint32_t remainingMs = 0;
  if (nowMs < resetStartMs + kResetCountdownMs) {
    remainingMs = (resetStartMs + kResetCountdownMs) - nowMs;
  }
  const uint8_t secondsLeft = static_cast<uint8_t>((remainingMs + 999) / 1000);

  display.clearBuffer();
  ui.renderStatusBar(appState);
  display.setDrawColor(0);
  display.drawBox(0, kMenuTop, kDisplayWidth, kMenuHeight);
  display.setDrawColor(1);

  display.setFont(u8g2_font_9x15_tf);
  display.setCursor(20, kMenuTop + 20);
  display.print("Reset in:");

  char buffer[8];
  snprintf(buffer, sizeof(buffer), "%u", secondsLeft);
  display.setFont(u8g2_font_6x10_tf);
  const uint8_t countWidth = display.getStrWidth(buffer);
  const uint8_t countX = (kDisplayWidth - countWidth) / 2;
  display.setCursor(countX, kMenuTop + 38);
  display.print(buffer);
  display.sendBuffer();
}

void renderWifiBleMenu(uint8_t selectedIndex) {
  display.clearBuffer();
  ui.renderStatusBar(appState);
  display.setDrawColor(0);
  display.drawBox(0, kMenuTop, kDisplayWidth, kMenuHeight);
  display.setDrawColor(1);

  const uint8_t itemHeight = kMenuHeight / 2;
  const uint8_t itemCount = sizeof(kWifiBleMenuItems) / sizeof(kWifiBleMenuItems[0]);
  const uint8_t visibleCount = 2;
  uint8_t firstIndex = 0;
  if (selectedIndex >= visibleCount) {
    firstIndex = selectedIndex - (visibleCount - 1);
  }
  if (firstIndex + visibleCount > itemCount) {
    firstIndex = itemCount - visibleCount;
  }

  for (uint8_t i = 0; i < visibleCount; ++i) {
    const uint8_t itemIndex = firstIndex + i;
    if (itemIndex >= itemCount) {
      break;
    }
    const uint8_t rowTop = kMenuTop + i * itemHeight;
    const bool selected = itemIndex == selectedIndex;
    if (selected) {
      display.setDrawColor(1);
      display.drawFrame(0, rowTop, kDisplayWidth, itemHeight);
    }

    display.setDrawColor(1);
    display.setFont(u8g2_font_6x10_tf);
    const uint8_t textX = 36;
    const int16_t ascent = display.getAscent();
    const int16_t descent = display.getDescent();
    const int16_t textHeight = ascent - descent;
    const int16_t textY = rowTop + (itemHeight - textHeight) / 2 + ascent;
    display.setCursor(textX, static_cast<uint8_t>(textY));
    display.print(kWifiBleMenuItems[itemIndex]);

    if (itemIndex == 0) {
      const uint8_t iconX = 8;
      const uint8_t iconY = rowTop + (itemHeight - kIcon16) / 2;
      display.drawXBMP(iconX, iconY, kIcon16, kIcon16, kIconReturn);
    } else {
      const bool active =
          (itemIndex == 1 && appState.connection == ConnectionMode::BLE) ||
          (itemIndex == 2 && appState.connection == ConnectionMode::Wifi);
      const uint8_t boxSize = 10;
      const uint8_t boxX = kDisplayWidth - 14;
      const uint8_t boxY = rowTop + (itemHeight - boxSize) / 2;
      display.setDrawColor(1);
      display.drawFrame(boxX, boxY, boxSize, boxSize);
      if (active) {
        display.drawBox(boxX + 2, boxY + 2, boxSize - 4, boxSize - 4);
      }
    }
  }

  display.setDrawColor(1);
  display.sendBuffer();
}

const char *settingLabel(uint8_t slot) {
  switch (slot) {
    case 0:
      return "LED";
    case 1:
      return "Buzzer";
    case 2:
      return "Vibration";
    case 3:
      return "Sensor";
    case 4:
      return "OFF Timer";
    default:
      return "";
  }
}

uint8_t settingMaxValue(uint8_t slot) {
  return slot == 3 ? 25 : 10;
}

void renderSettingAdjust(uint8_t slot, uint16_t value) {
  const uint16_t maxValue = slot == 4 ? kSleepTimeoutMaxSec : settingMaxValue(slot);
  display.clearBuffer();
  ui.renderStatusBar(appState);
  display.setDrawColor(0);
  display.drawBox(0, kMenuTop, kDisplayWidth, kMenuHeight);
  display.setDrawColor(1);

  display.setFont(u8g2_font_9x15_tf);
  display.setCursor(10, kMenuTop + 20);
  display.print(settingLabel(slot));

  char buffer[12];
  if (slot == 4) {
    if (value == 0) {
      snprintf(buffer, sizeof(buffer), "OFF");
    } else if (value == 30) {
      snprintf(buffer, sizeof(buffer), "%us", static_cast<unsigned int>(value));
    } else {
      const unsigned int minutes = value / 60;
      const unsigned int seconds = value % 60;
      snprintf(buffer, sizeof(buffer), "%um %us", minutes, seconds);
    }
  } else {
    snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned int>(value));
  }
  display.setFont(u8g2_font_6x10_tf);
  const uint8_t barX = kDisplayWidth - 6;
  const uint8_t barY = kMenuTop + 3;
  const uint8_t barW = 4;
  const uint8_t barH = kMenuHeight - 6;
  display.drawFrame(barX, barY, barW, barH);

  const uint8_t valueBoxW = slot == 4 ? 48 : 28;
  const uint8_t valueBoxH = 18;
  const uint8_t valueBoxX = static_cast<uint8_t>(barX - 6 - valueBoxW);
  const uint8_t valueBoxY = kMenuTop + 30;
  display.drawFrame(valueBoxX, valueBoxY, valueBoxW, valueBoxH);
  const uint8_t valueWidth = display.getStrWidth(buffer);
  const uint8_t valueX = valueBoxX + (valueBoxW - valueWidth) / 2;
  display.setCursor(valueX, valueBoxY + 12);
  display.print(buffer);

  if (value > 0) {
    const uint8_t fillH =
        static_cast<uint8_t>((static_cast<uint32_t>(value) * (barH - 2)) / maxValue);
    const uint8_t fillY = static_cast<uint8_t>(barY + barH - 1 - fillH);
    display.drawBox(barX + 1, fillY, barW - 2, fillH);
  }

  display.sendBuffer();
}

void startSleepCountdown(uint32_t nowMs) {
  sleepPending = true;
  sleepStartMs = nowMs;
}

void cancelSleepCountdown() {
  sleepPending = false;
}

void renderSleepCountdown(uint32_t nowMs) {
  uint32_t remainingMs = 0;
  if (nowMs < sleepStartMs + kSleepCountdownMs) {
    remainingMs = (sleepStartMs + kSleepCountdownMs) - nowMs;
  }
  const uint8_t secondsLeft = static_cast<uint8_t>((remainingMs + 999) / 1000);

  display.clearBuffer();
  ui.renderStatusBar(appState);
  display.setDrawColor(0);
  display.drawBox(0, kMenuTop, kDisplayWidth, kMenuHeight);
  display.setDrawColor(1);

  display.setFont(u8g2_font_9x15_tf);
  display.setCursor(30, kMenuTop + 20);
  display.print("Sleep in:");

  char buffer[8];
  snprintf(buffer, sizeof(buffer), "%u", secondsLeft);
  display.setFont(u8g2_font_6x10_tf);
  const uint8_t countWidth = display.getStrWidth(buffer);
  const uint8_t countX = (kDisplayWidth - countWidth) / 2;
  display.setCursor(countX, kMenuTop + 38);
  display.print(buffer);
  display.sendBuffer();
}

void enterDeepSleep() {
  display.clearBuffer();
  display.sendBuffer();
  // Wait for button release so sleep doesn't instantly wake.
  while (digitalRead(PIN_BUTTON) == LOW) {
    delay(10);
  }
  delay(50);
  // Note: ESP32-C3 deep sleep wake only works on GPIO0-5.
  const gpio_num_t wakeGpio = static_cast<gpio_num_t>(PIN_BUTTON);
  gpio_pullup_en(wakeGpio);
  gpio_pulldown_dis(wakeGpio);
  const uint64_t wakeMask = (1ULL << static_cast<uint64_t>(wakeGpio));
  esp_deep_sleep_enable_gpio_wakeup(wakeMask, ESP_GPIO_WAKEUP_GPIO_LOW);
  if (duelEnabled) {
    const time_t now = time(nullptr);
    if (now > 0 && duel.nextTime() > now) {
      const uint64_t deltaUs =
          static_cast<uint64_t>(duel.nextTime() - now) * 1000000ULL;
      esp_sleep_enable_timer_wakeup(deltaUs);
    }
  }
  esp_deep_sleep_start();
}

void updateButton(uint32_t nowMs, bool &shortPress, bool &longPress, bool &extraLongPress) {
  shortPress = false;
  longPress = false;
  extraLongPress = false;

  const bool level = digitalRead(PIN_BUTTON) == LOW;
  if (level && !button.lastLevel) {
    button.pressedMs = nowMs;
    button.longReported = false;
    button.extraReported = false;
  }

  if (!level && button.lastLevel) {
    const uint32_t heldMs = nowMs - button.pressedMs;
    if (!button.longReported && heldMs > kButtonDebounceMs) {
      button.pendingShort = true;
    }
  }

  if (level) {
    const uint32_t heldMs = nowMs - button.pressedMs;
    if (heldMs >= kButtonExtraLongMs && !button.extraReported) {
      extraLongPress = true;
      button.extraReported = true;
      button.pendingShort = false;
    } else if (heldMs >= kButtonLongPressMs && !button.longReported) {
      longPress = true;
      button.longReported = true;
      button.pendingShort = false;
    }
  }

  if (button.pendingShort) {
    shortPress = true;
    button.pendingShort = false;
  }

  button.lastLevel = level;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_FAULT_BUTTON, INPUT_PULLUP);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  scanI2c();

  display.begin();
  display.clearBuffer();
  delay(1500);
  ui.begin();

  appState.batteryPercent = 78;
  appState.charging = true;
  appState.connection = ConnectionMode::BLE;
  appState.lastUser = "Test";
  appState.lastMeasurement = 0.42f;
  mainMenuIndex = 0;
  submenuIndex = 0;
  inSubmenu = false;
  inWifiBleMenu = false;
  appState.menuIndex = 0;

  if (!LittleFS.begin()) {
    LittleFS.begin(true);
  }
  if (!loadSettings()) {
    applyDefaultSettings();
    saveSettings();
  }
  roulette.reset();
  duel.disable();
  randomSeed(micros());
  lastInteractionMs = millis();
}

void loop() {
  const uint32_t nowMs = millis();
  bool shortPress = false;
  bool longPress = false;
  bool extraLongPress = false;
  updateButton(nowMs, shortPress, longPress, extraLongPress);
  if (shortPress || longPress || extraLongPress) {
    lastInteractionMs = nowMs;
  }

  if (sleepPending) {
    if (shortPress || longPress || extraLongPress) {
      cancelSleepCountdown();
    } else if (nowMs - sleepStartMs >= kSleepCountdownMs) {
      enterDeepSleep();
    } else {
      renderSleepCountdown(nowMs);
    }
    return;
  }
  if (sleepTimeoutSec > 0 &&
      nowMs - lastInteractionMs >= static_cast<uint32_t>(sleepTimeoutSec) * 1000UL &&
      !measuringActive && roulettePhase == RoulettePhase::Idle && duelPhase == DuelPhase::Idle &&
      !sleepPending && !resetPending) {
    enterDeepSleep();
  }
  if (resetPending) {
    if (shortPress || longPress || extraLongPress) {
      cancelResetCountdown();
    } else if (nowMs - resetStartMs >= kResetCountdownMs) {
      applyDefaultSettings();
      saveSettings();
      resetPending = false;
    } else {
      renderResetCountdown(nowMs);
    }
    return;
  }
  if (duelPhase != DuelPhase::Idle) {
    if (duelPhase == DuelPhase::Versus) {
      renderDuelVersus(duelUserA, duelUserB);
      if (shortPress || longPress || extraLongPress) {
        duelPhase = DuelPhase::FirstStart;
      }
    } else if (duelPhase == DuelPhase::FirstStart) {
      renderDuelPrompt(duelUserA, "Starts");
      if (shortPress || longPress || extraLongPress) {
        duelPhase = DuelPhase::SecondStart;
      }
    } else if (duelPhase == DuelPhase::SecondStart) {
      renderDuelPrompt(duelUserB, "Starts");
      if (shortPress || longPress || extraLongPress) {
        float diff = fabsf(duelMeasA - duelMeasB);
        String loser = (duelMeasA >= duelMeasB) ? duelUserB : duelUserA;
        if (diff < 0.001f) {
          loser = (random(2) == 0) ? duelUserA : duelUserB;
        }
        int sips = static_cast<int>(ceilf(diff * 10.0f));
        if (sips < 1) {
          sips = 1;
        }
        duelMessage = loser + " drinks " + String(sips) + " sips";
        duelPhase = DuelPhase::ShowResult;
        duelPhaseStartMs = nowMs;
      }
    } else {
      renderDuelResult();
      if (shortPress || longPress || extraLongPress ||
          (nowMs - duelPhaseStartMs >= kDuelResultMs)) {
        duelPhase = DuelPhase::Idle;
      }
    }
    return;
  }
  if (roulettePhase != RoulettePhase::Idle) {
    if (roulettePhase == RoulettePhase::ShowPromille) {
      renderRoulettePromille();
      if (nowMs - roulettePhaseStartMs >= kRoulettePromilleMs) {
        roulettePhase = RoulettePhase::ShowResult;
        roulettePhaseStartMs = nowMs;
      }
    } else {
      renderRouletteResult();
      if (shortPress || longPress || extraLongPress ||
          (nowMs - roulettePhaseStartMs >= kRouletteResultMs)) {
        roulettePhase = RoulettePhase::Idle;
      }
    }
    return;
  }
  if (duelEnabled) {
    const time_t now = time(nullptr);
    if (now > 0 && duel.due(now)) {
      JsonDocument doc;
      if (loadUsers(doc)) {
        JsonObject users = doc["users"].as<JsonObject>();
        String ids[20];
        const size_t count = collectEligibleUserIds(users, "duel", ids, 20);
        if (count >= 2) {
          const String userA = pickWeightedUser(ids, count, duel, String());
          const String userB = pickWeightedUser(ids, count, duel, userA);
          if (userA.length() > 0 && userB.length() > 0) {
            duel.rememberPick(userA);
            duel.rememberPick(userB);
            duelUserA = userA;
            duelUserB = userB;
            duelMeasA = recentMeasurement(users, userA, now);
            duelMeasB = recentMeasurement(users, userB, now);
            duelPhase = DuelPhase::Versus;
            duelPhaseStartMs = nowMs;
          }
        }
      }
      duel.scheduleNext(now);
    }
  }

  const char *const *menuItems = kMenuItems;
  size_t menuCount = sizeof(kMenuItems) / sizeof(kMenuItems[0]);
  uint8_t *activeIndex = &mainMenuIndex;

  if (measuringActive) {
    if (rouletteEnabled) {
      const uint32_t elapsedMs = nowMs - measureStartMs;
      if (elapsedMs >= kMeasureCountdownSec * 1000UL && (shortPress || longPress)) {
        bool allowRoulette = true;
        JsonDocument doc;
        if (loadUsers(doc)) {
          JsonObject users = doc["users"].as<JsonObject>();
          if (!userFlagEnabled(users, String(appState.lastUser), "roulette")) {
            allowRoulette = false;
          }
        }
        if (allowRoulette) {
          rouletteLastHit = roulette.fire();
          roulettePhase = RoulettePhase::ShowPromille;
          roulettePhaseStartMs = nowMs;
          measuringActive = false;
          return;
        }
      }
    }
    if (shortPress || longPress) {
      measuringActive = false;
      return;
    }
    renderMeasurement(nowMs);
    if (!extraLongPress) {
      return;
    }
  }
  if (showingQr) {
    if (shortPress || longPress) {
      showingQr = false;
      return;
    }
    renderQrCode();
    if (!extraLongPress) {
      return;
    }
  }
  if (adjustingSetting) {
    if (shortPress) {
      if (adjustingSettingSlot == 3) {
        settingSensValue = static_cast<uint8_t>((settingSensValue + 1) % 26);
      } else if (adjustingSettingSlot == 4) {
        uint16_t next = static_cast<uint16_t>(sleepTimeoutSec + kSleepTimeoutStepSec);
        if (next > kSleepTimeoutMaxSec) {
          next = kSleepTimeoutMinSec;
        }
        sleepTimeoutSec = next;
      } else {
        settingValues[adjustingSettingSlot] =
            static_cast<uint8_t>((settingValues[adjustingSettingSlot] + 1) % 11);
      }
      settingsDirty = true;
    }
    if (longPress) {
      if (settingsDirty) {
        saveSettings();
        settingsDirty = false;
      }
      adjustingSetting = false;
      return;
    }
    const uint16_t currentValue =
        adjustingSettingSlot == 3
            ? settingSensValue
            : (adjustingSettingSlot == 4 ? sleepTimeoutSec
                                         : settingValues[adjustingSettingSlot]);
    renderSettingAdjust(adjustingSettingSlot, currentValue);
    return;
  }
  if (inWifiBleMenu) {
    menuItems = kWifiBleMenuItems;
    menuCount = sizeof(kWifiBleMenuItems) / sizeof(kWifiBleMenuItems[0]);
    activeIndex = &wifiBleMenuIndex;
  } else if (inSubmenu && mainMenuIndex == kPartyMenuIndex) {
    menuItems = kPartyMenuItems;
    menuCount = sizeof(kPartyMenuItems) / sizeof(kPartyMenuItems[0]);
    activeIndex = &submenuIndex;
  } else if (inSubmenu) {
    menuItems = submenuItemsForMain(mainMenuIndex, menuCount);
    activeIndex = &submenuIndex;
  }

  if (shortPress && menuCount > 0) {
    *activeIndex = (*activeIndex + 1) % static_cast<uint8_t>(menuCount);
  }

  if (longPress) {
    if (inWifiBleMenu) {
      if (wifiBleMenuIndex == 0) {
        inWifiBleMenu = false;
        return;
      }
      appState.connection =
          wifiBleMenuIndex == 1 ? ConnectionMode::BLE : ConnectionMode::Wifi;
      saveSettings();
      return;
    }
    if (inSubmenu) {
      if (mainMenuIndex == kPartyMenuIndex && submenuIndex == kPartyDuelIndex) {
        duelEnabled = !duelEnabled;
        if (duelEnabled) {
          const time_t now = time(nullptr);
          if (now > 0) {
            duel.enable(now);
          }
        } else {
          duel.disable();
        }
        return;
      }
      if (mainMenuIndex == kPartyMenuIndex && submenuIndex == kPartyRouletteIndex) {
        rouletteEnabled = !rouletteEnabled;
        roulette.reset();
        return;
      }
      if (mainMenuIndex == kSettingsMenuIndex && submenuIndex == kSettingWifiBleIndex) {
        inWifiBleMenu = true;
        wifiBleMenuIndex = appState.connection == ConnectionMode::Wifi ? 2 : 1;
        return;
      }
      if (mainMenuIndex == kSettingsMenuIndex &&
          submenuIndex >= kSettingLedIndex &&
          submenuIndex <= kSettingVibIndex) {
        adjustingSetting = true;
        adjustingSettingSlot = static_cast<uint8_t>(submenuIndex - kSettingLedIndex);
        return;
      }
      if (mainMenuIndex == kSettingsMenuIndex && submenuIndex == kSettingSensIndex) {
        adjustingSetting = true;
        adjustingSettingSlot = 3;
        return;
      }
      if (mainMenuIndex == kSettingsMenuIndex && submenuIndex == kSettingSleepIndex) {
        adjustingSetting = true;
        adjustingSettingSlot = 4;
        return;
      }
      if (mainMenuIndex == kSettingsMenuIndex && submenuIndex == kSettingResetIndex) {
        startResetCountdown(nowMs);
        return;
      }
      if (submenuIndex == 0) {
        mainMenuIndex = 0;
        submenuIndex = 0;
        inSubmenu = false;
      }
    } else if (mainMenuIndex == kSleepMenuIndex) {
      startSleepCountdown(nowMs);
      return;
    } else if (mainMenuIndex == kAboutMenuIndex) {
      showingQr = true;
      return;
    } else if (mainMenuIndex == 0) {
      measuringActive = true;
      measureStartMs = nowMs;
      return;
    } else if (hasSubmenuForMain(mainMenuIndex)) {
      inSubmenu = true;
      submenuIndex = 0;
    }
  }

  if (inWifiBleMenu) {
    appState.menuIndex = wifiBleMenuIndex;
  } else if (inSubmenu && mainMenuIndex == kPartyMenuIndex) {
    appState.menuIndex = submenuIndex;
  } else {
    appState.menuIndex = inSubmenu ? submenuIndex : mainMenuIndex;
  }

  if (extraLongPress) {
    enterDeepSleep();
  }

  const bool faultLevel = digitalRead(PIN_FAULT_BUTTON) == LOW;
  if (faultLevel != faultButtonLast) {
    faultButtonLastMs = nowMs;
    faultButtonLast = faultLevel;
  } else if (faultLevel && (nowMs - faultButtonLastMs) > kButtonDebounceMs) {
    appState.chargerFaultReason = appState.chargerFaultReason == 0 ? 1 : 0;
    appState.chargerTsFault = appState.chargerTsFault == 2 ? 1 : 2;
    faultButtonLast = false;
  }

  if (nowMs - lastUiTickMs >= kUiTickMs) {
    if (inWifiBleMenu) {
      renderWifiBleMenu(wifiBleMenuIndex);
    } else if (inSubmenu && mainMenuIndex == kPartyMenuIndex) {
      renderPartyMenu(submenuIndex);
    } else if (!inSubmenu) {
      renderMainMenuWithMeasureIcon(mainMenuIndex);
    } else {
      renderSubmenuWithReturnIcon(menuItems, menuCount, submenuIndex);
    }
    lastUiTickMs = nowMs;
  }
}
