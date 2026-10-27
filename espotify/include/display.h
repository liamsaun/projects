#pragma once
#include "config.h"

// Display layer
//  ST7735:  TFT_eSPI draws to the panel; the UI sprite is pushed as-is.
//  AMOLED:  LovyanGFX renders the same 128x160 UI into a RAM sprite (TFT_eSPI
//           doesn't build for the ESP32-C6), which is scaled up over QSPI.

#if defined(DISPLAY_AMOLED_QSPI)
  #include <LovyanGFX.hpp>
  #include "amoled_co5300.h"
  using namespace lgfx::colors;     // TFT_BLACK, TFT_WHITE, ...
  using namespace lgfx::textdatum;  // TL_DATUM, TC_DATUM, ...
  using TFT_eSprite = LGFX_Sprite;
#else
  #include <TFT_eSPI.h>
#endif

extern TFT_eSprite sprite;

// RGB565 blend, same maths as TFT_eSPI::alphaBlend
inline uint16_t alphaBlend565(uint8_t alpha, uint16_t fgc, uint16_t bgc) {
  uint32_t rxb = bgc & 0xF81F;
  rxb += ((fgc & 0xF81F) - rxb) * (alpha >> 2) >> 6;
  uint32_t xgx = bgc & 0x07E0;
  xgx += ((fgc & 0x07E0) - xgx) * alpha >> 8;
  return (rxb & 0xF81F) | (xgx & 0x07E0);
}

// Copy the finished UI sprite to the panel
inline void displayPushFrame() {
#if defined(DISPLAY_AMOLED_QSPI)
  Amoled::pushScaled((const uint16_t *)sprite.getBuffer(), sprite.width(), sprite.height());
#else
  sprite.pushSprite(0, 0);
#endif
}

// Called every loop from the UI task: applies brightness changes even while no frames are drawn
inline void displayService() {
#if defined(DISPLAY_AMOLED_QSPI)
  Amoled::service();
#endif
}

// Show boot/setup text drawn on `screen` (the TFT draws it directly)
inline void displayShowScreen() {
#if defined(DISPLAY_AMOLED_QSPI)
  displayPushFrame();
#endif
}
