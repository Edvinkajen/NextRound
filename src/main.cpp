#include <Arduino.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <LittleFS.h>

#include "display_constants.h"
#include "ui.h"
#include "qrcode_bitmap.h"
#include "pins.h"
#include "actuators.h"
#include "BatteryManager.h"
#include "wifi_server.h"
#include "device_info.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr uint32_t kButtonDebounceMs  = 30;
constexpr uint32_t kButtonLongPressMs = 800;
constexpr uint32_t kButtonExtraLongMs = 7000;
constexpr uint32_t kUiTickMs          = 30;
constexpr uint32_t kSleepCountdownMs  = 3000;
constexpr uint32_t kResetCountdownMs  = 10000;
constexpr uint32_t kWifiCountdownMs   = 3000;
constexpr uint16_t kSleepTimeoutMinSec  = 0;
constexpr uint16_t kSleepTimeoutMaxSec  = 900;
constexpr uint16_t kSleepTimeoutStepSec = 30;
constexpr uint8_t  kPwmChannelBuzzer     = 0;
constexpr uint8_t  kPwmChannelVib        = 1;
constexpr uint32_t kBuzzerPwmHz          = 2700;
constexpr uint32_t kVibPwmHz             = 20000;
constexpr uint16_t kBuzzerTestToneHz     = 2000;
constexpr uint32_t kBuzzerTestDurationMs = 500;
constexpr uint8_t  kDefaultNeopixelR     = 200;
constexpr uint8_t  kDefaultNeopixelG     = 60;
constexpr uint8_t  kDefaultNeopixelB     = 130;

// ---------------------------------------------------------------------------
// Menu data
// ---------------------------------------------------------------------------

const char *const kMainMenuItems[]     = { "Measure", "Turn OFF", "Settings", "About" };
constexpr uint8_t kMainMenuCount       = 4;
constexpr uint8_t kMainMeasureIndex    = 0;
constexpr uint8_t kMainSleepIndex      = 1;
constexpr uint8_t kMainSettingsIndex   = 2;
constexpr uint8_t kMainAboutIndex      = 3;

const char *const kSettingsMenuItems[] = {
  "Return", "Calib", "LED", "Buzzer", "Vibration",
  "Buzzer Test", "WIFI/BLE", "Sensor", "OFF Timer", "FW Update", "Reset"
};
constexpr uint8_t kSettingsMenuCount    = 11;
constexpr uint8_t kSettingCalibIndex    = 1;
constexpr uint8_t kSettingLedIndex      = 2;
constexpr uint8_t kSettingBuzzIndex     = 3;
constexpr uint8_t kSettingVibIndex      = 4;
constexpr uint8_t kSettingBuzzTestIndex = 5;
constexpr uint8_t kSettingWifiBleIndex  = 6;
constexpr uint8_t kSettingSensIndex     = 7;
constexpr uint8_t kSettingSleepIndex    = 8;
constexpr uint8_t kSettingOtaIndex      = 9;
constexpr uint8_t kSettingResetIndex    = 10;

const char *const kWifiBleMenuItems[]  = { "Return", "BLE", "WiFi" };
constexpr uint8_t kWifiBleMenuCount    = 3;

// ---------------------------------------------------------------------------
// Icon bitmaps (16×16 XBM unless noted)
// ---------------------------------------------------------------------------

constexpr uint8_t kIcon16 = 16;
constexpr uint8_t kIcon15 = 15;

