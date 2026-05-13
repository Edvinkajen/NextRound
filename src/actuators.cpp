#include "actuators.h"

namespace {
constexpr uint8_t  kPwmResolutionBits = 8;
constexpr uint32_t kPwmMaxDuty        = (1u << kPwmResolutionBits) - 1u;

uint8_t clamp100(uint8_t v) { return v > 100 ? 100 : v; }
}  // namespace

// ===========================================================================
// PwmActuator
// ===========================================================================

uint8_t PwmActuator::levelToPercent(uint8_t level) {
  if (level == 0) return 0;
  const uint8_t clamped = level > 10 ? 10 : level;
  // Linear: level 1 → 25 %, level 10 → 100 %
  return static_cast<uint8_t>(25u + (static_cast<uint32_t>(clamped - 1) * 75u) / 9u);
}

uint8_t PwmActuator::scalePercent(uint8_t base, uint8_t cmd) {
  return static_cast<uint8_t>((static_cast<uint32_t>(base) * cmd) / 100u);
}

void PwmActuator::pwmBegin() {
  ledcSetup(channel_, pwmHz_, kPwmResolutionBits);
  ledcAttachPin(pin_, channel_);
  writePercent(false);
}

void PwmActuator::writePercent(bool enabled) {
  const uint8_t eff = overrideLevel_
      ? scalePercent(baseLevelPercent_, commandLevelPercent_)
      : baseLevelPercent_;
  uint32_t duty = enabled
      ? (static_cast<uint32_t>(eff) * kPwmMaxDuty) / 100u
      : 0u;
  if (!activeHigh_) duty = kPwmMaxDuty - duty;
  ledcWrite(channel_, duty);
  outputOn_ = enabled && eff > 0;
}

// ===========================================================================
// VibrationMotor
// ===========================================================================

VibrationMotor::VibrationMotor(uint8_t pin, uint8_t channel, uint32_t pwmHz,
                               bool activeHigh)
    : PwmActuator(pin, channel, pwmHz, activeHigh) {}

void VibrationMotor::begin() { pwmBegin(); }

void VibrationMotor::update(uint32_t nowMs) {
  switch (mode_) {
    case Mode::TimedOn:
      if (static_cast<int32_t>(nowMs - nextChangeMs_) >= 0) off();
      break;

    case Mode::PulsingOn:
      if (static_cast<int32_t>(nowMs - nextChangeMs_) >= 0) {
        writePercent(false);
        mode_         = Mode::PulsingOff;
        nextChangeMs_ = nowMs + pulseOffMs_;
      }
      break;

    case Mode::PulsingOff:
      if (static_cast<int32_t>(nowMs - nextChangeMs_) >= 0) {
        if (remainingPulses_ > 0) --remainingPulses_;
        if (remainingPulses_ == 0) {
          mode_          = Mode::Idle;
          overrideLevel_ = false;
          writePercent(false);
        } else {
          writePercent(true);
          mode_         = Mode::PulsingOn;
          nextChangeMs_ = nowMs + pulseOnMs_;
        }
      }
      break;

    default: break;
  }
}

void VibrationMotor::setLevel(uint8_t level) {
  baseLevelPercent_ = levelToPercent(level);
  bool enabled;
  if (mode_ == Mode::PulsingOff) {
    enabled = false;
  } else if (mode_ == Mode::Idle) {
    enabled = outputOn_;
  } else {
    enabled = true;
  }
  writePercent(enabled);
}

void VibrationMotor::on() {
  overrideLevel_ = false;
  mode_          = Mode::Idle;
  writePercent(true);
}

void VibrationMotor::on(uint8_t strengthPercent) {
  overrideLevel_       = true;
  commandLevelPercent_ = clamp100(strengthPercent);
  mode_                = Mode::Idle;
  writePercent(true);
}

void VibrationMotor::off() {
  overrideLevel_ = false;
  mode_          = Mode::Idle;
  writePercent(false);
}

void VibrationMotor::onFor(uint32_t nowMs, uint32_t durationMs) {
  if (durationMs == 0) { off(); return; }
  overrideLevel_ = false;
  writePercent(true);
  mode_         = Mode::TimedOn;
  nextChangeMs_ = nowMs + durationMs;
}

void VibrationMotor::onFor(uint32_t nowMs, uint32_t durationMs,
                           uint8_t strengthPercent) {
  if (durationMs == 0) { off(); return; }
  overrideLevel_       = true;
  commandLevelPercent_ = clamp100(strengthPercent);
  writePercent(true);
  mode_         = Mode::TimedOn;
  nextChangeMs_ = nowMs + durationMs;
}

void VibrationMotor::pulse(uint32_t nowMs, uint8_t count,
                           uint32_t onMs, uint32_t offMs) {
  if (count == 0 || onMs == 0) { off(); return; }
  overrideLevel_   = false;
  pulseOnMs_       = onMs;
  pulseOffMs_      = offMs;
  remainingPulses_ = count;
  writePercent(true);
  mode_         = Mode::PulsingOn;
  nextChangeMs_ = nowMs + pulseOnMs_;
}

