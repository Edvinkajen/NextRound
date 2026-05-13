# NextRound — BLE GATT-schema

**Status:** Utkast v0.1 — kontrakt mellan firmware och companion-app.
**Plattform:** ESP32-S3 med NimBLE-stack.
**Relaterade dokument:** `ARCHITECTURE.md` (övergripande firmware-arkitektur).

---

## 1. Konventioner

### 1.1 Byte-ordning och datatyper
Alla multi-byte-värden är **little-endian** (BLE-standard). Datatyper i payload-tabeller:

| Notation | Betydelse |
|---|---|
| `u8`, `u16`, `u32` | Unsigned int |
| `i8`, `i16` | Signed int (two's complement) |
| `epoch` | `u32` unix-tid i sekunder (`0` = ej satt) |
| `char[N]` | UTF-8 sträng, null-padded till N byte |
| `mbac` | `u16` milli-BAC, t.ex. 0.34‰ → 340 |
| `tempC10` | `i16` temperatur i °C × 10, t.ex. 23.4°C → 234 |
| `rhPct10` | `u16` luftfuktighet × 10, t.ex. 45.2% → 452 |

### 1.2 Versionsbyte
Alla characteristics med strukturerad payload har `version: u8` som första byte. Aktuell version är `0x01` överallt. Klienter ska kontrollera versionen och hantera okända versioner som "ignorera fält efter sista kända offset". Server bumpar versionen när payload-layout ändras icke-bakåtkompatibelt.

### 1.3 UUID-konvention
Custom base: `4e52SSCC-9b1f-4b3e-8a7c-3a4f6d8e9c01`
där `SS` = service-nibble, `CC` = characteristic-index inom service.
(`4e52` = ASCII "NR".)

> **Notera:** UUID:erna i detta dokument är illustrativa. Innan du börjar koda, kör `uuidgen` och fixera nya base-UUID:er för projektet, eller bekräfta att dessa kan användas. Fixerade UUID:er ska aldrig ändras efter första app-release.

### 1.4 MTU
Firmware ska negotiera ATT MTU till **247 byte** vid connection (244 byte payload). Detta görs via `ble_att_set_preferred_mtu(247)` i NimBLE. Default 23 byte är otillräckligt för flera av våra characteristics.

### 1.5 Connection parameters
Föreslagna intervall (firmware som peripheral begär):
- Connection interval: 15–30 ms
- Slave latency: 0
- Supervision timeout: 4 s

Detta ger låg latens för live_data-notifies utan att dränera batteriet. Justera om appen rapporterar laggig UI.

### 1.6 CCCD
Alla characteristics med `notify` eller `indicate` har en implicit Client Characteristic Configuration Descriptor (CCCD, UUID 0x2902). Appen måste skriva `0x0001` (notify) eller `0x0002` (indicate) för att aktivera.

### 1.7 Säkerhetsmodell
- **Reads:** öppna utan bonding (Just Works pairing räcker).
- **Writes:** kräver bonded connection. Försök till write utan bonding returnerar `ATT_ERR_INSUFFICIENT_AUTHENTICATION`.
- **Pairing:** Numeric Comparison (6-siffrigt nummer visas på OLED + app, användaren bekräftar på båda).
- Bonding-info persisteras på enheten (NimBLE NVS).

---

## 2. Service-översikt

| Service | UUID | Bonding för writes? | Notes |
|---|---|---|---|
| Device Information | `0x180A` (standard) | n/a (read-only) | Manufacturer, model, FW/HW-rev, serial |
| Battery | `0x180F` (standard) | n/a (read-only) | Standard battery level för kompatibilitet |
| Power (custom) | `4e521000-…` | Ja | Detaljerad batteri/laddstatus |
| Measurement | `4e522000-…` | Ja | Live mätflöde |
| History | `4e523000-…` | Ja | Pull-baserad historik |
| User | `4e524000-…` | Ja | Profilhantering |
| Time | `4e525000-…` | Ja | Soft clock |
| Settings | `4e526000-…` | Ja | LED/buzzer/vib/sensor/sleep |
| Calibration | `4e527000-…` | Ja | Device-wide kalibrering |
| OTA (placeholder) | `4e528000-…` | Ja | UUID reserverade, ej specificerade i v1 |

---

## 3. Device Information Service (`0x180A`)

Standard GATT-service, alla characteristics är read-only `u8[]`-strängar.

| Characteristic | UUID | Värde |
|---|---|---|
| Manufacturer Name | `0x2A29` | `"NextRound"` |
| Model Number | `0x2A24` | `kHardwareRev` (t.ex. `"rev-c"`) |
| Firmware Revision | `0x2A26` | `kFirmwareVersion` (t.ex. `"1.2.0"`) |
| Hardware Revision | `0x2A27` | `kHardwareRev` |
| Software Revision | `0x2A28` | `kFirmwareVersion` |
| Serial Number | `0x2A25` | MAC-baserat ID, t.ex. `"NR-A1B2C3D4"` |

Appen läser dessa en gång efter pairing och cachar.

---

## 4. Battery Service (`0x180F`)

Standard GATT-service. Endast en characteristic exponeras här — för detaljerad status, se Power Service.

| Characteristic | UUID | Properties | Payload |
|---|---|---|---|
| Battery Level | `0x2A19` | read + notify | `u8` SoC 0–100 |

Notify trigger: SoC-ändring ≥1%.

---

## 5. Power Service (`4e521000-…`)

Detaljerad batteri- och laddstatus. Speglar `BatteryManager` i firmware.

### 5.1 `power_status` — read + notify
UUID: `4e521001-…`
Payload (8 byte):

| Offset | Fält | Typ | Notes |
|---|---|---|---|
| 0 | version | u8 | `0x01` |
| 1 | soc | u8 | 0–100 |
| 2–3 | voltage_mv | u16 | mV |
| 4 | state | u8 | 0=Discharging, 1=PreCharge, 2=FastCharge, 3=ChargeComplete, 4=Fault |
| 5 | fault | u8 | Bitmask: 0x01=Watchdog, 0x02=Input, 0x04=Thermal, 0x08=Timer, 0x10=BatteryOVP, 0x20=NTC |
| 6 | flags | u8 | bit0=vbus_present, bit1=charging |
| 7 | reserved | u8 | `0x00` |

Notify trigger: state-byte, charging-flag, eller fault-byte ändras. Voltage notify:as inte separat (för att inte dränka radio); appen kan läsa när den vill.

---

## 6. Measurement Service (`4e522000-…`)

Hjärtat i schemat. Appen subscribar på `state` och `result` direkt efter pairing.

### 6.1 `state` — read + notify
UUID: `4e522001-…`
Payload (1 byte):

| Värde | State |
|---|---|
| `0x00` | Idle |
| `0x01` | Warmup |
| `0x02` | Ready |
| `0x03` | Blowing |
| `0x04` | Measuring |
| `0x05` | Result |
| `0x06` | Fault |

Notify trigger: varje state-byte. Appen byter vy baserat på detta.

### 6.2 `live_data` — notify only
UUID: `4e522002-…`
Tagged union. Första byte = typ, resten beror på typ. Max 7 byte total.

**Type `0x01` — Warmup-progress** (notifieras ~5 Hz under Warmup):
| Offset | Fält | Typ |
|---|---|---|
| 0 | type | u8 = `0x01` |
| 1 | warmup_pct | u8 (0–100) |
| 2 | warmup_seconds_remaining | u8 |

**Type `0x02` — Blow-data** (notifieras ~20 Hz under Blowing):
| Offset | Fält | Typ |
|---|---|---|
| 0 | type | u8 = `0x02` |
| 1–2 | strength | u16 (mic envelope, 0–4095) |
| 3–4 | duration_ms | u16 |

**Type `0x03` — Sensor debug** (endast om debug-flagga är aktiv, ~2 Hz):
| Offset | Fält | Typ |
|---|---|---|
| 0 | type | u8 = `0x03` |
| 1–2 | raw_adc | u16 |
| 3–4 | tempC10 | i16 |
| 5–6 | rhPct10 | u16 |

### 6.3 `result` — read + notify
UUID: `4e522003-…`
Senaste färdiga mätningen. Notify:as när state → Result. Read returnerar senaste, eller alla nollor om ingen mätning finns sedan boot.

Payload (14 byte):

| Offset | Fält | Typ | Notes |
|---|---|---|---|
| 0 | version | u8 | `0x01` |
| 1 | user_id | u8 | Aktiv användare vid mättillfället, `0xFF` om ingen |
| 2–5 | timestamp | epoch | `0` om klockan inte var satt |
| 6–7 | bac | mbac | |
| 8–9 | tempC10 | i16 | Vid mättillfället |
| 10–11 | rhPct10 | u16 | Vid mättillfället |
| 12 | flags | u8 | bit0=calibration_valid, bit1=temp_in_range, bit2=blow_quality_ok |
| 13 | reserved | u8 | `0x00` |

### 6.4 `trigger` — write
UUID: `4e522004-…`
Payload (1 byte):

| Värde | Kommando |
|---|---|
| `0x01` | Starta mätning (om state==Ready, annars ignoreras) |
| `0x02` | Avbryt pågående mätning, gå till Idle |

---

## 7. History Service (`4e523000-…`)

Pull-baserad åtkomst till lagrade mätningar (lagrade i NVS/LittleFS när appen inte är ansluten).

**Lagringsmodell:** Cirkulär buffer med plats för minst 100 mätningar. Index 0 = nyaste, `count-1` = äldsta.

### 7.1 `count` — read + notify
UUID: `4e523001-…`
Payload: `u16` antal lagrade mätningar.

Notify trigger: ny mätning sparas eller buffert rensas.

### 7.2 `read_index` — write
UUID: `4e523002-…`
Payload: `u16` index 0..count-1.

Appen skriver index för att välja vilken entry som ska exponeras via `entry`.

### 7.3 `entry` — read
UUID: `4e523003-…`
Payload: samma 14-byte-format som `Measurement.result`.

Returnerar entry vid `read_index`. Om index är out-of-bounds returneras alla nollor.

> **Pagineringsmönster:** `read count → for i in 0..count-1: write read_index=i; read entry`. Inte snabbt för stora buffertar men enkelt och pålitligt. Kan utökas i v2 med en `entry_batch`-characteristic om det blir bottleneck.

---

## 8. User Service (`4e524000-…`)

Max **8 användare** på enheten (user_id 0–7). `0xFF` reserverat för "ingen aktiv".

### 8.1 `active_user` — read + write + notify
UUID: `4e524001-…`
Payload: `u8` user_id (0–7 eller `0xFF`).

Write byter aktiv användare. Notify:as om aktiv användare ändras lokalt på enheten (via meny — funktion kommer i framtida UI-uppdatering).

### 8.2 `user_count` — read
UUID: `4e524002-…`
Payload: `u8` antal definierade användare (0–8).

### 8.3 `user_index` — write
UUID: `4e524003-…`
Payload: `u8` user_id som ska läsas/skrivas via `user_record`.

### 8.4 `user_record` — read + write
UUID: `4e524004-…`
Payload (23 byte):

| Offset | Fält | Typ | Notes |
|---|---|---|---|
| 0 | version | u8 | `0x01` |
| 1 | user_id | u8 | Måste matcha senast skrivet `user_index` |
| 2 | flags | u8 | bit0=valid, bit1=is_active |
| 3–22 | name | char[20] | UTF-8, null-padded |

**Read:** returnerar record för senast skrivna `user_index`. Om slot är tom: flags=0, name=alla nollor.

**Write:**
- `flags.valid=1` → skapa eller uppdatera användare (name + ev. is_active).
- `flags.valid=0` → radera användaren i den sloten.

Värdena persisteras i NVS direkt.

---

## 9. Time Service (`4e525000-…`)

Soft clock på enheten. Klockan persisteras i RTC memory över deep sleep men förlorar tid efter strömlöst läge.

### 9.1 `clock` — read + write
UUID: `4e525001-…`
Payload (5 byte):

| Offset | Fält | Typ | Notes |
|---|---|---|---|
| 0 | version | u8 | `0x01` |
| 1–4 | epoch | epoch | `0` = klockan ej satt |

**Read:** returnerar aktuell device-tid.
**Write:** sätter device-tid. Appen bör skriva på varje connection direkt efter pairing, och igen vid behov (t.ex. om appen själv synkat mot NTP).

**Beteende när klockan är `0`:** nya mätningar får `timestamp=0` i sin `result`. Appen ska behandla `timestamp=0` som "okänd tid" och själv tagga med ungefärlig tid (eller dölja entries med `timestamp=0` från statistik som kräver tid).

---

## 10. Settings Service (`4e526000-…`)

Speglar `ctx.settings` i firmware. Speglar exakt fälten i `/settings.json`.

### 10.1 `settings_blob` — read + write + notify
UUID: `4e526001-…`
Payload (10 byte):

| Offset | Fält | Typ | Range | Notes |
|---|---|---|---|---|
| 0 | version | u8 | — | `0x01` |
| 1 | led_level | u8 | 0–10 | NeoPixel-ljusstyrka |
| 2 | buzzer_level | u8 | 0–10 | |
| 3 | vibration_level | u8 | 0–10 | |
| 4 | sensor_sens | u8 | 0–25 | MQ303B-kallibreringskänslighet |
| 5 | sleep_timeout_30s | u8 | 0–30 | Steg om 30 s (0=off, 30=15 min) |
| 6 | connection_mode | u8 | 0–2 | 0=Off, 1=BLE, 2=WiFi |
| 7 | flags | u8 | — | bit0=changes_pending_save |
| 8–9 | reserved | u8[2] | — | `0x00 0x00` |

**Read:** aktuella sparade inställningar.
**Write:** uppdaterar och sparar omedelbart till `/settings.json`.
**Notify:** triggas när inställningar ändras lokalt via enhetsmenyn, så appen håller sig synkad.

> **Notera:** `connection_mode=1` (BLE) får aldrig sättas via BLE (skulle skjuta sig själv i foten). `connection_mode=2` (WiFi) accepteras och utlöser att enheten startar om i WiFi-läge — appen kommer att tappa anslutningen. Bekräfta gärna med användaren i appen innan.

---

## 11. Calibration Service (`4e527000-…`)

Device-wide kalibrering (ingen per-användare-offset i v1).

### 11.1 `cal_status` — read + notify
UUID: `4e527001-…`
Payload (7 byte):

| Offset | Fält | Typ | Notes |
|---|---|---|---|
| 0 | version | u8 | `0x01` |
| 1 | state | u8 | 0=Idle, 1=Running, 2=Complete, 3=Failed |
| 2–5 | last_cal_epoch | epoch | När senaste lyckade kalibrering kördes |
| 6 | progress_pct | u8 | 0–100 under Running |

Notify trigger: state byte ändras eller progress ökar med ≥10%.

### 11.2 `cal_control` — write
UUID: `4e527002-…`
Payload (1 byte):

| Värde | Kommando |
|---|---|
| `0x01` | Starta kalibrering (kräver att enheten är i Idle) |
| `0x02` | Avbryt pågående kalibrering |
| `0xFF` | Återställ till fabrikskalibrering |

---

## 12. OTA Service (`4e528000-…`) — placeholder

UUID:er reserverade men inte specificerade i v1. Tills BLE-OTA implementeras kvarstår WiFi-OTA via Settings → FW Update (se `ARCHITECTURE.md` §6).

Förväntade characteristics när det specificeras:
- `ota_control` (write + notify) — start/end/abort, totalstorlek, SHA256, status
- `ota_data` (write without response) — firmware-chunks
- `ota_progress` (notify) — byte-räknare + procent

---

## 13. Mätflöde — typisk sekvens

För referens, normalt flöde sett från BLE-sidan:

```
1.  App: subscribe(Measurement.state, Measurement.live_data,
                   Measurement.result, Power.power_status,
                   Battery.level, History.count, Settings.settings_blob,
                   User.active_user)
2.  App: read(DeviceInformation.*)            — cacheas
3.  App: read/write(Time.clock)               — synka tid
4.  App: read(User.user_count)
        write(User.user_index = 0..N); read(User.user_record)  — bygg lista
5.  App: read(Measurement.result)             — visa senaste värde direkt
6.  App: read(History.count); paginera om större än lokalt cache

Mätningsflöde:
7.  Enhet: notify(state = Warmup)
   Enhet: notify(live_data, type=0x01)         — 5 Hz under uppvärmning
8.  Enhet: notify(state = Ready)
9.  Användaren börjar blåsa (detekterat via mic):
   Enhet: notify(state = Blowing)
   Enhet: notify(live_data, type=0x02)         — 20 Hz under blåsning
10. Enhet: notify(state = Measuring)
11. Enhet: notify(state = Result)
   Enhet: notify(result, …)
   Enhet: notify(history.count++)
```

---

## 14. Öppna frågor och framtida arbete

- **OTA-protokoll:** behöver specificeras innan v1.0-release om vi vill ha BLE-OTA.
- **Batch-läsning av history:** om paginering blir för långsam, lägg till `entry_batch` som returnerar flera entries per read (begränsat av MTU).
- **Notify-rate-throttling:** verifiera att 20 Hz `live_data` inte överbelastar mobil-BLE-stacken på äldre Android-telefoner.
- **Tidssynk-drift:** RTC-klockan på ESP32-S3 driver ~20 ppm. Appen bör skriva `Time.clock` minst en gång per dygn för att hålla tidsstämplar rimliga.
- **Kalibrerings-UX:** den faktiska kalibreringsproceduren (vad användaren ska göra med enheten) behöver specificeras separat. BLE-schemat har bara start/stop/status.
- **Connection-loss-recovery:** vad händer om appen tappar anslutning mitt under en mätning? Förslag: enheten slutför mätningen och sparar i history, app pullar nästa gång.
- **Fabriksåterställning via BLE:** ska detta finnas, eller kvarstår det som lokal-only via Settings → Reset?

---

## 15. UUID-referens (sammanfattning för kod-konstanter)

```cpp
// Custom services
constexpr const char* kSvcPower       = "4e521000-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kSvcMeasurement = "4e522000-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kSvcHistory     = "4e523000-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kSvcUser        = "4e524000-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kSvcTime        = "4e525000-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kSvcSettings    = "4e526000-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kSvcCalibration = "4e527000-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kSvcOta         = "4e528000-9b1f-4b3e-8a7c-3a4f6d8e9c01";

// Power
constexpr const char* kChrPowerStatus = "4e521001-9b1f-4b3e-8a7c-3a4f6d8e9c01";

// Measurement
constexpr const char* kChrMeasState   = "4e522001-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kChrMeasLive    = "4e522002-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kChrMeasResult  = "4e522003-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kChrMeasTrigger = "4e522004-9b1f-4b3e-8a7c-3a4f6d8e9c01";

// History
constexpr const char* kChrHistCount   = "4e523001-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kChrHistIndex   = "4e523002-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kChrHistEntry   = "4e523003-9b1f-4b3e-8a7c-3a4f6d8e9c01";

// User
constexpr const char* kChrUserActive  = "4e524001-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kChrUserCount   = "4e524002-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kChrUserIndex   = "4e524003-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kChrUserRecord  = "4e524004-9b1f-4b3e-8a7c-3a4f6d8e9c01";

// Time
constexpr const char* kChrTimeClock   = "4e525001-9b1f-4b3e-8a7c-3a4f6d8e9c01";

// Settings
constexpr const char* kChrSettingsBlob = "4e526001-9b1f-4b3e-8a7c-3a4f6d8e9c01";

// Calibration
constexpr const char* kChrCalStatus   = "4e527001-9b1f-4b3e-8a7c-3a4f6d8e9c01";
constexpr const char* kChrCalControl  = "4e527002-9b1f-4b3e-8a7c-3a4f6d8e9c01";
```
