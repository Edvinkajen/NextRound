#include "BatteryManager.h"

namespace {

constexpr uint8_t  kAddr   = 0x6A;
constexpr uint8_t  kIntPin = 11;

// Register addresses
constexpr uint8_t kRegAdcCtrl  = 0x02;
constexpr uint8_t kRegChgCtrl1 = 0x03;
constexpr uint8_t kRegIchg     = 0x04;
constexpr uint8_t kRegIprechg  = 0x05;
constexpr uint8_t kRegVreg     = 0x06;
constexpr uint8_t kRegTimer    = 0x07;
constexpr uint8_t kRegStatus   = 0x0B;
constexpr uint8_t kRegFault    = 0x0C;
constexpr uint8_t kRegBatv     = 0x0E;
constexpr uint8_t kRegDevInfo  = 0x14;

// Bit masks
constexpr uint8_t kMaskConvRate  = 0x80;  // REG02[7] continuous ADC
constexpr uint8_t kMaskConvStart = 0x40;  // REG02[6] start conversion
constexpr uint8_t kMaskChgCfg   = 0x10;  // REG03[4] charge enable
constexpr uint8_t kMaskIchg     = 0x7F;  // REG04[6:0]
constexpr uint8_t kMaskVreg     = 0xFC;  // REG06[7:2]
constexpr uint8_t kMaskWatchdog = 0x30;  // REG07[5:4]
constexpr uint8_t kMaskRegRst   = 0x80;  // REG14[7] soft reset

// REG0B charge status bits [4:3]
constexpr uint8_t kMaskVbusStat = 0xC0;
constexpr uint8_t kMaskChgStat  = 0x18;
constexpr uint8_t kChgPre       = 0x08;
constexpr uint8_t kChgFast      = 0x10;
constexpr uint8_t kChgDone      = 0x18;

// Threshold voltages (mV)
constexpr uint16_t kLowMv      = 3300;
constexpr uint16_t kCriticalMv = 3000;

// SoC lookup — linear interpolation between points (no-load Li-ion typical)
struct SocPoint { uint16_t mv; uint8_t pct; };
constexpr SocPoint kSocTable[] = {
  { 4200, 100 },
  { 4060,  85 },
  { 3920,  70 },
  { 3800,  55 },
  { 3720,  40 },
  { 3660,  25 },
  { 3500,  10 },
  { 3300,   0 },
};
constexpr uint8_t kSocPoints = sizeof(kSocTable) / sizeof(kSocTable[0]);

}  // namespace

// ---------------------------------------------------------------------------

BatteryManager* BatteryManager::instance_ = nullptr;

