#pragma once

#include <Arduino.h>
#include <time.h>

class RussianRoulette {
 public:
  void reset(uint8_t chambers = 6);
  bool fire();
  uint8_t remaining() const;
  bool lastHit() const;

 private:
  uint8_t chambers_ = 6;
  uint8_t remaining_ = 6;
  bool lastHit_ = false;
};

class DuelMode {
 public:
  void enable(time_t now);
  void disable();
  bool enabled() const;
  bool due(time_t now) const;
  void scheduleNext(time_t now);
  time_t nextTime() const;
  uint8_t weightFor(const String &userId) const;
  void rememberPick(const String &userId);

 private:
  static constexpr uint8_t kRecentCount = 4;
  time_t nextDuelAt_ = 0;
  bool enabled_ = false;
  String recent_[kRecentCount];
  uint8_t recentIndex_ = 0;
};
