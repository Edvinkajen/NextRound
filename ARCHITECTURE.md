# NextRound — Arkitektur och funktionsbeskrivning

Plattform: **ESP32-S3** (PlatformIO, Arduino-framework, 8 MB flash, PSRAM aktiverat)
Display: **SSD1306 128×64 OLED** via I2C och U8g2-biblioteket
Byggfil: `platformio.ini` — board `esp32-s3-devkitm-1`, partitionstabell med OTA-rollback

---

## 1. Filstruktur

```
src/
  main.cpp          — Applikationens huvudfil (setup + loop + all UI-logik)
  actuators.h/.cpp  — Buzzer, VibrationMotor, NeopixelLed
  bq25895.h/.cpp    — Batteriladdare (I2C)
  wifi_server.h/.cpp — WiFi SoftAP, OTA-uppladdning, HTML-dashboard
  pins.h            — Alla GPIO-pinndefinitioner
  OTA.h/.cpp        — OTA-hjälpfunktioner (markAppValid)
  qrcode_bitmap.h   — Förrenderad QR-kod som XBM-bitmap
  device_info.h     — kFirmwareVersion, kHardwareRev

lib/ui/src/
  ui.h/.cpp         — Ui-klass: statusbar, menulista, faultvisning
  appstate.h        — AppState-struct (data som Ui-klassen läser)
  ui_assets.h       — UiBitmap/UiAssets-struct för utbytbara bitmaps
  display_constants.h — kDisplayWidth/Height, kStatusBarHeight, kMenuTop/Height
```

---

## 2. Hårdvara

### 2.1 BQ25895 — Batteriladdare (`src/bq25895.h/.cpp`)

Kommunicerar via I2C. Konfigureras i `setup()` **innan** 5V-rälsen aktiveras.

**Konfiguration (i main.cpp):**
- `inputCurrentLimitMa = 500` — begränsar USB-inflöde
- `fastChargeCurrentMa = 1024`
- `chargeVoltageMv = 4208`
- `enableBatfet = true`, `batfetResetEnable = false`

`Bq25895::readStatus(bms)` returnerar:
- `batteryPercent` (0–100)
- `charging` (bool)
- `plugged` (bool)
- `faultReason` (0 = ingen, 1–6 = felkod)
- `tsFault` (temperaturfel: 1=kallt, 2=varmt)

Faultstatus visas som en popup-overlay av `Ui::drawFault()` om `chargerFaultReason != 0`.

**Polling:** Var 30 s (`kHwPollIntervalMs`). Tvingad vid uppstart.

### 2.2 Aktuatorer (`src/actuators.h/.cpp`)

Tre klasser som alla använder ESP32:s `ledc` PWM-system (8-bitars upplösning).

#### Buzzer
- PWM-kanal 0, 2700 Hz (`kPwmChannelBuzzer`, `kBuzzerPwmHz`)
- Pin: `PIN_BUZZER`
- API: `on()`, `off()`, `onFor(ms)`, `playTone(hz, ms)`, `setLevel(0–10)`, `update(nowMs)`
- `playTone()` ändrar PWM-frekvensen med `ledcWriteTone()` och stänger av efter `durationMs`
- Nivå 0–10 mappar till 0–100% duty via `levelToPercent()` (0=0%, 1=25%, 10=100%)
- `update()` anropas varje loop-iteration för att hantera tidsstyrda lägen

#### VibrationMotor
- PWM-kanal 1, 20 000 Hz
- Pin: `PIN_VIB`
- API: `on()`, `off()`, `onFor(ms, strength%)`, `pulse(count, onMs, offMs, strength%)`, `setLevel(0–10)`, `update(nowMs)`
- Stödjer tre lägen: `Idle`, `TimedOn`, `PulsingOn/PulsingOff`
- `pulse()` blinkar motorn `count` gånger med konfigurerbar on/off-tid
- Används för taktil bekräftelse: varje knapptryckning ger `vib.onFor(50, 100)`

#### NeopixelLed
- Adafruit NeoPixel, en pixel
- Pin: `PIN_LED`
- API: `onColor(r, g, b)`, `off()`, `setLevel(0–10)`, `apply()`
- Startfärg: RGB(200, 60, 130) — definieras av `kDefaultNeopixelR/G/B`
- Stängs av i `enterDeepSleep()`

### 2.3 GPIO och 5V-räls

- `PIN_5V_EN` — OUTPUT, sätts LOW vid init, HIGH efter att laddaren konfigurerats
- `PIN_BUTTON` — INPUT_PULLUP, aktiv LOW
- `PIN_SHAKE` — INPUT (accelerometer-interrupt, oanvänt i nuvarande kod)
- `PIN_I2C_SDA`, `PIN_I2C_SCL` — Wire-buss

