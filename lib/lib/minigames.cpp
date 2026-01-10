#include "minigames.h"

void RussianRoulette::reset(uint8_t chambers) {
  chambers_ = chambers == 0 ? 6 : chambers;
  remaining_ = chambers_;
  lastHit_ = false;
}

bool RussianRoulette::fire() {
  if (remaining_ == 0) {
    remaining_ = chambers_;
  }
  const uint8_t roll = static_cast<uint8_t>(random(remaining_));
  lastHit_ = (roll == 0);
  if (lastHit_) {
    remaining_ = chambers_;
  } else {
    remaining_ = static_cast<uint8_t>(remaining_ - 1);
  }
  return lastHit_;
}

uint8_t RussianRoulette::remaining() const {
  return remaining_;
}

bool RussianRoulette::lastHit() const {
  return lastHit_;
}

void DuelMode::enable(time_t now) {
  enabled_ = true;
  scheduleNext(now);
}

void DuelMode::disable() {
  enabled_ = false;
  nextDuelAt_ = 0;
}

bool DuelMode::enabled() const {
  return enabled_;
}

bool DuelMode::due(time_t now) const {
  return enabled_ && nextDuelAt_ != 0 && now >= nextDuelAt_;
}

void DuelMode::scheduleNext(time_t now) {
  if (!enabled_) {
    return;
  }
  const uint32_t delaySec = static_cast<uint32_t>(random(5 * 60, 30 * 60 + 1));
  nextDuelAt_ = now + delaySec;
}

time_t DuelMode::nextTime() const {
  return nextDuelAt_;
}

uint8_t DuelMode::weightFor(const String &userId) const {
  for (uint8_t i = 0; i < kRecentCount; ++i) {
    if (recent_[i].length() > 0 && recent_[i] == userId) {
      return 1;
    }
  }
  return 3;
}

void DuelMode::rememberPick(const String &userId) {
  recent_[recentIndex_] = userId;
  recentIndex_ = static_cast<uint8_t>((recentIndex_ + 1) % kRecentCount);
}
