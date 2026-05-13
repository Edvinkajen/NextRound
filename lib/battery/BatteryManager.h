#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// Input current limit written to REG00[5:0] during begin().
// Encode: (mA − 100) / 50. Must not exceed the hardware cap set by R_ILIM.
// Board: R_ILIM = 324 Ω → I_INMAX ≈ 1.10 A typ (988 mA – 1.20 A over K_ILIM tolerance).
constexpr uint16_t BQ25895_IINLIM_MA = 1000;

/*
 * BQ25895 default behaviours disabled in begin() (see SLUSC88C §8.4):
 *
 *   OTG_CONFIG  (REG03[5]) — cleared to 0: chip default is 1, which would
 *     allow boost from battery if the OTG pin floats high. No OTG function
 *     on this board.
 *
 *   HVDCP_EN    (REG02[3]) — cleared to 0: prevents negotiation of VBUS to
 *   MAXC_EN     (REG02[2])   9 V / 12 V via Qualcomm Quick Charge or
 *   AUTO_DPDM_EN(REG02[0])   Maxim protocols. IINLIM is set explicitly via
 *                             BQ25895_IINLIM_MA instead of the automatic
 *                             3.25 A that DCP detection would impose.
 *
 * Usage example:
 *
 *   BatteryManager battery;
 *   void setup() {
 *     Wire.begin(9, 10, 400000);
 *     battery.onEvent([](auto evt, auto& b) {
 *       if (evt == BatteryManager::Event::VoltageUpdate) {
 *         // update display
 *       }
 *     });
 *     battery.begin();
 *   }
 */
class BatteryManager {
 public:
  enum class State : uint8_t {
    Discharging,
    PreCharge,
    FastCharge,
    ChargeComplete,
    Fault,
  };

  enum class Event : uint8_t {
    StateChanged,
    VoltageUpdate,
    Fault,
    Low,      // VBAT < 3300 mV
    Critical, // VBAT < 3000 mV
  };

  using EventCallback = std::function<void(Event, const BatteryManager&)>;

  bool begin(TwoWire& wire = Wire);
  void onEvent(EventCallback cb);

  uint16_t          voltageMv()         const;
  uint8_t           socPercent()        const;
  State             state()             const;
  bool              isCharging()        const;
  bool              isVbusPresent()     const;
  bool              hasFault()          const;
  uint8_t           lastFaultRegister() const;

  // Shared Wire mutex — take this before any display.sendBuffer() call so
  // the BMS monitor task cannot preempt mid-I2C-transfer.
  SemaphoreHandle_t wireMutex()         const { return wireMutex_; }

 private:
  static void       monitorTask(void* param);
  static void IRAM_ATTR intIsr();

  bool readRegister (uint8_t reg, uint8_t& value);
  bool writeRegister(uint8_t reg, uint8_t  value);
  bool updateBits   (uint8_t reg, uint8_t mask, uint8_t value);
  bool verifyWrite  (uint8_t reg, uint8_t mask, uint8_t expected, const char* label);

  void    emit(Event evt);
  void    poll();
  uint8_t calcSoc(uint16_t mv) const;

  TwoWire*      wire_     = nullptr;
  EventCallback callback_;

  // Written only from monitorTask; uint8_t/bool reads are atomic on ESP32.
  volatile uint16_t voltageMv_    = 0;
  volatile uint8_t  socPct_       = 0;
  volatile State    state_        = State::Discharging;
  volatile bool     vbusPresent_  = false;
  volatile uint8_t  faultReg_     = 0;

  // Edge-detection flags — monitor task only, no sharing needed.
  bool lowEmitted_      = false;
  bool criticalEmitted_ = false;

  TaskHandle_t      taskHandle_ = nullptr;
  SemaphoreHandle_t wireMutex_  = nullptr;

  static BatteryManager* instance_;
};
