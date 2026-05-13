#pragma once

#include <Arduino.h>

constexpr uint8_t kDisplayWidth    = 128;
constexpr uint8_t kDisplayHeight   = 64;
constexpr uint8_t kStatusBarHeight = 14;
constexpr uint8_t kNavBarHeight    = 0;
constexpr uint8_t kMenuTop         = kStatusBarHeight + 2;
constexpr uint8_t kMenuBottom      = kDisplayHeight - kNavBarHeight - 2;
constexpr uint8_t kMenuHeight      = kMenuBottom - kMenuTop;