const uint8_t kIconCalib[] = {
  0x00,0x00,0x80,0x01,0x80,0x01,0x80,0x01,0x80,0x01,0x00,0x00,0x00,0x00,
  0x9e,0x79,0x9e,0x79,0x00,0x00,0x00,0x00,0x80,0x01,0x80,0x01,0x80,0x01,
  0x80,0x01,0x00,0x00
};
const uint8_t kIconSensor[] = {
  0x00,0x00,0x02,0x00,0x07,0x00,0x02,0x00,0x02,0x00,0x02,0x00,0x02,0x00,
  0xaa,0x2a,0x02,0x00,0x82,0x07,0xc2,0x00,0x72,0x00,0x1e,0x00,0x06,0x20,
  0xfe,0x7f,0x00,0x20
};
const uint8_t kIconWifiBle[] = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x08,0x08,0x10,0x24,0x24,0x14,0x28,
  0x94,0x29,0x94,0x29,0x14,0x28,0xa4,0x25,0x88,0x11,0x90,0x09,0x80,0x01,
  0x80,0x01,0x80,0x01
};
const uint8_t kIconOffTimer[] = {
  0x00,0x00,0x00,0x00,0xf0,0x1f,0x08,0x20,0x08,0x20,0x08,0x20,0x10,0x10,
  0x20,0x08,0x40,0x04,0x40,0x05,0x20,0x09,0x90,0x13,0x48,0x25,0xa8,0x2a,
  0x58,0x35,0xf0,0x1f
};
const uint8_t kIconLed[] = {
  0x00,0x00,0x10,0x10,0x22,0x48,0x44,0x24,0x08,0x10,0xe1,0x87,0x12,0x48,
  0x08,0x10,0x08,0x14,0x0b,0xd4,0x08,0x14,0x08,0x14,0x08,0x14,0x08,0x10,
  0x08,0x10,0xfc,0x3f
};
const uint8_t kIconReset[] = {
  0x00,0x00,0x20,0x00,0x30,0x00,0xf8,0x03,0xf8,0x0f,0x30,0x0c,0x20,0x18,
  0x00,0x18,0x0c,0x30,0x0c,0x30,0x18,0x18,0x18,0x18,0x30,0x0c,0xf0,0x0f,
  0xc0,0x03,0x00,0x00
};
const uint8_t kIconBuzzer[] = {
  0x00,0x00,0x30,0x0c,0x38,0x18,0xbc,0x11,0x3f,0x33,0x3f,0x22,0x3f,0x22,
  0x3f,0x22,0x3f,0x22,0x3f,0x22,0x3f,0x22,0x3f,0x33,0xbc,0x11,0x38,0x18,
  0x30,0x0c,0x00,0x00
};
const uint8_t kIconFwUpdate[] = {
  0x80,0x01,0x80,0x01,0x80,0x01,0x80,0x01,0x80,0x01,0x80,0x01,0xe0,0x07,
  0xc0,0x03,0x80,0x01,0x00,0x00,0xf0,0x0f,0x10,0x08,0x90,0x09,0x10,0x08,
  0x10,0x08,0xf0,0x0f
};
const uint8_t kIconVibration[] = {
  0x00,0x00,0x00,0x00,0x0c,0x30,0x06,0x60,0x42,0x06,0x02,0x0c,0x92,0x49,
  0x52,0x4a,0x52,0x4a,0x92,0x49,0x30,0x40,0x60,0x42,0x06,0x60,0x0c,0x30,
  0x00,0x00,0x00,0x00
};
const uint8_t kIconSleep[] = {
  0x00,0x00,0xe0,0x03,0x18,0x0c,0x84,0x10,0x84,0x10,0xa2,0x22,0x92,0x24,
  0x12,0x24,0x12,0x24,0x22,0x22,0xc4,0x11,0x04,0x10,0x18,0x0c,0xe0,0x03,
  0x00,0x00
};
const uint8_t kIconSettings[] = {
  0x00,0x18,0x00,0x1c,0x00,0x0e,0x00,0xc6,0x00,0xe6,0x00,0x7f,0x80,0x3f,
  0xc0,0x07,0xe0,0x03,0xf0,0x01,0xfc,0x00,0x76,0x00,0x22,0x00,0x32,0x00,
  0x1e,0x00,0x00,0x00
};
const uint8_t kIconAbout[] = {
  0xe0,0x07,0x18,0x18,0x04,0x20,0x82,0x41,0x82,0x41,0x01,0x80,0x01,0x80,
  0x81,0x81,0x81,0x81,0x81,0x81,0x81,0x81,0x82,0x41,0x82,0x41,0x04,0x20,
  0x18,0x18,0xe0,0x07
};
const uint8_t kIconReturn[] = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x18,0x00,0xfc,0x03,0xf8,0x07,
  0x10,0x0e,0x00,0x1c,0x00,0x18,0x00,0x18,0x00,0x18,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00
};
const uint8_t kIconMeasure[] = {
  0x00,0x00,0x02,0x00,0x07,0x00,0x82,0x3f,0x42,0x00,0x22,0x00,0x22,0x00,
  0x12,0x00,0x12,0x00,0x12,0x00,0x0a,0x00,0x0a,0x00,0x0a,0x00,0x06,0x20,
  0xfe,0x7f,0x00,0x20
};

// ---------------------------------------------------------------------------
// Hardware objects
// ---------------------------------------------------------------------------

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R2, U8X8_PIN_NONE);
Ui             ui(display);
Buzzer         buzzer(PIN_BUZZER, kPwmChannelBuzzer, kBuzzerPwmHz);
VibrationMotor vib(PIN_VIB,      kPwmChannelVib,    kVibPwmHz);
NeopixelLed    neopixel(PIN_LED);
BatteryManager battery;

// Sends the display buffer over I2C while holding the shared Wire mutex so
// the BMS monitor task cannot interleave its I2C reads mid-transfer.
static inline void sendDisplay() {
  xSemaphoreTake(battery.wireMutex(), portMAX_DELAY);
  display.sendBuffer();
  xSemaphoreGive(battery.wireMutex());
}

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------

struct ButtonState {
  bool     lastLevel     = false;
  uint32_t pressedMs     = 0;
  bool     longReported  = false;
  bool     extraReported = false;
  bool     pendingShort  = false;
};

struct AppContext {
  struct Menu {
    uint8_t mainIndex     = 0;
    uint8_t subIndex      = 0;
    bool    inSubmenu     = false;
    bool    inWifiBleMenu = false;
    uint8_t wifiBleIndex  = 0;
    bool    showingQr     = false;
    bool    adjusting     = false;
    uint8_t settingSlot   = 0;
    bool    settingDirty  = false;
  } menu;

  struct Device {
    uint8_t  batteryPercent     = 0;
    bool     charging           = false;
    bool     plugged            = false;
    uint8_t  chargerFaultReason = 0;
    uint8_t  chargerTsFault     = 0;
  } device;

  struct Settings {
    uint8_t        ledLevel        = 5;
    uint8_t        buzzerLevel     = 5;
    uint8_t        vibLevel        = 5;
    uint8_t        sensorSens      = 10;
    uint16_t       sleepTimeoutSec = 300;
    ConnectionMode connection      = ConnectionMode::Wifi;
  } settings;

  struct BuzzerTest {
    bool     active    = false;
    uint32_t endMs     = 0;
    uint8_t  prevLevel = 0;
  } buzzerTest;

  struct Countdown {
    bool     sleepPending = false;
    uint32_t sleepStartMs = 0;
    bool     resetPending = false;
    uint32_t resetStartMs = 0;
    bool     wifiPending  = false;
    uint32_t wifiStartMs  = 0;
  } countdown;

