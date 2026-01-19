#include "measurement.h"

namespace {
constexpr uint8_t kPwmResolutionBits = 8;
constexpr uint32_t kPwmMaxDuty = (1u << kPwmResolutionBits) - 1u;
uint8_t clampPercent(uint8_t percent) {
  return percent > 100 ? 100 : percent;
}
}  // namespace

MeasurementController::MeasurementController(uint8_t heaterPin, uint8_t heaterChannel,
                                             uint8_t micPin, uint8_t alcPin,
                                             uint32_t heaterPwmHz,
                                             bool heaterActiveHigh)
    : heaterPin_(heaterPin),
      heaterChannel_(heaterChannel),
      heaterPwmHz_(heaterPwmHz),
      heaterActiveHigh_(heaterActiveHigh),
      micPin_(micPin),
      alcPin_(alcPin),
      micThreshold_(1200),
      heatingMs_(20000),
      blowHoldMs_(1000),
      micGraceMs_(400),
      retryDisplayMs_(1500),
      buzzerStrength_(80),
      phase_(Phase::Idle),
      phaseStartMs_(0),
      blowStartMs_(0),
      lastAboveMs_(0),
      retryUntilMs_(0),
      blowing_(false),
      lastMicValue_(0),
      alcValue_(0) {
}

void MeasurementController::begin() {
  ledcSetup(heaterChannel_, heaterPwmHz_, kPwmResolutionBits);
  ledcAttachPin(heaterPin_, heaterChannel_);
  writeHeater(false);
}

void MeasurementController::start(uint32_t nowMs) {
  phase_ = Phase::Heating;
  phaseStartMs_ = nowMs;
  blowStartMs_ = 0;
  lastAboveMs_ = 0;
  retryUntilMs_ = 0;
  blowing_ = false;
  alcValue_ = 0;
  writeHeater(true);
}

void MeasurementController::stop() {
  phase_ = Phase::Idle;
  writeHeater(false);
}

void MeasurementController::update(uint32_t nowMs, Buzzer &buzzer) {
  if (phase_ == Phase::Idle || phase_ == Phase::Done) {
    return;
  }

  if (phase_ == Phase::Heating) {
    if (nowMs - phaseStartMs_ >= heatingMs_) {
      phase_ = Phase::Blow;
      phaseStartMs_ = nowMs;
      blowing_ = false;
      lastAboveMs_ = 0;
    }
    return;
  }

  if (phase_ == Phase::Retry) {
    if (nowMs >= retryUntilMs_) {
      phase_ = Phase::Blow;
      phaseStartMs_ = nowMs;
      blowing_ = false;
      lastAboveMs_ = 0;
    }
    return;
  }

  if (phase_ == Phase::Blow) {
    lastMicValue_ = analogRead(micPin_);
    const bool above = lastMicValue_ >= static_cast<int>(micThreshold_);

    if (above) {
      if (!blowing_) {
        blowing_ = true;
        blowStartMs_ = nowMs;
      }
      lastAboveMs_ = nowMs;
      buzzer.on(buzzerStrength_);

      if (nowMs - blowStartMs_ >= blowHoldMs_) {
        alcValue_ = analogRead(alcPin_);
        buzzer.off();
        phase_ = Phase::Done;
        writeHeater(false);
      }
    } else {
      buzzer.off();
      if (blowing_ && (nowMs - lastAboveMs_ > micGraceMs_)) {
        phase_ = Phase::Retry;
        retryUntilMs_ = nowMs + retryDisplayMs_;
        blowing_ = false;
      }
    }
  }
}

void MeasurementController::setHeaterPercent(uint8_t percent) {
  heaterPercent_ = clampPercent(percent);
  if (phase_ != Phase::Idle && phase_ != Phase::Done) {
    writeHeater(true);
  }
}

void MeasurementController::setHeatingMs(uint32_t durationMs) {
  heatingMs_ = durationMs;
}

void MeasurementController::setMicThreshold(uint16_t threshold) {
  micThreshold_ = threshold;
}

void MeasurementController::setBlowHoldMs(uint32_t durationMs) {
  blowHoldMs_ = durationMs;
}

void MeasurementController::setMicGraceMs(uint32_t durationMs) {
  micGraceMs_ = durationMs;
}

void MeasurementController::setRetryDisplayMs(uint32_t durationMs) {
  retryDisplayMs_ = durationMs;
}

void MeasurementController::setBuzzerStrength(uint8_t percent) {
  buzzerStrength_ = clampPercent(percent);
}

MeasurementController::Phase MeasurementController::phase() const {
  return phase_;
}

bool MeasurementController::isActive() const {
  return phase_ != Phase::Idle && phase_ != Phase::Done;
}

bool MeasurementController::isRetry() const {
  return phase_ == Phase::Retry;
}

bool MeasurementController::isBlowPhase() const {
  return phase_ == Phase::Blow;
}

bool MeasurementController::isComplete() const {
  return phase_ == Phase::Done;
}

uint32_t MeasurementController::heatingRemainingMs(uint32_t nowMs) const {
  if (phase_ != Phase::Heating) {
    return 0;
  }
  const uint32_t elapsed = nowMs - phaseStartMs_;
  return elapsed >= heatingMs_ ? 0 : (heatingMs_ - elapsed);
}

int MeasurementController::lastMicValue() const {
  return lastMicValue_;
}

int MeasurementController::alcValue() const {
  return alcValue_;
}

void MeasurementController::writeHeater(bool enabled) {
  uint32_t duty = enabled
                      ? (static_cast<uint32_t>(heaterPercent_) * kPwmMaxDuty) / 100u
                      : 0u;
  if (!heaterActiveHigh_) {
    duty = kPwmMaxDuty - duty;
  }
  ledcWrite(heaterChannel_, duty);
}
