#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class VibrationMotor {
public:
  VibrationMotor(uint8_t pin, uint8_t channel, uint32_t pwmHz = 2000,
                 bool activeHigh = true);

  void begin();
  void update(uint32_t nowMs);

  void setLevel(uint8_t level);
  void on();
  void on(uint8_t strengthPercent);
  void off();
  void onFor(uint32_t durationMs);
  void onFor(uint32_t durationMs, uint8_t strengthPercent);
  void pulse(uint8_t count, uint32_t onMs, uint32_t offMs);
  void pulse(uint8_t count, uint32_t onMs, uint32_t offMs, uint8_t strengthPercent);
  bool isActive() const;

private:
  enum class Mode : uint8_t {
    Idle,
    TimedOn,
    PulsingOn,
    PulsingOff,
  };

  void write(bool enabled);

  uint8_t pin_;
  uint8_t channel_;
  uint32_t pwmHz_;
  bool activeHigh_;
  Mode mode_ = Mode::Idle;
  uint8_t remainingPulses_ = 0;
  uint32_t nextChangeMs_ = 0;
  uint32_t pulseOnMs_ = 0;
  uint32_t pulseOffMs_ = 0;
  uint8_t baseLevelPercent_ = 100;
  uint8_t commandLevelPercent_ = 100;
  bool overrideLevel_ = false;
  bool outputOn_ = false;
};

class Buzzer {
public:
  Buzzer(uint8_t pin, uint8_t channel, uint32_t pwmHz = 2000, bool activeHigh = true);

  void begin();
  void update(uint32_t nowMs);

  void setLevel(uint8_t level);
  void on();
  void on(uint8_t strengthPercent);
  void off();
  void onFor(uint32_t durationMs);
  void onFor(uint32_t durationMs, uint8_t strengthPercent);
  void playTone(uint16_t frequencyHz, uint32_t durationMs = 0);
  void playTone(uint16_t frequencyHz, uint32_t durationMs, uint8_t strengthPercent);
  bool isActive() const;

private:
  enum class Mode : uint8_t {
    Idle,
    TimedOn,
    TimedTone,
    ContinuousTone,
    ContinuousOn,
  };

  void write(bool enabled);

  uint8_t pin_;
  uint8_t channel_;
  uint32_t pwmHz_;
  bool activeHigh_;
  Mode mode_ = Mode::Idle;
  uint32_t endMs_ = 0;
  uint8_t baseLevelPercent_ = 100;
  uint8_t commandLevelPercent_ = 100;
  bool overrideLevel_ = false;
  bool outputOn_ = false;
};

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
  uint8_t r_ = 0;
  uint8_t g_ = 0;
  uint8_t b_ = 0;
  bool on_ = false;
};
