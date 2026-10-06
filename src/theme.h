#pragma once
#include <Arduino.h>

struct Theme {
  uint16_t background;
  uint16_t header;
  uint16_t surface;
  uint16_t surfaceRaised;
  uint16_t accent;
  uint16_t text;
  uint16_t muted;
  uint16_t success;
};

// Both themes use a dark surface with a clear light-blue interaction color.
constexpr Theme kThemes[] = {
  {0x0841, 0x10A3, 0x18E4, 0x2166, 0x6E7F, 0xFFFF, 0xBDF7, 0x5EF7}, // Ocean
  {0x0000, 0x0842, 0x1084, 0x18E7, 0x4DF9, 0xFFFF, 0xAD55, 0x5EF7}, // Midnight
};
constexpr uint8_t kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);
