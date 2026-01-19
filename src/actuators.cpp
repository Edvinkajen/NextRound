#include "actuators.h"

namespace {
constexpr uint8_t kPwmResolutionBits = 8;
constexpr uint32_t kPwmMaxDuty = (1u << kPwmResolutionBits) - 1u;
uint8_t clampPercent(uint8_t percent) {
  return percent > 100 ? 100 : percent;
}
uint8_t levelToPercent(uint8_t level) {
  if (level == 0) {
    return 0;
  }
  if (level >= 10) {
    return 100;
  }
  const uint32_t scaled = 25u + (static_cast<uint32_t>(level - 1) * 75u + 4u) / 9u;
  return static_cast<uint8_t>(scaled);
}
uint8_t scalePercent(uint8_t basePercent, uint8_t commandPercent) {
  return static_cast<uint8_t>((static_cast<uint32_t>(basePercent) *
                               static_cast<uint32_t>(commandPercent)) /
                              100u);
}
}  // namespace

VibrationMotor::VibrationMotor(uint8_t pin, uint8_t channel, uint32_t pwmHz,
                               bool activeHigh)
    : pin_(pin),
      channel_(channel),
      pwmHz_(pwmHz),
      activeHigh_(activeHigh) {}

void VibrationMotor::begin() {
  ledcSetup(channel_, pwmHz_, kPwmResolutionBits);
  ledcAttachPin(pin_, channel_);
  write(false);
}

void VibrationMotor::update(uint32_t nowMs) {
  switch (mode_) {
    case Mode::TimedOn:
      if (static_cast<int32_t>(nowMs - nextChangeMs_) >= 0) {
        off();
      }
      break;
    case Mode::PulsingOn:
      if (static_cast<int32_t>(nowMs - nextChangeMs_) >= 0) {
        write(false);
        mode_ = Mode::PulsingOff;
        nextChangeMs_ = nowMs + pulseOffMs_;
      }
      break;
    case Mode::PulsingOff:
      if (static_cast<int32_t>(nowMs - nextChangeMs_) >= 0) {
        if (remainingPulses_ > 0) {
          --remainingPulses_;
        }
        if (remainingPulses_ == 0) {
          mode_ = Mode::Idle;
          overrideLevel_ = false;
          write(false);
        } else {
          write(true);
          mode_ = Mode::PulsingOn;
          nextChangeMs_ = nowMs + pulseOnMs_;
        }
      }
      break;
    case Mode::Idle:
    default:
      break;
  }
}

void VibrationMotor::setLevel(uint8_t level) {
  baseLevelPercent_ = levelToPercent(level);
  const bool enabled = (mode_ == Mode::PulsingOff)
                           ? false
                           : (mode_ == Mode::Idle ? outputOn_ : true);
  write(enabled);
}

void VibrationMotor::on() {
  overrideLevel_ = false;
  mode_ = Mode::Idle;
  write(true);
}

void VibrationMotor::on(uint8_t strengthPercent) {
  overrideLevel_ = true;
  commandLevelPercent_ = clampPercent(strengthPercent);
  mode_ = Mode::Idle;
  write(true);
}

void VibrationMotor::off() {
  overrideLevel_ = false;
  mode_ = Mode::Idle;
  write(false);
}

void VibrationMotor::onFor(uint32_t durationMs) {
  if (durationMs == 0) {
    off();
    return;
  }
  overrideLevel_ = false;
  write(true);
  mode_ = Mode::TimedOn;
  nextChangeMs_ = millis() + durationMs;
}

void VibrationMotor::onFor(uint32_t durationMs, uint8_t strengthPercent) {
  if (durationMs == 0) {
    off();
    return;
  }
  overrideLevel_ = true;
  commandLevelPercent_ = clampPercent(strengthPercent);
  write(true);
  mode_ = Mode::TimedOn;
  nextChangeMs_ = millis() + durationMs;
}

void VibrationMotor::pulse(uint8_t count, uint32_t onMs, uint32_t offMs) {
  if (count == 0 || onMs == 0) {
    off();
    return;
  }
  overrideLevel_ = false;
  pulseOnMs_ = onMs;
  pulseOffMs_ = offMs;
  remainingPulses_ = count;
  write(true);
  mode_ = Mode::PulsingOn;
  nextChangeMs_ = millis() + pulseOnMs_;
}

void VibrationMotor::pulse(uint8_t count, uint32_t onMs, uint32_t offMs,
                           uint8_t strengthPercent) {
  if (count == 0 || onMs == 0) {
    off();
    return;
  }
  overrideLevel_ = true;
  commandLevelPercent_ = clampPercent(strengthPercent);
  pulseOnMs_ = onMs;
  pulseOffMs_ = offMs;
  remainingPulses_ = count;
  write(true);
  mode_ = Mode::PulsingOn;
  nextChangeMs_ = millis() + pulseOnMs_;
}

bool VibrationMotor::isActive() const {
  return outputOn_;
}

