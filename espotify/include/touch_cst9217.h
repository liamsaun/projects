#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// CST9217 capacitive touch (Waveshare ESP32-C6-Touch-AMOLED-2.16)
// Shares the I2C bus that Amoled::begin() brings up for the PMIC.

namespace Touch {

inline void begin() {
  pinMode(TOUCH_PIN_RST, OUTPUT);
  digitalWrite(TOUCH_PIN_RST, HIGH);
  delay(200);
  digitalWrite(TOUCH_PIN_RST, LOW);
  delay(200);
  digitalWrite(TOUCH_PIN_RST, HIGH);
  delay(200);
}

// Returns true while a finger is down, with the point in panel pixels
inline bool read(int16_t &x, int16_t &y) {
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(0xD0);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return false;

  uint8_t d[10];
  if (Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)sizeof(d)) != sizeof(d)) return false;
  for (uint8_t &b : d) b = Wire.read();

  if (d[6] != 0xAB) return false;           // Report not valid
  if ((d[5] & 0x7F) == 0) return false;     // No points
  if ((d[0] & 0x0F) != 0x06) return false;  // First point not in contact

  int16_t a = ((int16_t)d[1] << 4) | (d[3] >> 4);
  int16_t b = ((int16_t)d[2] << 4) | (d[3] & 0x0F);

  // The controller's axes are swapped relative to the panel
  x = TOUCH_INVERT_X ? (AMOLED_W - 1 - b) : b;
  y = TOUCH_INVERT_Y ? (AMOLED_H - 1 - a) : a;
  x = constrain(x, 0, AMOLED_W - 1);
  y = constrain(y, 0, AMOLED_H - 1);
  return true;
}

}  // namespace Touch
