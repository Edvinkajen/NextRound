#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// ---------------------------------------------------------------------------
// PwmActuator — shared pin/channel/level/strength logic for Buzzer & motor
// ---------------------------------------------------------------------------

class PwmActuator {
protected:
  PwmActuator(uint8_t pin, uint8_t channel, uint32_t pwmHz, bool activeHigh)
      : pin_(pin), channel_(channel), pwmHz_(pwmHz), activeHigh_(activeHigh) {}

  void    pwmBegin();
  void    writePercent(bool enabled);

  static uint8_t levelToPercent(uint8_t level);
  static uint8_t scalePercent(uint8_t base, uint8_t cmd);

  uint8_t  pin_;
  uint8_t  channel_;
  uint32_t pwmHz_;
  bool     activeHigh_;
  uint8_t  baseLevelPercent_    = 100;
  uint8_t  commandLevelPercent_ = 100;
  bool     overrideLevel_       = false;
  bool     outputOn_            = false;
};

// ---------------------------------------------------------------------------
// VibrationMotor
// ---------------------------------------------------------------------------

class VibrationMotor : public PwmActuator {
public:
  VibrationMotor(uint8_t pin, uint8_t channel, uint32_t pwmHz = 20000,
                 bool activeHigh = true);

  void begin();
  void update(uint32_t nowMs);

  void setLevel(uint8_t level);
  void on();
  void on(uint8_t strengthPercent);
  void off();
  void onFor(uint32_t nowMs, uint32_t durationMs);
  void onFor(uint32_t nowMs, uint32_t durationMs, uint8_t strengthPercent);
  void pulse(uint32_t nowMs, uint8_t count, uint32_t onMs, uint32_t offMs);
  void pulse(uint32_t nowMs, uint8_t count, uint32_t onMs, uint32_t offMs,
             uint8_t strengthPercent);
  bool isActive() const;

private:
  enum class Mode : uint8_t { Idle, TimedOn, PulsingOn, PulsingOff };

  uint8_t  remainingPulses_ = 0;
  uint32_t nextChangeMs_    = 0;
  uint32_t pulseOnMs_       = 0;
  uint32_t pulseOffMs_      = 0;
  Mode     mode_            = Mode::Idle;
};

// ---------------------------------------------------------------------------
// Buzzer
// ---------------------------------------------------------------------------

class Buzzer : public PwmActuator {
public:
  Buzzer(uint8_t pin, uint8_t channel, uint32_t pwmHz = 2700,
         bool activeHigh = true);

  void begin();
  void update(uint32_t nowMs);

  void setLevel(uint8_t level);
  void on();
  void on(uint8_t strengthPercent);
  void off();
  void onFor(uint32_t nowMs, uint32_t durationMs);
  void onFor(uint32_t nowMs, uint32_t durationMs, uint8_t strengthPercent);
  void playTone(uint32_t nowMs, uint16_t frequencyHz, uint32_t durationMs = 0);
  void playTone(uint32_t nowMs, uint16_t frequencyHz, uint32_t durationMs,
                uint8_t strengthPercent);
  bool isActive() const;

private:
  enum class Mode : uint8_t { Idle, TimedOn, TimedTone, ContinuousTone, ContinuousOn };

  uint32_t endMs_ = 0;
  Mode     mode_  = Mode::Idle;
};

// ---------------------------------------------------------------------------
// NeopixelLed
// ---------------------------------------------------------------------------

class NeopixelLed {
public:
  NeopixelLed(uint8_t pin, uint16_t count = 1,
              neoPixelType type = NEO_GRB + NEO_KHZ800);

  void begin();
  void setLevel(uint8_t level);
  void onColor(uint8_t r, uint8_t g, uint8_t b);
  void onColor(uint32_t hexColor);
  void off();
  bool isActive() const;

private:
  void apply();

  Adafruit_NeoPixel pixels_;
  uint8_t level_ = 10;
  uint8_t r_ = 0, g_ = 0, b_ = 0;
  bool    on_ = false;
};
