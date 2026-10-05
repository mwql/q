#pragma once
#include <Arduino.h>

// CYD-2432S028 standard wiring. Change IR_PIN if you use a different free pin.
constexpr uint8_t IR_PIN = 27;
constexpr uint8_t BACKLIGHT_PIN = 21;
constexpr uint16_t TOUCH_MIN_X = 250, TOUCH_MAX_X = 3800;
constexpr uint16_t TOUCH_MIN_Y = 250, TOUCH_MAX_Y = 3800;

struct AppConfig {
  String haUrl;       // Example: http://homeassistant.local:8123
  String haToken;     // Long-lived Home Assistant token
  String irName;
  uint32_t irCode;
};
