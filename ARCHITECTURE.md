# NextRound — Arkitektur och funktionsbeskrivning

Plattform: **ESP32-S3** (PlatformIO, Arduino-framework, 8 MB flash, PSRAM aktiverat)
Display: **SSD1306 128×64 OLED** via I2C och U8g2-biblioteket
Byggfil: `platformio.ini` — board `esp32-s3-devkitm-1`, partitionstabell med OTA-rollback

---

## 1. Filstruktur

```
src/
  main.cpp            — Applikationens huvudfil (setup + loop + all UI-logik)
  actuators.h/.cpp    — Buzzer, VibrationMotor, NeopixelLed (PwmActuator-bas)
  wifi_server.h/.cpp  — WiFi SoftAP, OTA-uppladdning, HTML-dashboard (namespace)
  pins.h              — Alla GPIO-pinndefinitioner
  qrcode_bitmap.h     — Förrenderad QR-kod som XBM-bitmap (About-skärmen)

include/
  device_info.h       — kFirmwareVersion, kHardwareRev, kBuildDate
  app_state.h         — AppState-struct + ConnectionMode-enum (äldre kopia)

lib/
  battery/
    BatteryManager.h/.cpp  — BQ25895-batteriladdare via I2C + FreeRTOS-task
    library.properties
  ui/src/
    ui.h/.cpp              — Ui-klass: statusbar, menulista, faultvisning
    appstate.h             — AppState-struct + ConnectionMode-enum (aktiv kopia)
    ui_assets.h            — UiBitmap/UiAssets-struct för utbytbara bitmaps
    display_constants.h    — kDisplayWidth/Height, kStatusBarHeight, etc.
  README
```

> **Notera:** Det finns två kopior av `AppState`/`ConnectionMode` — en i `lib/ui/src/appstate.h` (inkluderas av ui.h) och en i `include/app_state.h`. Den aktiva är den i `lib/ui/src/`.

---

## 2. Hårdvara

### 2.1 BatteryManager — BQ25895 batteriladdare (`lib/battery/BatteryManager.h/.cpp`)

Kommunicerar via I2C (adress `0x6A`). Initialiseras i `setup()` innan 5V-rälsen aktiveras. Kör en egen FreeRTOS-task (`bms_mon`, prio 3, APP_CPU) som pollar laddaren via INT-pin (GPIO 11, falling edge) eller var 1:e sekund.

**Konfiguration (i `begin()`):**
- Ingångsström: 1000 mA (`BQ25895_IINLIM_MA`)
- Laddningsström: 448 mA (REG04 = 0x07)
- Laddningsspänning: 4,208 mV
- Watchdog: avstängd
- OTG/boost: avstängd
- Automatisk USB-detektion (HVDCP/MAXC/DPDM): avstängd
- ADC kontinuerligt läge: på (~1 Hz)

**Event-API:** `BatteryManager::onEvent(callback)` anropar vid:
- `VoltageUpdate` — ny spänning/SoC avläst
- `StateChanged` — ändring i laddningsstate
- `Fault` — feltillstånd (watchdog, input, thermal, timer, battery OVP, NTC)
- `Low` — VBAT < 3300 mV
- `Critical` — VBAT < 3000 mV

**Public accessors:**
- `voltageMv()` — batterispänning i mV
- `socPercent()` — 0–100 % (linjär interpolering i uppslagstabell)
- `state()` — Discharging / PreCharge / FastCharge / ChargeComplete / Fault
- `isCharging()`, `isVbusPresent()`, `hasFault()`, `lastFaultRegister()`
- `wireMutex()` — SemaphoreHandle för I2C-mutex (delas med display för att undvika I2C-konflikter)

**SoC-tabell (no-load Li-ion):**

| mV   | % |
|------|---|
| 4200 | 100 |
| 4060 | 85 |
| 3920 | 70 |
| 3800 | 55 |
| 3720 | 40 |
| 3660 | 25 |
| 3500 | 10 |
| 3300 | 0 |

### 2.2 Aktuatorer (`src/actuators.h/.cpp`)

