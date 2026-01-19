#pragma once

#include <Arduino.h>

#include "actuators.h"

class MeasurementController {
public:
  enum class Phase : uint8_t {
    Idle,
    Heating,
    Blow,
    Retry,
    Done,
  };

  MeasurementController(uint8_t heaterPin, uint8_t heaterChannel, uint8_t micPin,
                        uint8_t alcPin, uint32_t heaterPwmHz = 2000,
                        bool heaterActiveHigh = true);

  void begin();
  void start(uint32_t nowMs);
  void stop();
  void update(uint32_t nowMs, Buzzer &buzzer);

  void setHeaterPercent(uint8_t percent);
  void setHeatingMs(uint32_t durationMs);
  void setMicThreshold(uint16_t threshold);
  void setBlowHoldMs(uint32_t durationMs);
  void setMicGraceMs(uint32_t durationMs);
  void setRetryDisplayMs(uint32_t durationMs);
  void setBuzzerStrength(uint8_t percent);

  Phase phase() const;
  bool isActive() const;
  bool isRetry() const;
  bool isBlowPhase() const;
  bool isComplete() const;
  uint32_t heatingRemainingMs(uint32_t nowMs) const;
  int lastMicValue() const;
  int alcValue() const;

private:
  void writeHeater(bool enabled);

  uint8_t heaterPin_;
  uint8_t heaterChannel_;
  uint32_t heaterPwmHz_;
  bool heaterActiveHigh_;
  uint8_t micPin_;
  uint8_t alcPin_;

  uint8_t heaterPercent_ = 75;
  uint16_t micThreshold_ = 2000;
  uint32_t heatingMs_ = 20000;
  uint32_t blowHoldMs_ = 5000;
  uint32_t micGraceMs_ = 300;
  uint32_t retryDisplayMs_ = 1500;
  uint8_t buzzerStrength_ = 50;

  Phase phase_ = Phase::Idle;
  uint32_t phaseStartMs_ = 0;
  uint32_t blowStartMs_ = 0;
  uint32_t lastAboveMs_ = 0;
  uint32_t retryUntilMs_ = 0;
  bool blowing_ = false;
  int lastMicValue_ = 0;
  int alcValue_ = 0;
};
