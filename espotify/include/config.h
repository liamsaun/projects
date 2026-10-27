#pragma once

// Board selection
//  Default: ESP32 Dev Module + 1.8" ST7735 TFT (128x160, SPI)
//  -DBOARD_WAVESHARE_C6_AMOLED_216: Waveshare ESP32-C6-Touch-AMOLED-2.16
//    (480x480 CO5300 AMOLED over QSPI, CST9217 touch, AXP2101 PMIC)

#if defined(BOARD_WAVESHARE_C6_AMOLED_216)

#define DISPLAY_AMOLED_QSPI   1
#define HAS_TOUCH             1

// AMOLED (QSPI, no DC pin, reset is done by power-cycling PMIC ALDO3)
#define AMOLED_SPI_HOST    SPI2_HOST
#define AMOLED_PIN_CS      15
#define AMOLED_PIN_SCLK     0
#define AMOLED_PIN_D0       1
#define AMOLED_PIN_D1       2
#define AMOLED_PIN_D2       3
#define AMOLED_PIN_D3       4
#define AMOLED_PCLK_HZ     40000000
#define AMOLED_W          480
#define AMOLED_H          480
#define AMOLED_MADCTL    0x30       // Orientation used by Waveshare's demos (0xF0 = rotated 180 deg)

// The 128x160 UI is scaled 3x (384x480) and centred; AMOLED black = pixels off
#define AMOLED_SCALE        3
#define AMOLED_X_OFFSET   ((AMOLED_W - 128 * AMOLED_SCALE) / 2)
#define AMOLED_Y_OFFSET   ((AMOLED_H - 160 * AMOLED_SCALE) / 2)

// Shared I2C bus: AXP2101 PMIC + CST9217 touch
#define I2C_PIN_SDA         8
#define I2C_PIN_SCL         7
#define PMIC_I2C_ADDR    0x34
#define TOUCH_I2C_ADDR   0x5A
#define TOUCH_PIN_RST      11
#define TOUCH_PIN_INT       5

// Touch -> panel coordinate mapping (matches AMOLED_MADCTL 0x30).
// If swipes go the wrong way after rotating the panel, flip these.
#define TOUCH_INVERT_X      1
#define TOUCH_INVERT_Y      0

// Buttons: BOOT (GPIO9) - press to wake, hold 3s to factory reset
#define RESET_BTN_PIN       9
#define DEFAULT_WAKE_PIN    9

// Pins the wake button must never claim (display, I2C, touch, USB D-/D+)
#define RESERVED_PIN_MASK  ((1ULL << 0) | (1ULL << 1) | (1ULL << 2) | (1ULL << 3) | \
                            (1ULL << 4) | (1ULL << 5) | (1ULL << 7) | (1ULL << 8) | \
                            (1ULL << 11) | (1ULL << 12) | (1ULL << 13) | (1ULL << 15))

// Firmware from the upstream OTA feed is built for the ST7735 board
#define OTA_UPDATES_DISABLED 1

#else

// Hardware Config
//  MOSI  → 23    SCK  → 18    CS  → 5
//  DC/A0 → 2     RST  → 4
//  BL    → 22 (PWM)   BTN → 12 (INPUT_PULLUP)
//

#define PIN_BL       22       // Backlight (LEDC PWM)

#define RESET_BTN_PIN      12
#define DEFAULT_WAKE_PIN    0 // BOOT
#define RESERVED_PIN_MASK   0ULL

// Display
#define SCREEN_W    128       // Portrait width
#define SCREEN_H    160       // Portrait height
#define SCREEN_ROT    2       // Portrait rotation (180 deg)

// Backlight PWM
#define BL_CHANNEL    0       // LEDC channel (core <3.x)
#define BL_FREQ    5000       // Hz
#define BL_RES        8       // 8-bit (0-255)

#endif

#define BL_FULL     255
#define BL_DIM       40
#define BL_OFF        0

// The Spotify task shares core 1 with the UI on dual-core chips; the C6 has one core
#define SPOTIFY_TASK_CORE  (portNUM_PROCESSORS > 1 ? 1 : 0)

// Timing
#define UPDATE_INTERVAL  1000  // Data refresh ms