Tre klasser som alla använder ESP32:s `ledc` PWM-system (8-bitars upplösning).

#### PwmActuator (basklass)
- Delad logik för pin/channel/level/strength
- `levelToPercent(level)` — nivå 0–10 mappar linjärt: 0→0%, 1→25%, 10→100%
- `scalePercent(base, cmd)` — scaling för override-styrka
- `writePercent(enabled)` — skriver duty cycle

#### Buzzer
- PWM-kanal 0, 2700 Hz (`kPwmChannelBuzzer`, `kBuzzerPwmHz`)
- Pin: `PIN_BUZZER` (13)
- Lägen: `Idle`, `TimedOn`, `TimedTone`, `ContinuousTone`, `ContinuousOn`
- API: `on()`, `on(strength%)`, `off()`, `onFor(now, ms)`, `onFor(now, ms, strength%)`, `playTone(now, hz, ms)`, `playTone(now, hz, ms, strength%)`, `setLevel(0–10)`, `update(nowMs)`
- `playTone()` ändrar PWM-frekvens med `ledcWriteTone()`, `off()` återställer frekvensen
- `update()` hanterar tidsstyrda lägen, anropas varje loop-iteration

#### VibrationMotor
- PWM-kanal 2, 20 000 Hz
- Pin: `PIN_VIB` (17)
- Lägen: `Idle`, `TimedOn`, `PulsingOn`, `PulsingOff`
- API: `on()`, `on(strength%)`, `off()`, `onFor(now, ms)`, `onFor(now, ms, strength%)`, `pulse(now, count, onMs, offMs)`, `pulse(now, count, onMs, offMs, strength%)`, `setLevel(0–10)`, `update(nowMs)`
- Varje knapptryckning ger `vib.onFor(nowMs, 50, 100)` (50 ms, 100 % styrka)

#### NeopixelLed
- Adafruit NeoPixel, en pixel
- Pin: `PIN_LED` (8)
- API: `onColor(r, g, b)`, `onColor(hex)`, `off()`, `setLevel(0–10)`, `isActive()`
- Startfärg: RGB(200, 60, 130) — definieras av `kDefaultNeopixelR/G/B`
- Ljusstyrka skalas via `setBrightness(level * 255 / 10)`
- Stängs av i `enterDeepSleep()`

### 2.3 GPIO och 5V-räls

| Pin | Funktion |
|-----|----------|
| 5   | `PIN_ALC_SENSOR` — etanolsensor (ADC) |
| 6   | `PIN_HEATER` — sensorvärmare (PWM/GPIO) |
| 4   | `PIN_MIC` — mikrofon (ADC) |
| 8   | `PIN_LED` — NeoPixel (GPIO) |
| 9   | `PIN_I2C_SDA` — I2C data |
| 10  | `PIN_I2C_SCL` — I2C clock |
| 11  | `PIN_INT` — BQ25895 INT (falling edge) |
| 12  | `PIN_BUTTON` — INPUT_PULLUP, aktiv LOW |
| 13  | `PIN_BUZZER` — piezo-buzzer (PWM) |
| 15  | `PIN_5V_EN` — 5V-räls enable, sätts LOW vid init, HIGH efter laddarkonfig |
| 17  | `PIN_VIB` — vibrationsmotor (PWM) |
| 18  | `PIN_SHAKE` — accelerometer-interrupt (INPUT, oanvänt) |

---

## 3. Applikationsstruktur (main.cpp)

### 3.1 AppContext

En global `static AppContext ctx` håller all körningsstate. Uppdelad i substruct:

```
ctx.menu            — navigationsstate (mainIndex, subIndex, inSubmenu, inWifiBleMenu,
                      wifiBleIndex, showingQr, adjusting, settingSlot, settingDirty)
ctx.device          — hårdvarustatus (batteryPercent, charging, plugged,
                      chargerFaultReason, chargerTsFault)
ctx.settings        — användarinställningar (ledLevel, buzzerLevel, vibLevel,
                      sensorSens, sleepTimeoutSec, connection)
ctx.buzzerTest      — state för pågående buzzertest (active, endMs, prevLevel)
ctx.countdown       — flaggor och starttider för sleep/reset/wifi-nedräkning
ctx.fwUpdatePending — satt efter OTA, visas som informationsskärm
ctx.fwPrevVersion   — sparad version från innan OTA
ctx.measureActive   — Measure-skärm aktiv
ctx.calibStubActive — Calib-skärm aktiv
ctx.lastMeasurement — senaste uppmätta värde
ctx.lastInteractionMs — används för auto-sleep timeout
ctx.lastUiTickMs    — begränsar UI-uppdateringstakt till kUiTickMs (30 ms)
```