  bool     fwUpdatePending   = false;
  char     fwPrevVersion[16] = {};
  bool     measureActive     = false;
  bool     calibStubActive   = false;
  float    lastMeasurement   = 0.0f;
  uint32_t lastInteractionMs = 0;
  uint32_t lastUiTickMs      = 0;
};

static AppContext  ctx;
static ButtonState button;
AppState           appState;

// ---------------------------------------------------------------------------
// Sync UI presentation state from AppContext
// ---------------------------------------------------------------------------

static void syncAppState() {
  appState.batteryPercent     = ctx.device.batteryPercent;
  appState.charging           = ctx.device.charging;
  appState.connection         = ctx.settings.connection;
  appState.lastMeasurement    = ctx.lastMeasurement;
  appState.lastUser           = "Me";
  appState.chargerFaultReason = ctx.device.chargerFaultReason;
  appState.chargerTsFault     = ctx.device.chargerTsFault;
  appState.menuIndex          = ctx.menu.inSubmenu ? ctx.menu.subIndex : ctx.menu.mainIndex;
}

// ---------------------------------------------------------------------------
// Settings: persist to / restore from LittleFS
// ---------------------------------------------------------------------------

static const char kSettingsFile[] = "/settings.json";
static const char kFwInfoFile[]   = "/fw_info.txt";

static void applyDefaultSettings() {
  ctx.settings.ledLevel        = 5;
  ctx.settings.buzzerLevel     = 5;
  ctx.settings.vibLevel        = 5;
  ctx.settings.sensorSens      = 10;
  ctx.settings.sleepTimeoutSec = 300;
  ctx.settings.connection      = ConnectionMode::Wifi;
}

static void applyActuatorLevels() {
  neopixel.setLevel(ctx.settings.ledLevel);
  buzzer.setLevel(ctx.settings.buzzerLevel);
  vib.setLevel(ctx.settings.vibLevel);
}

