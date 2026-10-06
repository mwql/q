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
constexpr uint32_t kDefaultSleepMs = 10000;
}
