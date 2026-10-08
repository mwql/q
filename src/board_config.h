#pragma once

// Verified TPM408-2.8 / ESP32-2432S028R hardware configuration.
namespace Board {
constexpr uint16_t kWidth = 320;
constexpr uint16_t kHeight = 240;
constexpr uint8_t kTouchDout = 39;
constexpr uint8_t kTouchDin = 32;
constexpr uint8_t kTouchCs = 33;
constexpr uint8_t kTouchClock = 25;
constexpr uint8_t kIrPin = 27;
constexpr uint8_t kSdCs  = 5;   // SD card chip-select on ESP32-2432S028R
constexpr uint32_t kDefaultSleepMs = 10000;

// CYD rear RGB LED pins (common-anode, active LOW)
constexpr uint8_t kLedRed = 4;
constexpr uint8_t kLedRedAlt = 22;
constexpr uint8_t kLedGreen = 16;
constexpr uint8_t kLedBlue = 17;
}