void IRAM_ATTR BatteryManager::intIsr() {
  if (instance_ && instance_->taskHandle_) {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(instance_->taskHandle_, &woken);
    portYIELD_FROM_ISR(woken);
  }
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

bool BatteryManager::begin(TwoWire& wire) {
  wire_     = &wire;
  instance_ = this;

  // 1. Soft reset
  writeRegister(kRegDevInfo, kMaskRegRst);
  delay(100);

  // 2. Disable I2C watchdog — CRITICAL: verify read-back before proceeding
  if (!verifyWrite(kRegTimer, kMaskWatchdog, 0x00, "watchdog disable")) {
    log_e("[BMS] ABORT: watchdog disable failed — chip registers unsafe");
    return false;
  }

  // 3. Charge current 448 mA: REG04[6:0] = 0x07 (7 × 64 mA = 448 mA)
  verifyWrite(kRegIchg, kMaskIchg, 0x07, "ICHG=448mA");

  // 4. Charge voltage 4.208 V: VREG[7:2] = 0x17 → (0x17 << 2) = 0x5C
  //    Formula: 3840 + VREG × 16 mV → (4208 - 3840) / 16 = 23 = 0x17
  if (!updateBits(kRegVreg, kMaskVreg, static_cast<uint8_t>(0x17 << 2))) {
    log_e("[BMS] VREG write failed");
  }

  // 5. Precharge / termination current: REG05 default 64 mA each — write 0x00
  writeRegister(kRegIprechg, 0x00);

  // 6. Enable charging
  if (!updateBits(kRegChgCtrl1, kMaskChgCfg, kMaskChgCfg)) {
    log_e("[BMS] charge enable failed");
  }

  // 7. Enable ADC continuous mode (~1 Hz auto-conversion)
  if (!updateBits(kRegAdcCtrl, kMaskConvRate | kMaskConvStart,
                                kMaskConvRate | kMaskConvStart)) {
    log_e("[BMS] ADC continuous mode failed");
  }

  // 8. Configure INT pin and attach interrupt (active-low, external pull-up)
  pinMode(kIntPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(kIntPin), intIsr, FALLING);

  // Mutex protects Wire from concurrent access between monitor task and display.
  wireMutex_ = xSemaphoreCreateMutex();

  // Start monitor task on APP_CPU, priority 3, 4 KB stack
  xTaskCreatePinnedToCore(monitorTask, "bms_mon", 4096, this, 3, &taskHandle_, 1);

  log_i("[BMS] begin OK");
  return true;
}

void BatteryManager::onEvent(EventCallback cb) {
  callback_ = cb;
}

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

uint16_t             BatteryManager::voltageMv()         const { return voltageMv_; }
uint8_t              BatteryManager::socPercent()        const { return socPct_; }
BatteryManager::State BatteryManager::state()            const { return state_; }
bool                 BatteryManager::isCharging()        const { return state_ == State::PreCharge || state_ == State::FastCharge; }
bool                 BatteryManager::isVbusPresent()     const { return vbusPresent_; }
bool                 BatteryManager::hasFault()          const { return faultReg_ != 0; }
uint8_t              BatteryManager::lastFaultRegister() const { return faultReg_; }

// ---------------------------------------------------------------------------
// Monitor task
// ---------------------------------------------------------------------------

void BatteryManager::monitorTask(void* param) {
  BatteryManager* self = static_cast<BatteryManager*>(param);
  // Wait for ADC's first conversion to complete (~1 Hz in continuous mode).
  // Also clears any spurious INT notifications from chip initialization.
  vTaskDelay(pdMS_TO_TICKS(1500));
  for (;;) {
    // Block up to 1 s; INT fires early on any charger event
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    self->poll();
  }
}

// ---------------------------------------------------------------------------
// Polling (called from monitor task only)
// ---------------------------------------------------------------------------

void BatteryManager::poll() {
  uint8_t regStatus = 0, regFaultA = 0, regFaultB = 0, regBatv = 0;

  // Hold the Wire mutex for the entire I2C burst so display sendBuffer()
  // on the main task cannot interleave with our register reads.
  if (xSemaphoreTake(wireMutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
    log_w("[BMS] Wire busy, skipping poll");
    return;
  }

  const bool ok =
    readRegister(kRegStatus, regStatus) &&
    (readRegister(kRegFault, regFaultA), readRegister(kRegFault, regFaultB)) &&
    readRegister(kRegBatv, regBatv);

  xSemaphoreGive(wireMutex_);

  if (!ok) {
    log_e("[BMS] I2C read failed");
    return;
  }

  // Decode voltage: 2304 mV + code × 20 mV
  const uint16_t mv = 2304u + static_cast<uint16_t>(regBatv & 0x7F) * 20u;

  // Decode charge state from REG0B[4:3]
  const uint8_t chgStat = regStatus & kMaskChgStat;
  State newState = State::Discharging;
  if (regFaultB != 0) {
    newState = State::Fault;
  } else if (chgStat == kChgPre) {
    newState = State::PreCharge;
  } else if (chgStat == kChgFast) {
    newState = State::FastCharge;
  } else if (chgStat == kChgDone) {
    newState = State::ChargeComplete;
  }

  const State   oldState = state_;
  const bool    vbus     = (regStatus & kMaskVbusStat) != 0;
  const uint8_t newSoc   = calcSoc(mv);

  voltageMv_   = mv;
  socPct_      = newSoc;
  state_       = newState;
  vbusPresent_ = vbus;

  // Fault event — only when fault byte changes to non-zero
  if (regFaultB != 0 && regFaultB != faultReg_) {
    faultReg_ = regFaultB;
    log_w("[BMS] Fault REG0C=0x%02X  WD=%d BOOST=%d CHRG=%d BAT=%d NTC=%d",
          regFaultB,
          (regFaultB >> 7) & 1,
          (regFaultB >> 6) & 1,
          (regFaultB >> 4) & 3,
          (regFaultB >> 3) & 1,
           regFaultB & 7);
    emit(Event::Fault);
  } else if (regFaultB == 0) {
    faultReg_ = 0;
  }

  if (newState != oldState) emit(Event::StateChanged);

  emit(Event::VoltageUpdate);

  // Low / Critical — edge-only (fire once when crossing down, reset when recovered)
  if (mv < kCriticalMv && !criticalEmitted_) {
    criticalEmitted_ = true;
    lowEmitted_      = true;
    emit(Event::Critical);
  } else if (mv < kLowMv && !lowEmitted_) {
    lowEmitted_ = true;
    emit(Event::Low);
  } else if (mv >= kLowMv) {
    lowEmitted_      = false;
    criticalEmitted_ = false;
  }
}

void BatteryManager::emit(Event evt) {
  if (callback_) callback_(evt, *this);
}

// ---------------------------------------------------------------------------
// SoC — linear interpolation on lookup table
// ---------------------------------------------------------------------------

uint8_t BatteryManager::calcSoc(uint16_t mv) const {
  if (mv >= kSocTable[0].mv)              return 100;
  if (mv <= kSocTable[kSocPoints - 1].mv) return 0;

  for (uint8_t i = 0; i < kSocPoints - 1; ++i) {
    if (mv <= kSocTable[i].mv && mv >= kSocTable[i + 1].mv) {
      const uint16_t vRange  = kSocTable[i].mv  - kSocTable[i + 1].mv;
      const uint8_t  pRange  = kSocTable[i].pct - kSocTable[i + 1].pct;
      const uint16_t offset  = mv - kSocTable[i + 1].mv;
      return static_cast<uint8_t>(kSocTable[i + 1].pct +
             static_cast<uint8_t>((static_cast<uint32_t>(offset) * pRange + vRange / 2) / vRange));
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
// I2C helpers
// ---------------------------------------------------------------------------

bool BatteryManager::readRegister(uint8_t reg, uint8_t& value) {
  wire_->beginTransmission(kAddr);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) {
    log_e("[BMS] readReg 0x%02X: I2C error", reg);
    return false;
  }
  if (wire_->requestFrom(static_cast<int>(kAddr), 1) != 1) {
    log_e("[BMS] readReg 0x%02X: no data", reg);
    return false;
  }
  value = wire_->read();
  return true;
}

bool BatteryManager::writeRegister(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(kAddr);
  wire_->write(reg);
  wire_->write(value);
  if (wire_->endTransmission() != 0) {
    log_e("[BMS] writeReg 0x%02X: I2C error", reg);
    return false;
  }
  return true;
}

bool BatteryManager::updateBits(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t cur = 0;
  if (!readRegister(reg, cur)) return false;
  cur = static_cast<uint8_t>((cur & ~mask) | (value & mask));
  return writeRegister(reg, cur);
}

bool BatteryManager::verifyWrite(uint8_t reg, uint8_t mask, uint8_t expected, const char* label) {
  if (!updateBits(reg, mask, expected)) return false;
  uint8_t readback = 0;
  if (!readRegister(reg, readback)) return false;
  const bool ok = ((readback & mask) == (expected & mask));
  if (ok) {
    log_i("[BMS] %-20s OK  (REG 0x%02X = 0x%02X)", label, reg, readback);
  } else {
    log_e("[BMS] %-20s MISMATCH  got=0x%02X expected=0x%02X mask=0x%02X",
          label, readback, expected, mask);
  }
  return ok;
}