### 3.2 AppState

`AppState appState` är den struct som Ui-klassen läser. Fylls i av `syncAppState()` varje loop-iteration:
- `batteryPercent`, `charging`, `connection`, `lastUser`, `lastMeasurement`
- `chargerFaultReason`, `chargerTsFault`
- `menuIndex` (används av `Ui::drawMenu()` för att markera valt alternativ)

### 3.3 Loop-arkitektur

`loop()` kör i fast rotation och arbetar i denna ordning:

1. `buzzer.update(nowMs)`, `vib.update(nowMs)` — hantera tidsstyrda aktuatorlägen
2. Avsluta buzzertest om tid löpt ut
3. `readButton(nowMs)` — detektera knappevents
4. Ge vibrationsfeedback vid knapptryckning
5. `syncAppState()` — kopiera ctx → appState
6. **Priority handlers** (returnerar `true` om de äger displayen denna frame):
   - `handleSleepCountdown` — nedräkning innan sleep
   - `handleFwUpdateNotice` — visa OTA-banner
   - Auto-sleep check (om timeout löpt ut och inga aktiva processer)
   - `handleResetCountdown` — nedräkning innan fabriksåterställning
   - `handleWifiCountdown` — nedräkning innan WiFi-läge startas
   - `handleCalibScreen` — Calib-stubbskärm
   - `handleMeasureScreen` — Measure-stubbskärm
   - `handleQrDisplay` — visa About-skärm med QR
   - `handleSettingAdjust` — hantera inställningsjustering
7. `handleMenuNav()` — normal menystyrning (begränsad till kUiTickMs)

> **Notering:** Till skillnad från äldre arkitekturbeskrivningar pollas inte hårdvara (batteri) i loopen längre — BatteryManager kör en separat FreeRTOS-task som anropar callback vid händelser.

---

## 4. Menysystem

### 4.1 Hierarki

```
Huvudmeny (4 alternativ)
  ├── Measure    → Measure-stubbskärm (placeholder)
  ├── Turn OFF   → sleep-nedräkning (3 s), sedan deep sleep
  ├── Settings   → inställningsundermeny
  │     ├── Return
  │     ├── Calib       → Calib-stubbskärm (placeholder)
  │     ├── LED         → värdesjustering (slot 0, 0–10)
  │     ├── Buzzer      → värdesjustering (slot 1, 0–10)
  │     ├── Vibration   → värdesjustering (slot 2, 0–10)
  │     ├── Buzzer Test → spelar testton direkt
  │     ├── WIFI/BLE    → sub-undermeny för anslutningsval
  │     │     ├── Return
  │     │     ├── BLE    (radio-knapp, kvadrat)
  │     │     └── WiFi   (radio-knapp, kvadrat)
  │     ├── Sensor      → värdesjustering (slot 3, 0–25)
  │     ├── OFF Timer   → värdesjustering (slot 4, 0–900 s i steg om 30)
  │     ├── FW Update   → WiFi-nedräkning (3 s), sedan WifiServer::enterBlocking()
  │     └── Reset       → fabriksåterställning (10 s nedräkning)
  └── About      → QR-kod + firmware/hw-version
```

### 4.2 Knapptryckningstolkning

En enda fysisk knapp (`PIN_BUTTON`, aktiv LOW) med tre nivåer:

| Tryck | Längd | Funktion |
|---|---|---|
| Kort (short) | >30 ms, <800 ms | Navigera framåt i aktuell lista |
| Långt (long) | ≥800 ms | Välj/bekräfta/spara |
| Extra långt | ≥7 000 ms | Direkt deep sleep varifrån som helst |

