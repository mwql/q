// Clean bring-up test for TPM408-2.8 / ESP32-2432S028.
// Pin configuration is based on a tested CYD TPM408 reference board.
#include <Arduino.h>
#include <TFT_eSPI.h>

static TFT_eSPI tft;
constexpr uint16_t LIGHT_BLUE = 0x9E7F;

void setup() {
  // Backlight first: this board is active-high on GPIO 21.
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);       // native 240x320 panel, logical 320x240 UI
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_WHITE);

  tft.fillRect(0, 0, 320, 52, LIGHT_BLUE);
  tft.setTextColor(TFT_WHITE, LIGHT_BLUE);
  tft.setTextSize(2);
  tft.drawString("CYD HOME", 18, 16);

  tft.setTextColor(LIGHT_BLUE, TFT_WHITE);
  tft.setTextSize(2);
  tft.drawCentreString("Display is working", 160, 92, 2);
  tft.setTextSize(1);
  tft.drawCentreString("TPM408-2.8 / ILI9341", 160, 125, 2);

  tft.drawRoundRect(24, 164, 272, 44, 8, LIGHT_BLUE);
  tft.drawCentreString("WHITE + LIGHT BLUE TEST", 160, 180, 2);
}

void loop() { delay(1000); }