static void saveSettings() {
  JsonDocument doc;
  doc["led"]          = ctx.settings.ledLevel;
  doc["buzz"]         = ctx.settings.buzzerLevel;
  doc["vib"]          = ctx.settings.vibLevel;
  doc["sens"]         = ctx.settings.sensorSens;
  doc["sleepTimeout"] = ctx.settings.sleepTimeoutSec;
  doc["connection"]   = ctx.settings.connection == ConnectionMode::Wifi ? "wifi" : "ble";
  File f = LittleFS.open(kSettingsFile, "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

static bool loadSettings() {
  if (!LittleFS.exists(kSettingsFile)) return false;
  File f = LittleFS.open(kSettingsFile, "r");
  if (!f) return false;
  JsonDocument doc;
  const bool ok = (deserializeJson(doc, f) == DeserializationError::Ok);
  f.close();
  if (!ok) return false;
  ctx.settings.ledLevel        = doc["led"]          | ctx.settings.ledLevel;
  ctx.settings.buzzerLevel     = doc["buzz"]         | ctx.settings.buzzerLevel;
  ctx.settings.vibLevel        = doc["vib"]          | ctx.settings.vibLevel;
  ctx.settings.sensorSens      = doc["sens"]         | ctx.settings.sensorSens;
  ctx.settings.sleepTimeoutSec = doc["sleepTimeout"] | ctx.settings.sleepTimeoutSec;
  if (ctx.settings.sleepTimeoutSec > kSleepTimeoutMaxSec)
    ctx.settings.sleepTimeoutSec = kSleepTimeoutMaxSec;
  const char *conn = doc["connection"] | "wifi";
  ctx.settings.connection = (strcmp(conn, "wifi") == 0) ? ConnectionMode::Wifi : ConnectionMode::BLE;
  return true;
}

// ---------------------------------------------------------------------------
// Firmware version tracking (OTA update detection)
// ---------------------------------------------------------------------------

static void checkFirmwareVersion() {
  if (!LittleFS.exists(kFwInfoFile)) {
    File f = LittleFS.open(kFwInfoFile, "w");
    if (f) { f.print(kFirmwareVersion); f.close(); }
    return;
  }
  File f = LittleFS.open(kFwInfoFile, "r");
  if (!f) return;
  char stored[16] = "";
  const size_t n = f.readBytesUntil('\n', stored, sizeof(stored) - 1);
  stored[n] = '\0';
  f.close();
  if (n > 0 && strcmp(stored, kFirmwareVersion) != 0) {
    strncpy(ctx.fwPrevVersion, stored, sizeof(ctx.fwPrevVersion) - 1);
    ctx.fwUpdatePending = true;
  }
}

static void acknowledgeNewFirmware() {
  File f = LittleFS.open(kFwInfoFile, "w");
  if (f) { f.print(kFirmwareVersion); f.close(); }
  ctx.fwUpdatePending = false;
}

// ---------------------------------------------------------------------------
// Rendering helpers
// ---------------------------------------------------------------------------

static void clearContentArea() {
  display.setDrawColor(0);
  display.drawBox(0, kMenuTop, kDisplayWidth, kMenuHeight);
  display.setDrawColor(1);
}

// ---------------------------------------------------------------------------
// Rendering: main menu
// ---------------------------------------------------------------------------

static void renderMainMenu() {
  display.clearBuffer();
  ui.renderStatusBar(appState);
  clearContentArea();

  const uint8_t itemH   = kMenuHeight / 2;
  const uint8_t sel     = ctx.menu.mainIndex;
  const uint8_t visible = 2;
  uint8_t first = 0;
  if (sel >= visible) first = sel - (visible - 1);
  if (first + visible > kMainMenuCount) first = kMainMenuCount - visible;

  display.setFont(u8g2_font_6x10_tf);
  const int16_t asc  = display.getAscent();
  const int16_t desc = display.getDescent();

  for (uint8_t i = 0; i < visible; ++i) {
    const uint8_t idx    = first + i;
    if (idx >= kMainMenuCount) break;
    const uint8_t rowTop = kMenuTop + i * itemH;
    if (idx == sel) {
      display.setDrawColor(1);
      display.drawFrame(0, rowTop, kDisplayWidth, itemH);
    }
    display.setDrawColor(1);
    const int16_t textY = rowTop + (itemH - (asc - desc)) / 2 + asc;
    display.setCursor(36, static_cast<uint8_t>(textY));
    display.print(kMainMenuItems[idx]);

    const uint8_t iconY = rowTop + (itemH - kIcon16) / 2;
    switch (idx) {
      case kMainMeasureIndex:
        display.drawXBMP(8, iconY, kIcon16, kIcon16, kIconMeasure);
        break;
      case kMainSleepIndex:
        display.drawXBMP(8, rowTop + (itemH - kIcon15)/2 - 1, kIcon15, kIcon15, kIconSleep);
        break;
      case kMainSettingsIndex:
        display.drawXBMP(8, iconY, kIcon16, kIcon16, kIconSettings);
        break;
      case kMainAboutIndex:
        display.drawXBMP(8, iconY, kIcon16, kIcon16, kIconAbout);
        break;
    }
  }
  sendDisplay();
}

// ---------------------------------------------------------------------------
// Rendering: settings submenu
// Slot mapping for adjustments: 0=LED, 1=Buzzer, 2=Vibration, 3=OFF Timer
// ---------------------------------------------------------------------------

static const uint8_t *kSettingIconTable[kSettingsMenuCount] = {
  kIconReturn, kIconCalib, kIconLed, kIconBuzzer, kIconVibration,
  kIconBuzzer, kIconWifiBle, kIconSensor, kIconOffTimer, kIconFwUpdate, kIconReset
};

static void renderSettingsMenu() {
  display.clearBuffer();
  ui.renderStatusBar(appState);
  clearContentArea();

  const uint8_t itemH   = kMenuHeight / 2;
  const uint8_t sel     = ctx.menu.subIndex;
  const uint8_t visible = 2;
  uint8_t first = 0;
  if (sel >= visible) first = sel - (visible - 1);
  if (first + visible > kSettingsMenuCount) first = kSettingsMenuCount - visible;

  display.setFont(u8g2_font_6x10_tf);
  const int16_t asc  = display.getAscent();
  const int16_t desc = display.getDescent();

  for (uint8_t i = 0; i < visible; ++i) {
    const uint8_t idx    = first + i;
    const uint8_t rowTop = kMenuTop + i * itemH;
    if (idx == sel) {
      display.setDrawColor(1);
      display.drawFrame(0, rowTop, kDisplayWidth, itemH);
    }
    display.setDrawColor(1);
    const int16_t textY = rowTop + (itemH - (asc - desc)) / 2 + asc;
    display.setCursor(36, static_cast<uint8_t>(textY));
    display.print(kSettingsMenuItems[idx]);
    const uint8_t iconY = rowTop + (itemH - kIcon16) / 2;
    if (kSettingIconTable[idx])
      display.drawXBMP(8, iconY, kIcon16, kIcon16, kSettingIconTable[idx]);
  }
  sendDisplay();
}

// ---------------------------------------------------------------------------
// Rendering: WiFi/BLE submenu
// ---------------------------------------------------------------------------

static void renderWifiBleMenu() {
  display.clearBuffer();
  ui.renderStatusBar(appState);
  clearContentArea();

  const uint8_t itemH   = kMenuHeight / 2;
  const uint8_t sel     = ctx.menu.wifiBleIndex;
  const uint8_t visible = 2;
  uint8_t first = 0;
  if (sel >= visible) first = sel - (visible - 1);
  if (first + visible > kWifiBleMenuCount) first = kWifiBleMenuCount - visible;

  display.setFont(u8g2_font_6x10_tf);
  const int16_t asc  = display.getAscent();
  const int16_t desc = display.getDescent();

  for (uint8_t i = 0; i < visible; ++i) {
    const uint8_t idx    = first + i;
    const uint8_t rowTop = kMenuTop + i * itemH;
    if (idx == sel) {
      display.setDrawColor(1);
      display.drawFrame(0, rowTop, kDisplayWidth, itemH);
    }
    display.setDrawColor(1);
    const int16_t textY = rowTop + (itemH - (asc - desc)) / 2 + asc;
    display.setCursor(36, static_cast<uint8_t>(textY));
    display.print(kWifiBleMenuItems[idx]);
    const uint8_t iconY = rowTop + (itemH - kIcon16) / 2;
    if (idx == 0) {
      display.drawXBMP(8, iconY, kIcon16, kIcon16, kIconReturn);
    } else {
      const bool active = (idx == 1 && ctx.settings.connection == ConnectionMode::BLE) ||
                          (idx == 2 && ctx.settings.connection == ConnectionMode::Wifi);
      constexpr uint8_t kBoxSz = 10;
      const uint8_t boxX = kDisplayWidth - 14;
      const uint8_t boxY = rowTop + (itemH - kBoxSz) / 2;
      display.drawFrame(boxX, boxY, kBoxSz, kBoxSz);
      if (active) display.drawBox(boxX + 2, boxY + 2, kBoxSz - 4, kBoxSz - 4);
    }
  }
  display.setDrawColor(1);
  sendDisplay();
}

// ---------------------------------------------------------------------------
// Rendering: setting value adjustment
// ---------------------------------------------------------------------------

static const char *settingLabel(uint8_t slot) {
  switch (slot) {
    case 0: return "LED";
    case 1: return "Buzzer";
    case 2: return "Vibration";
    case 3: return "Sensor";
    case 4: return "OFF Timer";
    default: return "";
  }
}

static void renderSettingAdjust() {
  const uint8_t  slot  = ctx.menu.settingSlot;
  const uint16_t value = (slot == 3) ? ctx.settings.sensorSens
                       : (slot == 4) ? ctx.settings.sleepTimeoutSec
                       : (slot == 0) ? ctx.settings.ledLevel
                       : (slot == 1) ? ctx.settings.buzzerLevel
                       : ctx.settings.vibLevel;
  const uint16_t maxV  = (slot == 3) ? 25u : (slot == 4) ? kSleepTimeoutMaxSec : 10u;

  display.clearBuffer();
  ui.renderStatusBar(appState);
  clearContentArea();

  display.setFont(u8g2_font_9x15_tf);
  display.setCursor(10, kMenuTop + 20);
  display.print(settingLabel(slot));

  char buf[14];
  if (slot == 4) {
    if (value == 0) {
      snprintf(buf, sizeof(buf), "OFF");
    } else if (value < 60) {
      snprintf(buf, sizeof(buf), "%us", static_cast<unsigned>(value));
    } else {
      const unsigned m = value / 60, s = value % 60;
      snprintf(buf, sizeof(buf), s > 0 ? "%um %us" : "%um", m, s);
    }
  } else {
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(value));
  }

  display.setFont(u8g2_font_6x10_tf);
  const uint8_t barX = kDisplayWidth - 6;
  const uint8_t barY = kMenuTop + 3;
  constexpr uint8_t barW = 4;
  const uint8_t barH = kMenuHeight - 6;
  display.drawFrame(barX, barY, barW, barH);

  const uint8_t vbW = (slot == 4) ? 48 : 28;
  constexpr uint8_t vbH = 18;
  const uint8_t vbX = barX - 6 - vbW;
  const uint8_t vbY = kMenuTop + 30;
  display.drawFrame(vbX, vbY, vbW, vbH);
  const uint8_t vw = display.getStrWidth(buf);
  display.setCursor(vbX + (vbW - vw) / 2, vbY + 12);
  display.print(buf);

  if (value > 0) {
    const uint8_t fillH = static_cast<uint8_t>((static_cast<uint32_t>(value) * (barH - 2)) / maxV);
    const uint8_t fillY = static_cast<uint8_t>(barY + barH - 1 - fillH);
    display.drawBox(barX + 1, fillY, barW - 2, fillH);
  }
  sendDisplay();
}

// ---------------------------------------------------------------------------
// Rendering: About screen (firmware version + HW rev + QR code)
// ---------------------------------------------------------------------------

static void renderAbout() {
  display.clearBuffer();
  display.drawXBMP(kDisplayWidth - kQrCodeWidth, (kDisplayHeight - kQrCodeHeight) / 2,
                   kQrCodeWidth, kQrCodeHeight, kQrCodeBits);
  display.setFont(u8g2_font_5x8_tf);
  display.setCursor(2, 14); display.print(kFirmwareVersion);
  display.setCursor(2, 26); display.print(kHardwareRev);
  sendDisplay();
}

// ---------------------------------------------------------------------------
// Rendering: countdown and notification screens
// ---------------------------------------------------------------------------

static void renderCountdown(const char *title, uint32_t remainingMs) {
  const uint8_t sec = static_cast<uint8_t>((remainingMs + 999) / 1000);
  display.clearBuffer();
  ui.renderStatusBar(appState);
  clearContentArea();
  display.setFont(u8g2_font_9x15_tf);
  display.setCursor((kDisplayWidth - display.getStrWidth(title)) / 2, kMenuTop + 20);
  display.print(title);
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(sec));
  display.setFont(u8g2_font_6x10_tf);
  display.setCursor((kDisplayWidth - display.getStrWidth(buf)) / 2, kMenuTop + 38);
  display.print(buf);
  sendDisplay();
}