Logiken finns i `readButton()`. Hysteresis hanteras med `pendingShort`-flaggan som fördröjer short-press till knappen släpps.

### 4.3 Menyscrollning

Alla menyer visar exakt **2 alternativ** synliga samtidigt (halva menu-arean = `kMenuHeight / 2` px per rad). Scrollning beräknas dynamiskt baserat på valt index: `first = max(0, selected - 1)`. Det valda alternativet markeras med en ram (`drawFrame`). Varje menyrad har en 16×16 XBM-ikon till vänster.

### 4.4 Inställningsjustering (renderSettingAdjust)

Slot-mapping:
- Slot 0 = LED-nivå (0–10)
- Slot 1 = Buzzernivå (0–10)
- Slot 2 = Vibrationsnivå (0–10)
- Slot 3 = Sensorkänslighet (1–25)
- Slot 4 = Auto-sleep timeout (0–900 s i steg om 30)

Visar ett vertikalt stapeldiagram (fill-bar) samt värde i en ram. För OFF Timer visas värdet formaterat som `OFF`, `30s`, `1m`, `5m 0s` etc. Kort tryck ökar värdet (med wraparound), långt tryck sparar och lämnar.

### 4.5 WiFi/BLE-submeny

Kvadratiska knappar med ifylld innerruta för aktivt läge (`drawFrame` + `drawBox`). Långt tryck på BLE eller WiFi sparar valet via `saveSettings()` och återgår till Settings-menyn.

---

## 5. Inställningslagring (`/settings.json` på LittleFS)

JSON-schema:
```json
{
  "led": 5,
  "buzz": 5,
  "vib": 5,
  "sens": 10,
  "sleepTimeout": 300,
  "connection": "wifi"
}
```

- Laddas i `setup()`, sparas direkt när ett värde bekräftas med långt tryck
- `applyDefaultSettings()` skriver standardvärden i minnet men sparar inte automatiskt
- Fabriksåterställning: `applyDefaultSettings()` + `saveSettings()` efter 10 s nedräkning

---

## 6. WiFi-läge och OTA

### 6.1 WifiServer (`src/wifi_server.h/.cpp`)

Aktiveras via Settings → FW Update. En 3-sekunders nedräkning (`kWifiCountdownMs`) visas och kan avbrytas. Därefter anropas `WifiServer::enterBlocking(display, dd)`.

**enterBlocking():**
1. Genererar slumpmässigt SSID (`NextRound-XXXXXX`) och lösenord (8 hex-tecken) med `esp_random()`
2. Startar SoftAP med dessa credentials
3. Renderar SSID + IP (`192.168.4.1`) + QR-kod på OLED
4. Startar `WebServer` på port 80
5. Exponerar routes:
   - `GET /` — HTML-dashboard (mörkt tema) med batteri, laddning, temp/fukt, senaste mätning
   - `GET /api/status` — JSON: battery, charging, temp, humidity, lastMeasurement, lastUser
   - `GET /api/history` — JSON-array ur `/users.json` (user, promille, timestamp)
   - `GET /status` — OTA-status som plain text
   - `POST /update` — tar emot firmware-binär, kör OTA via ESP Arduino Update-biblioteket
6. Avslutas när knappen hålls ≥2 s eller OTA-reboot sker

### 6.2 OTA rollback-skydd

`WifiServer::markAppValid()` (via `esp_ota_mark_app_valid_cancel_rollback()`) anropas i `setup()` varje gång firmware startar korrekt för att bekräfta OTA-imagen.

### 6.3 FW-versionsdetektering

`checkFirmwareVersion()` läser `/fw_info.txt` från LittleFS. Om versionen skiljer sig från `kFirmwareVersion` (i `include/device_info.h`) sätts `ctx.fwUpdatePending = true`. Vid nästa startup visas en "FW Update complete V1.x → V1.y"-banner som kvitteras med valfri knapptryckning. Därefter skrivs ny version till filen.

---

## 7. Statusbar (`lib/ui/`)