void VibrationMotor::pulse(uint32_t nowMs, uint8_t count,
                           uint32_t onMs, uint32_t offMs,
                           uint8_t strengthPercent) {
  if (count == 0 || onMs == 0) { off(); return; }
  overrideLevel_       = true;
  commandLevelPercent_ = clamp100(strengthPercent);
  pulseOnMs_           = onMs;
  pulseOffMs_          = offMs;
  remainingPulses_     = count;
  writePercent(true);
  mode_         = Mode::PulsingOn;
  nextChangeMs_ = nowMs + pulseOnMs_;
}

bool VibrationMotor::isActive() const { return outputOn_; }

// ===========================================================================
// Buzzer
// ===========================================================================

Buzzer::Buzzer(uint8_t pin, uint8_t channel, uint32_t pwmHz, bool activeHigh)
    : PwmActuator(pin, channel, pwmHz, activeHigh) {}

void Buzzer::begin() { pwmBegin(); }

void Buzzer::update(uint32_t nowMs) {
  if (mode_ == Mode::TimedOn || mode_ == Mode::TimedTone) {
    if (static_cast<int32_t>(nowMs - endMs_) >= 0) off();
  }
}

void Buzzer::setLevel(uint8_t level) {
  baseLevelPercent_ = levelToPercent(level);
  writePercent(mode_ != Mode::Idle);
}

void Buzzer::on() {
  overrideLevel_ = false;
  mode_          = Mode::ContinuousOn;
  writePercent(true);
}

void Buzzer::on(uint8_t strengthPercent) {
  overrideLevel_       = true;
  commandLevelPercent_ = clamp100(strengthPercent);
  mode_                = Mode::ContinuousOn;
  writePercent(true);
}

void Buzzer::off() {
  const bool wasTone = (mode_ == Mode::TimedTone || mode_ == Mode::ContinuousTone);
  overrideLevel_ = false;
  mode_          = Mode::Idle;
  writePercent(false);
  if (wasTone) {
    // ledcWriteTone changed the channel's timer frequency; restore the configured frequency.
    ledcSetup(channel_, pwmHz_, kPwmResolutionBits);
  }
}

void Buzzer::onFor(uint32_t nowMs, uint32_t durationMs) {
  if (durationMs == 0) { off(); return; }
  overrideLevel_ = false;
  writePercent(true);
  mode_ = Mode::TimedOn;
  endMs_ = nowMs + durationMs;
}

void Buzzer::onFor(uint32_t nowMs, uint32_t durationMs, uint8_t strengthPercent) {
  if (durationMs == 0) { off(); return; }
  overrideLevel_       = true;
  commandLevelPercent_ = clamp100(strengthPercent);
  writePercent(true);
  mode_  = Mode::TimedOn;
  endMs_ = nowMs + durationMs;
}

void Buzzer::playTone(uint32_t nowMs, uint16_t frequencyHz, uint32_t durationMs) {
  if (frequencyHz == 0) { off(); return; }
  overrideLevel_ = false;
  ledcWriteTone(channel_, frequencyHz);
  writePercent(true);
  if (durationMs == 0) {
    mode_ = Mode::ContinuousTone;
  } else {
    mode_  = Mode::TimedTone;
    endMs_ = nowMs + durationMs;
  }
}

void Buzzer::playTone(uint32_t nowMs, uint16_t frequencyHz, uint32_t durationMs,
                      uint8_t strengthPercent) {
  if (frequencyHz == 0) { off(); return; }
  overrideLevel_       = true;
  commandLevelPercent_ = clamp100(strengthPercent);
  ledcWriteTone(channel_, frequencyHz);
  writePercent(true);
  if (durationMs == 0) {
    mode_ = Mode::ContinuousTone;
  } else {
    mode_  = Mode::TimedTone;
    endMs_ = nowMs + durationMs;
  }
}

bool Buzzer::isActive() const { return outputOn_; }

// ===========================================================================
// NeopixelLed
// ===========================================================================

NeopixelLed::NeopixelLed(uint8_t pin, uint16_t count, neoPixelType type)
    : pixels_(count, pin, type) {}

void NeopixelLed::begin() {
  pixels_.begin();
  pixels_.show();
}

void NeopixelLed::setLevel(uint8_t level) {
  level_ = level > 10 ? 10 : level;
  apply();
}

void NeopixelLed::onColor(uint8_t r, uint8_t g, uint8_t b) {
  r_ = r; g_ = g; b_ = b;
  on_ = true;
  apply();
}

void NeopixelLed::onColor(uint32_t hexColor) {
  onColor(static_cast<uint8_t>((hexColor >> 16) & 0xFFu),
          static_cast<uint8_t>((hexColor >>  8) & 0xFFu),
          static_cast<uint8_t>( hexColor        & 0xFFu));
}

void NeopixelLed::off() {
  on_ = false;
  apply();
}

bool NeopixelLed::isActive() const { return on_ && level_ > 0; }

void NeopixelLed::apply() {
  pixels_.setBrightness(static_cast<uint8_t>((level_ * 255u) / 10u));
  if (on_ && level_ > 0) {
    const uint32_t color = pixels_.Color(r_, g_, b_);
    for (uint16_t i = 0; i < pixels_.numPixels(); ++i)
      pixels_.setPixelColor(i, color);
  } else {
    pixels_.clear();
  }
  pixels_.show();
}