static void renderFwUpdateComplete() {
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  display.setCursor((kDisplayWidth - display.getStrWidth("FW Update complete")) / 2, kMenuTop + 22);
  display.print("FW Update complete");
  char ver[40];
  snprintf(ver, sizeof(ver), "V%s -> V%s", ctx.fwPrevVersion, kFirmwareVersion);
  display.setCursor((kDisplayWidth - display.getStrWidth(ver)) / 2, kMenuTop + 38);
  display.print(ver);
  sendDisplay();
}

// ---------------------------------------------------------------------------
// Button reading (short / long / extra-long press detection)
// ---------------------------------------------------------------------------

static void readButton(uint32_t nowMs, bool &shortPress, bool &longPress, bool &extraLong) {
  shortPress = longPress = extraLong = false;
  const bool level = digitalRead(PIN_BUTTON) == LOW;

  if (level && !button.lastLevel) {
    button.pressedMs     = nowMs;
    button.longReported  = false;
    button.extraReported = false;
  }
  if (!level && button.lastLevel) {
    const uint32_t held = nowMs - button.pressedMs;
    if (!button.longReported && held > kButtonDebounceMs) button.pendingShort = true;
  }
  if (level) {
    const uint32_t held = nowMs - button.pressedMs;
    if (held >= kButtonExtraLongMs && !button.extraReported) {
      extraLong = true; button.extraReported = true; button.pendingShort = false;
    } else if (held >= kButtonLongPressMs && !button.longReported) {
      longPress = true; button.longReported = true; button.pendingShort = false;
    }
  }
  if (button.pendingShort) { shortPress = true; button.pendingShort = false; }
  button.lastLevel = level;
}