Ritas av `Ui::drawStatusBar()` (anropas från main.cpp som `ui.renderStatusBar(appState)`).

**Innehåll (vänster till höger):**
- Användarnamn + senaste mätvärde (t.ex. "Me 0.00")
- Separator (vertikal linje)
- Anslutningsikon: WiFi-bitmap, BLE-bitmap eller text "OFF"
- Laddningsikon (blixt-bitmap) om `state.charging == true`
- Batteri-ram (20×8 px) med procentuell fyllning + terminalblock (2×4 px)

**Faultvisning:** `Ui::drawFault()` renderar en inverterad overlay-box mitt på skärmen med "Charger fault" + feltext om `chargerFaultReason != 0`. Visas alltid ovanpå befintlig menyinnehåll.

**UiAssets-stöd:** Alla ikoner (statusbar, navBar, menuBackground, iconBle/Wifi/Charge/Battery) kan bytas ut via `setAssets()` med egna bitmap-data.

**Displaylayout:**
```
Y=0   ┌────────────────────────────────────────────────┐  kStatusBarHeight=14 px
      │  Me 0.00  │  [WiFi/BLE]  [⚡]  [====    ] 75%  │
Y=16  ├────────────────────────────────────────────────┤  kMenuHeight=46 px
      │  [Icon]  MenuItem 1                            │  kMenuHeight/2=23 px
      │  [Icon]  MenuItem 2  ◄ selected (frame)        │  kMenuHeight/2=23 px
Y=62  └────────────────────────────────────────────────┘
```

---

## 8. Deep sleep

`enterDeepSleep()`:
1. Rensar och skickar tom displaybuffer (svart skärm)
2. Stänger av NeoPixel
3. Sätter `PIN_5V_EN` LOW (stänger av 5V-rälsen)
4. Väntar tills knappen är släppt
5. Konfigurerar ext1 wakeup på `PIN_BUTTON` (aktiv LOW)
6. Anropar `esp_deep_sleep_start()`

**Auto-sleep:** Om `sleepTimeoutSec > 0` och ingen interaktion skett på `sleepTimeoutSec` sekunder utlöses sleep automatiskt (förutsatt att inga countdown-processer är aktiva).

---

## 9. Konstanter och gränsvärden (main.cpp)

| Konstant | Värde | Betydelse |
|---|---|---|
| `kButtonDebounceMs` | 30 ms | Min-tid för att räknas som tryck |
| `kButtonLongPressMs` | 800 ms | Gräns för long press |
| `kButtonExtraLongMs` | 7 000 ms | Gräns för extra long (direkt sleep) |
| `kUiTickMs` | 30 ms | Max UI-uppdateringsfrekvens (~33 Hz) |
| `kSleepCountdownMs` | 3 000 ms | Nedräkning innan sleep |
| `kResetCountdownMs` | 10 000 ms | Nedräkning innan fabriksåterställning |
| `kWifiCountdownMs` | 3 000 ms | Nedräkning innan WiFi-läge |
| `kSleepTimeoutMinSec` | 0 s | Min auto-sleep |
| `kSleepTimeoutMaxSec` | 900 s | Max auto-sleep (15 min) |
| `kSleepTimeoutStepSec` | 30 s | Steg i sleep-timeout-inställning |
| `kBuzzerTestToneHz` | 2 000 Hz | Testton-frekvens |
| `kBuzzerTestDurationMs` | 500 ms | Testtonens längd |
| `kDefaultNeopixelR/G/B` | 200/60/130 | NeoPixel-startfärg |

---

## 10. Beroenden (platformio.ini lib_deps)

| Bibliotek | Användning |
|---|---|
| `olikraus/U8g2` | OLED-display (I2C SSD1306, full buffer-läge) |
| `bblanchon/ArduinoJson` | settings.json, wifi_server dashboard, users.json |
| `ricmoo/QRCode` | QR-kodgenerering i wifi_server |
| `adafruit/Adafruit NeoPixel` | RGB-LED |

ESP32-inbyggda: `WiFi`, `WebServer`, `Update` (OTA), `LittleFS`, `Wire`, `ledc` (PWM)