void VibrationMotor::write(bool enabled) {
  const uint8_t effectivePercent =
      overrideLevel_ ? scalePercent(baseLevelPercent_, commandLevelPercent_)
                     : baseLevelPercent_;
  uint32_t duty =
      enabled ? (static_cast<uint32_t>(effectivePercent) * kPwmMaxDuty) / 100u : 0u;
  if (!activeHigh_) {
    duty = kPwmMaxDuty - duty;
  }
  ledcWrite(channel_, duty);
  outputOn_ = enabled && effectivePercent > 0;
}

Buzzer::Buzzer(uint8_t pin, uint8_t channel, uint32_t pwmHz, bool activeHigh)
    : pin_(pin),
      channel_(channel),
      pwmHz_(pwmHz),
      activeHigh_(activeHigh) {}

void Buzzer::begin() {
  ledcSetup(channel_, pwmHz_, kPwmResolutionBits);
  ledcAttachPin(pin_, channel_);
  write(false);
}

void Buzzer::update(uint32_t nowMs) {
  if (mode_ == Mode::TimedOn || mode_ == Mode::TimedTone) {
    if (static_cast<int32_t>(nowMs - endMs_) >= 0) {
      off();
    }
  }
}

void Buzzer::setLevel(uint8_t level) {
  baseLevelPercent_ = levelToPercent(level);
  write(mode_ != Mode::Idle);
}

void Buzzer::on() {
  overrideLevel_ = false;
  mode_ = Mode::ContinuousOn;
  write(true);
}

void Buzzer::on(uint8_t strengthPercent) {
  overrideLevel_ = true;
  commandLevelPercent_ = clampPercent(strengthPercent);
  mode_ = Mode::ContinuousOn;
  write(true);
}

void Buzzer::off() {
  overrideLevel_ = false;
  mode_ = Mode::Idle;
  write(false);
}

void Buzzer::onFor(uint32_t durationMs) {
  if (durationMs == 0) {
    off();
    return;
  }
  overrideLevel_ = false;
  write(true);
  mode_ = Mode::TimedOn;
  endMs_ = millis() + durationMs;
}

void Buzzer::onFor(uint32_t durationMs, uint8_t strengthPercent) {
  if (durationMs == 0) {
    off();
    return;
  }
  overrideLevel_ = true;
  commandLevelPercent_ = clampPercent(strengthPercent);
  write(true);
  mode_ = Mode::TimedOn;
  endMs_ = millis() + durationMs;
}

void Buzzer::playTone(uint16_t frequencyHz, uint32_t durationMs) {
  if (frequencyHz == 0) {
    off();
    return;
  }
  overrideLevel_ = false;
  ledcWriteTone(channel_, frequencyHz);
  write(true);
  if (durationMs == 0) {
    mode_ = Mode::ContinuousTone;
  } else {
    mode_ = Mode::TimedTone;
    endMs_ = millis() + durationMs;
  }
}

void Buzzer::playTone(uint16_t frequencyHz, uint32_t durationMs,
                      uint8_t strengthPercent) {
  if (frequencyHz == 0) {
    off();
    return;
  }
  overrideLevel_ = true;
  commandLevelPercent_ = clampPercent(strengthPercent);
  ledcWriteTone(channel_, frequencyHz);
  write(true);
  if (durationMs == 0) {
    mode_ = Mode::ContinuousTone;
  } else {
    mode_ = Mode::TimedTone;
    endMs_ = millis() + durationMs;
  }
}

bool Buzzer::isActive() const {
  return outputOn_;
}

void Buzzer::write(bool enabled) {
  const uint8_t effectivePercent =
      overrideLevel_ ? scalePercent(baseLevelPercent_, commandLevelPercent_)
                     : baseLevelPercent_;
  uint32_t duty =
      enabled ? (static_cast<uint32_t>(effectivePercent) * kPwmMaxDuty) / 100u : 0u;
  if (!activeHigh_) {
    duty = kPwmMaxDuty - duty;
  }
  ledcWrite(channel_, duty);
  outputOn_ = enabled && effectivePercent > 0;
}

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
  r_ = r;
  g_ = g;
  b_ = b;
  on_ = true;
  apply();
}

void NeopixelLed::onColor(uint32_t hexColor) {
  const uint8_t r = static_cast<uint8_t>((hexColor >> 16) & 0xFFu);
  const uint8_t g = static_cast<uint8_t>((hexColor >> 8) & 0xFFu);
  const uint8_t b = static_cast<uint8_t>(hexColor & 0xFFu);
  onColor(r, g, b);
}

void NeopixelLed::off() {
  on_ = false;
  apply();
}

bool NeopixelLed::isActive() const {
  return on_ && level_ > 0;
}

void NeopixelLed::apply() {
  const uint8_t brightness = static_cast<uint8_t>((level_ * 255u) / 10u);
  pixels_.setBrightness(brightness);
  if (on_ && level_ > 0) {
    const uint32_t color = pixels_.Color(r_, g_, b_);
    for (uint16_t i = 0; i < pixels_.numPixels(); ++i) {
      pixels_.setPixelColor(i, color);
    }
  } else {
    pixels_.clear();
  }
  pixels_.show();
}