// ---------------------------------------------------------------------------
// Deep sleep
// ---------------------------------------------------------------------------

static void enterDeepSleep() {
  display.clearBuffer();
  sendDisplay();
  neopixel.off();
  digitalWrite(PIN_5V_EN, LOW);
  while (digitalRead(PIN_BUTTON) == LOW) delay(10);
  delay(50);
  const gpio_num_t wakeGpio = static_cast<gpio_num_t>(PIN_BUTTON);
  gpio_pullup_en(wakeGpio);
  gpio_pulldown_dis(wakeGpio);
  esp_sleep_enable_ext1_wakeup(1ULL << static_cast<uint64_t>(wakeGpio), ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();
}

// ---------------------------------------------------------------------------
// Loop sub-handlers (return true = this handler owns the display this frame)
// ---------------------------------------------------------------------------

static bool handleFwUpdateNotice(bool shortPress, bool longPress, bool extraLong) {
  if (!ctx.fwUpdatePending) return false;
  if (shortPress || longPress || extraLong) { acknowledgeNewFirmware(); return false; }
  renderFwUpdateComplete();
  return true;
}

static bool handleSleepCountdown(uint32_t nowMs, bool shortPress, bool longPress, bool extraLong) {
  if (!ctx.countdown.sleepPending) return false;
  if (shortPress || longPress || extraLong) { ctx.countdown.sleepPending = false; return false; }
  const uint32_t elapsed = nowMs - ctx.countdown.sleepStartMs;
  if (elapsed >= kSleepCountdownMs) { enterDeepSleep(); return true; }
  renderCountdown("Sleep in:", kSleepCountdownMs - elapsed);
  return true;
}

static bool handleResetCountdown(uint32_t nowMs, bool shortPress, bool longPress, bool extraLong) {
  if (!ctx.countdown.resetPending) return false;
  if (shortPress || longPress || extraLong) { ctx.countdown.resetPending = false; return false; }
  const uint32_t elapsed = nowMs - ctx.countdown.resetStartMs;
  if (elapsed >= kResetCountdownMs) {
    applyDefaultSettings();
    saveSettings();
    ctx.countdown.resetPending = false;
    return false;
  }
  renderCountdown("Reset in:", kResetCountdownMs - elapsed);
  return true;
}

static bool handleWifiCountdown(uint32_t nowMs, bool shortPress, bool longPress, bool extraLong) {
  if (!ctx.countdown.wifiPending) return false;
  if (shortPress || longPress || extraLong) { ctx.countdown.wifiPending = false; return false; }
  const uint32_t elapsed = nowMs - ctx.countdown.wifiStartMs;
  if (elapsed >= kWifiCountdownMs) {
    ctx.countdown.wifiPending = false;
    WifiServer::DashboardData dd;
    dd.batteryPercent  = ctx.device.batteryPercent;
    dd.charging        = ctx.device.charging;
    dd.lastMeasurement = 0.0f;
    dd.lastUser        = "Me";
    dd.tempC           = 25.0f;
    dd.humidity        = 50.0f;
    WifiServer::enterBlocking(display, dd);
    return false;
  }
  renderCountdown("FW Update:", kWifiCountdownMs - elapsed);
  return true;
}

static bool handleQrDisplay(bool shortPress, bool longPress) {
  if (!ctx.menu.showingQr) return false;
  if (shortPress || longPress) { ctx.menu.showingQr = false; return false; }
  renderAbout();
  return true;
}

static bool handleSettingAdjust(bool shortPress, bool longPress) {
  if (!ctx.menu.adjusting) return false;
  const uint8_t slot = ctx.menu.settingSlot;
  if (shortPress) {
    if (slot == 3) {
      ctx.settings.sensorSens = static_cast<uint8_t>(
          ctx.settings.sensorSens >= 25 ? 1 : ctx.settings.sensorSens + 1);
    } else if (slot == 4) {
      uint16_t next = static_cast<uint16_t>(ctx.settings.sleepTimeoutSec + kSleepTimeoutStepSec);
      if (next > kSleepTimeoutMaxSec) next = kSleepTimeoutMinSec;
      ctx.settings.sleepTimeoutSec = next;
    } else {
      uint8_t &lv = (slot == 0) ? ctx.settings.ledLevel
                  : (slot == 1) ? ctx.settings.buzzerLevel
                  : ctx.settings.vibLevel;
      lv = static_cast<uint8_t>((lv + 1) % 11);
      applyActuatorLevels();
    }
    ctx.menu.settingDirty = true;
  }
  if (longPress) {
    if (ctx.menu.settingDirty) { saveSettings(); ctx.menu.settingDirty = false; }
    ctx.menu.adjusting = false;
    renderSettingsMenu();
    return true;
  }
  renderSettingAdjust();
  return true;
}

// ---------------------------------------------------------------------------
// Rendering: measure stub screen
// ---------------------------------------------------------------------------

static void renderMeasureStub() {
  display.clearBuffer();
  ui.renderStatusBar(appState);
  clearContentArea();
  display.setFont(u8g2_font_9x15_tf);
  const char *title = "Measure";
  display.setCursor((kDisplayWidth - display.getStrWidth(title)) / 2, kMenuTop + 20);
  display.print(title);
  display.setFont(u8g2_font_6x10_tf);
  const char *hint = "Press to exit";
  display.setCursor((kDisplayWidth - display.getStrWidth(hint)) / 2, kMenuTop + 40);
  display.print(hint);
  sendDisplay();
}

static bool handleMeasureScreen(bool shortPress, bool longPress) {
  if (!ctx.measureActive) return false;
  if (shortPress || longPress) { ctx.measureActive = false; return false; }
  renderMeasureStub();
  return true;
}

static void renderCalibStub() {
  display.clearBuffer();
  ui.renderStatusBar(appState);
  clearContentArea();
  display.setFont(u8g2_font_9x15_tf);
  const char *title = "Calib";
  display.setCursor((kDisplayWidth - display.getStrWidth(title)) / 2, kMenuTop + 20);
  display.print(title);
  display.setFont(u8g2_font_6x10_tf);
  const char *hint = "Press to exit";
  display.setCursor((kDisplayWidth - display.getStrWidth(hint)) / 2, kMenuTop + 40);
  display.print(hint);
  sendDisplay();
}

static bool handleCalibScreen(bool shortPress, bool longPress) {
  if (!ctx.calibStubActive) return false;
  if (shortPress || longPress) { ctx.calibStubActive = false; return false; }
  renderCalibStub();
  return true;
}

// ---------------------------------------------------------------------------
// Menu navigation
// ---------------------------------------------------------------------------

static void handleMenuNav(uint32_t nowMs, bool shortPress, bool longPress, bool extraLong) {
  // WiFi/BLE sub-submenu
  if (ctx.menu.inWifiBleMenu) {
    if (shortPress)
      ctx.menu.wifiBleIndex = static_cast<uint8_t>((ctx.menu.wifiBleIndex + 1) % kWifiBleMenuCount);
    if (longPress) {
      if (ctx.menu.wifiBleIndex == 0) {
        ctx.menu.inWifiBleMenu = false;
      } else {
        ctx.settings.connection = (ctx.menu.wifiBleIndex == 1) ? ConnectionMode::BLE : ConnectionMode::Wifi;
        saveSettings();
        ctx.menu.inWifiBleMenu = false;
      }
    }
    if (extraLong) enterDeepSleep();
    if (ctx.menu.inWifiBleMenu) { renderWifiBleMenu(); return; }
    renderSettingsMenu();
    return;
  }

  // Settings submenu
  if (ctx.menu.inSubmenu) {
    if (shortPress)
      ctx.menu.subIndex = static_cast<uint8_t>((ctx.menu.subIndex + 1) % kSettingsMenuCount);
    if (longPress) {
      if (ctx.menu.subIndex == 0) {
        ctx.menu.inSubmenu = false;
        ctx.menu.subIndex  = 0;
      } else if (ctx.menu.subIndex == kSettingCalibIndex) {
        ctx.calibStubActive = true;
      } else if (ctx.menu.subIndex >= kSettingLedIndex && ctx.menu.subIndex <= kSettingVibIndex) {
        ctx.menu.adjusting    = true;
        ctx.menu.settingSlot  = static_cast<uint8_t>(ctx.menu.subIndex - kSettingLedIndex);
        ctx.menu.settingDirty = false;
      } else if (ctx.menu.subIndex == kSettingBuzzTestIndex) {
        ctx.buzzerTest.prevLevel = ctx.settings.buzzerLevel;
        buzzer.setLevel(10);
        buzzer.playTone(kBuzzerTestToneHz, kBuzzerTestDurationMs, 100);
        ctx.buzzerTest.endMs  = nowMs + kBuzzerTestDurationMs;
        ctx.buzzerTest.active = true;
      } else if (ctx.menu.subIndex == kSettingSensIndex) {
        ctx.menu.adjusting    = true;
        ctx.menu.settingSlot  = 3;
        ctx.menu.settingDirty = false;
      } else if (ctx.menu.subIndex == kSettingSleepIndex) {
        ctx.menu.adjusting    = true;
        ctx.menu.settingSlot  = 4;
        ctx.menu.settingDirty = false;
      } else if (ctx.menu.subIndex == kSettingWifiBleIndex) {
        ctx.menu.inWifiBleMenu = true;
        ctx.menu.wifiBleIndex  = (ctx.settings.connection == ConnectionMode::Wifi) ? 2 : 1;
      } else if (ctx.menu.subIndex == kSettingOtaIndex) {
        ctx.countdown.wifiPending = true;
        ctx.countdown.wifiStartMs = nowMs;
      } else if (ctx.menu.subIndex == kSettingResetIndex) {
        ctx.countdown.resetPending = true;
        ctx.countdown.resetStartMs = nowMs;
      }
    }
    if (extraLong) enterDeepSleep();
    if (ctx.menu.inSubmenu) { renderSettingsMenu(); return; }
    renderMainMenu();
    return;
  }

  // Main menu
  if (shortPress)
    ctx.menu.mainIndex = static_cast<uint8_t>((ctx.menu.mainIndex + 1) % kMainMenuCount);
  if (longPress) {
    switch (ctx.menu.mainIndex) {
      case kMainMeasureIndex:
        ctx.measureActive = true;
        break;
      case kMainSleepIndex:
        ctx.countdown.sleepPending = true;
        ctx.countdown.sleepStartMs = nowMs;
        break;
      case kMainAboutIndex:
        ctx.menu.showingQr = true;
        break;
      case kMainSettingsIndex:
        ctx.menu.inSubmenu = true;
        ctx.menu.subIndex  = 0;
        break;
      default:
        break;
    }
  }
  if (extraLong) enterDeepSleep();
  renderMainMenu();
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  Serial.print("\nReset reason: ");
  Serial.println(static_cast<int>(esp_reset_reason()));

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_5V_EN, OUTPUT);
  digitalWrite(PIN_5V_EN, LOW);
  pinMode(PIN_SHAKE, INPUT);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  delay(10);

  // Register BatteryManager event handler before begin() so no events are missed.
  // NOTE: callback is invoked from the BMS monitor task — keep it short.
  battery.onEvent([](BatteryManager::Event evt, const BatteryManager& bms) {
    switch (evt) {
      case BatteryManager::Event::VoltageUpdate:
        ctx.device.batteryPercent = bms.socPercent();
        ctx.device.charging       = bms.isCharging();
        ctx.device.plugged        = bms.isVbusPresent();
        log_i("[APP] VBAT %u mV  SoC %u%%  charging %d",
              bms.voltageMv(), bms.socPercent(), bms.isCharging());
        break;

      case BatteryManager::Event::StateChanged:
        log_i("[APP] BMS state -> %u", static_cast<uint8_t>(bms.state()));
        break;

      case BatteryManager::Event::Fault: {
        const uint8_t f = bms.lastFaultRegister();
        uint8_t reason = 0, ts = 0;
        if      (f & 0x80)         { reason = 1; }             // watchdog
        else if ((f & 0x30) == 0x10) reason = 2;               // input fault
        else if ((f & 0x30) == 0x20) reason = 3;               // thermal shutdown
        else if ((f & 0x30) == 0x30) reason = 4;               // safety timer
        else if  (f & 0x08)          reason = 5;               // battery OVP
        else {
          const uint8_t ntc = f & 0x07;
          if (ntc) { reason = 1; ts = (ntc == 2 || ntc == 6) ? 2 : 1; }
        }
        ctx.device.chargerFaultReason = reason;
        ctx.device.chargerTsFault     = ts;
        break;
      }

      case BatteryManager::Event::Low:
        log_w("[APP] Battery low (%u mV)", bms.voltageMv());
        break;

      case BatteryManager::Event::Critical:
        log_e("[APP] Battery critical (%u mV)!", bms.voltageMv());
        break;
    }
  });

  // Initialize BMS before enabling the 5V rail (battery is always present)
  if (!battery.begin()) Serial.println("BatteryManager begin failed");

  display.begin();
  ui.begin();
  display.clearBuffer();
  sendDisplay();

  if (!LittleFS.begin()) LittleFS.begin(true);
  if (!loadSettings()) {
    applyDefaultSettings();
    saveSettings();
  }

  // Enable 5V rail after charger is configured
  digitalWrite(PIN_5V_EN, HIGH);
  delay(50);

  buzzer.begin();
  vib.begin();
  neopixel.begin();
  applyActuatorLevels();
  neopixel.onColor(kDefaultNeopixelR, kDefaultNeopixelG, kDefaultNeopixelB);

  // Confirm OTA image is valid (cancels rollback timer)
  WifiServer::markAppValid();
  checkFirmwareVersion();

  ctx.lastInteractionMs = millis();
}