---

## 3. Applikationsstruktur (main.cpp)

### 3.1 AppContext

En global `static AppContext ctx` håller all körningsstate. Uppdelad i substruct:

```
ctx.menu        — navigationsstate (vilken meny, vilket index, justeringsläge)
ctx.device      — hårdvarustatus (batteri, laddning, charger fault)
ctx.settings    — användarinställningar (ledLevel, buzzerLevel, vibLevel,
                  sleepTimeoutSec, connection)
ctx.buzzerTest  — state för pågående buzzertest
ctx.countdown   — flaggor och starttider för sleep/reset/wifi-nedräkning
ctx.fwUpdatePending   — satt efter OTA, visas som informationsskärm
ctx.fwPrevVersion     — sparad version från innan OTA
ctx.lastInteractionMs — används för auto-sleep timeout
ctx.lastUiTickMs      — begränsar UI-uppdateringstakt till kUiTickMs (30 ms)
```

### 3.2 AppState

`AppState appState` är den struct som Ui-klassen läser. Fylls i av `syncAppState()` varje loop-iteration:
- `batteryPercent`, `charging`, `connection`
- `chargerFaultReason`, `chargerTsFault`
- `menuIndex` (används av `Ui::drawMenu()` för att markera valt alternativ)

### 3.3 Loop-arkitektur

`loop()` kör i fast rotation och arbetar i denna ordning:

1. `pollHardware(nowMs)` — uppdatera batteri- och laddarstatus
2. `buzzer.update()`, `vib.update()` — hantera tidsstyrda aktuatorlägen
3. Avsluta buzzertest om tid löpt ut
4. `readButton()` — detektera knappevents
5. Ge vibrationsfeedback vid knapptryckning
6. `syncAppState()` — kopiera ctx → appState
7. **Priority handlers** (returnerar `true` om de äger displayen denna frame):
   - `handleSleepCountdown` — nedräkning innan sleep
   - `handleFwUpdateNotice` — visa OTA-banner
   - Auto-sleep check (om timeout löpt ut och inga aktiva processer)
   - `handleResetCountdown` — nedräkning innan fabriksåterställning
   - `handleWifiCountdown` — nedräkning innan WiFi-läge startas
   - `handleQrDisplay` — visa About-skärm med QR
   - `handleSettingAdjust` — hantera inställningsjustering
8. `handleMenuNav()` — normal menystyrning (begränsad till kUiTickMs)

---

## 4. Menysystem

### 4.1 Hierarki

```
Huvudmeny (3 alternativ)
  ├── Turn OFF  → sleep-nedräkning (3 s), sedan deep sleep
  ├── Settings  → inställningsundermeny
  │     ├── Return
  │     ├── LED          → värdesjustering (slot 0, 0–10)
  │     ├── Buzzer       → värdesjustering (slot 1, 0–10)
  │     ├── Vibration    → värdesjustering (slot 2, 0–10)
  │     ├── Buzzer Test  → spelar testton direkt
  │     ├── WIFI/BLE     → sub-undermeny för anslutningsval
  │     │     ├── Return
  │     │     ├── BLE    (radio-knapp)
  │     │     └── WiFi   (radio-knapp)
  │     ├── OFF Timer    → värdesjustering (slot 3, 0–900 s i steg om 30)
  │     ├── FW Update    → WiFi-nedräkning (3 s), sedan enterBlocking()
  │     └── Reset        → fabriksåterställning (10 s nedräkning, kan avbrytas)
  └── About     → QR-kod + firmware/hw-version
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

Alla menyer visar exakt **2 alternativ** synliga samtidigt (halva menu-arean = `kMenuHeight / 2` px per rad). Scrollning beräknas dynamiskt baserat på valt index: `first = max(0, selected - 1)`. Det valda alternativet markeras med en ram (`drawFrame`).

### 4.4 Inställningsjustering (renderSettingAdjust)

Slot-mapping:
- Slot 0 = LED-nivå (0–10)
- Slot 1 = Buzzernivå (0–10)
- Slot 2 = Vibrationsnivå (0–10)
- Slot 3 = Auto-sleep timeout (0–900 s i steg om 30)

Visar ett vertikalt stapeldiagram (fill-bar) samt värde i en ram. Kort tryck ökar värdet, långt tryck sparar och lämnar.

---

## 5. Inställningslagring (`/settings.json` på LittleFS)

JSON-schema:
```json
{
  "led": 5,
  "buzz": 5,
  "vib": 5,
  "sleepTimeout": 300,
  "connection": "wifi"
}
```

- Laddas i `setup()`, sparas direkt när ett värde bekräftas med långt tryck
- `applyDefaultSettings()` skriver standardvärden i minnet men sparar inte automatiskt
- Fabriksåterställning: `applyDefaultSettings()` + `saveSettings()` efter 10 s nedräkning

---

## 6. WiFi-läge och OTA (`src/wifi_server.h/.cpp`)

### 6.1 Aktivering

Aktiveras via Settings → FW Update. En 3-sekunders nedräkning (`kWifiCountdownMs`) visas och kan avbrytas. Därefter anropas `WifiServer::enterBlocking(display, dd)`.

### 6.2 enterBlocking()

Blockerar main loop helt under WiFi-sessionens gång.

1. Genererar slumpmässigt SSID (`NextRound-XXXXXX`) och lösenord (8 hex-tecken) med `esp_random()`
2. Startar SoftAP med dessa credentials
3. Renderar SSID + IP (`192.168.4.1`) + QR-kod på OLED
4. Startar `WebServer` på port 80
5. Exponerar routes:
   - `GET /` — HTML-dashboard med batteristatus, laddningsstatus, senaste mätning (0.0 i nuläget), temp/fukt (25/50 som defaults)
   - `POST /update` — tar emot firmware-binär, kör OTA via ESP Arduino Update-biblioteket, visar progress på OLED
6. Avslutas när knappen hålls ≥2 s eller OTA-reboot sker

### 6.3 OTA rollback-skydd

`WifiServer::markAppValid()` kallas i `setup()` varje gång firmware startar korrekt. Detta bekräftar OTA-imagen och avbryter automatisk rollback. Implementerat via `esp_ota_ops.h`.

### 6.4 FW-versionsdetektering

`checkFirmwareVersion()` läser `/fw_info.txt` från LittleFS. Om versionen skiljer sig från `kFirmwareVersion` (i `device_info.h`) sätts `ctx.fwUpdatePending = true`. Vid nästa startup visas en "FW Update complete V1.x → V1.y"-banner som kvitteras med valfri knapptryckning. Därefter skrivs ny version till filen.

---

## 7. Statusbar (`lib/ui/`)

Ritad av `Ui::drawStatusBar()` och `Ui::renderStatusBar()` (anropas från main.cpp varje frame).

**Innehåll (vänster till höger):**
- Laddningsikon (blixt-bitmap) om `state.charging == true`
- Anslutningsikon: WiFi-bitmap, BLE-bitmap, eller text "OFF"
- Batteri-ram (20×8 px) med procentuell fyllning + terminalblock (2×4 px)

**Faultvisning:** `Ui::drawFault()` renderar en inverterad overlay-box mitt på skärmen med "Charger fault" + feltext om `chargerFaultReason != 0`. Visas alltid ovanpå befintlig menyinnehåll.

**Displaylayout:**
```
Y=0   ┌────────────────────────────────────────────────┐  kStatusBarHeight px
      │  [WiFi/BLE]  [⚡]  [====    ] 75%              │
Y=kMenuTop ├────────────────────────────────────────────────┤  kMenuHeight px
      │  [Icon]  MenuItem 1                            │  kMenuHeight/2 px
      │  [Icon]  MenuItem 2  ◄ selected (frame)        │  kMenuHeight/2 px
Y=64  └────────────────────────────────────────────────┘
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
| `kSleepTimeoutMaxSec` | 900 s | Max auto-sleep (15 min) |
| `kSleepTimeoutStepSec` | 30 s | Steg i sleep-timeout-inställning |
| `kHwPollIntervalMs` | 30 000 ms | BQ25895-pollintervall |
| `kBuzzerTestToneHz` | 2 000 Hz | Testton-frekvens |
| `kBuzzerTestDurationMs` | 500 ms | Testtonens längd |
| `kChargerInputCurrentLimitMa` | 500 mA | USB-ingångsström |
| `kChargerFastChargeCurrentMa` | 1 024 mA | Snabbladdningsström |
| `kChargerVoltageMv` | 4 208 mV | Laddningsspänning |

---

## 10. Beroenden (platformio.ini lib_deps)

| Bibliotek | Användning |
|---|---|
| `olikraus/U8g2` | OLED-display (I2C SSD1306, full buffer-läge) |
| `bblanchon/ArduinoJson` | settings.json, wifi_server dashboard |
| `ricmoo/QRCode` | QR-kodgenerering i wifi_server |
| `adafruit/Adafruit NeoPixel` | RGB-LED |

ESP32-inbyggda: `WiFi`, `WebServer`, `Update` (OTA), `LittleFS`, `Wire`, `ledc` (PWM)