void loop() {
  const uint32_t nowMs = millis();

  buzzer.update(nowMs);
  vib.update(nowMs);

  // Restore buzzer level after test tone
  if (ctx.buzzerTest.active && static_cast<int32_t>(nowMs - ctx.buzzerTest.endMs) >= 0) {
    buzzer.setLevel(ctx.buzzerTest.prevLevel);
    ctx.buzzerTest.active = false;
  }

  bool shortPress = false, longPress = false, extraLong = false;
  readButton(nowMs, shortPress, longPress, extraLong);
  if (shortPress || longPress || extraLong) {
    ctx.lastInteractionMs = nowMs;
    vib.onFor(50, 100);
  }

  syncAppState();

  // Priority handlers — each returns true if it owns the display this frame
  if (handleSleepCountdown(nowMs, shortPress, longPress, extraLong)) return;
  if (handleFwUpdateNotice(shortPress, longPress, extraLong)) return;

  // Auto-sleep when idle
  if (ctx.settings.sleepTimeoutSec > 0 &&
      !ctx.countdown.sleepPending &&
      !ctx.countdown.resetPending &&
      nowMs - ctx.lastInteractionMs >= static_cast<uint32_t>(ctx.settings.sleepTimeoutSec) * 1000UL) {
    enterDeepSleep();
  }

  if (handleResetCountdown(nowMs, shortPress, longPress, extraLong)) return;
  if (handleWifiCountdown(nowMs, shortPress, longPress, extraLong)) return;
  if (handleCalibScreen(shortPress, longPress)) return;
  if (handleMeasureScreen(shortPress, longPress)) return;
  if (handleQrDisplay(shortPress, longPress)) return;
  if (handleSettingAdjust(shortPress, longPress)) return;

  if (nowMs - ctx.lastUiTickMs >= kUiTickMs) {
    ctx.lastUiTickMs = nowMs;
    handleMenuNav(nowMs, shortPress, longPress, extraLong);
  }
}
