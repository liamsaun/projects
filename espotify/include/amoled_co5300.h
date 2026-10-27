#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include "config.h"

// CO5300 AMOLED over QSPI (Waveshare ESP32-C6-Touch-AMOLED-2.16)
//
// Commands go out as 0x02 + 24-bit address (cmd << 8) on one line, pixel data as
// 0x32 + (0x2C << 8) on four lines - the same framing the SH8601 esp_lcd driver uses.
// The panel has no reset pin; it is reset by power-cycling AXP2101 ALDO3.

namespace Amoled {

// Rows of the 128x160 source sent per QSPI transfer. Must be even so every
// window starts and ends on the 2-pixel boundary the CO5300 requires.
static constexpr int BAND_SRC_ROWS = 4;
static constexpr int BAND_ROWS     = BAND_SRC_ROWS * AMOLED_SCALE;
static constexpr size_t BAND_BYTES = (size_t)AMOLED_W * BAND_ROWS * 2;

static_assert(AMOLED_X_OFFSET % 2 == 0 && AMOLED_Y_OFFSET % 2 == 0, "CO5300 windows must start on even pixels");
static_assert((128 * AMOLED_SCALE) % 2 == 0 && BAND_ROWS % 2 == 0, "CO5300 windows must be an even size");
static_assert(160 % BAND_SRC_ROWS == 0, "Bands must tile the sprite height");

static esp_lcd_panel_io_handle_t io = nullptr;
static uint16_t *bandBuf[2] = {nullptr, nullptr};
static uint32_t bandCount = 0;
static volatile int16_t pendingBrightness = -1;

struct InitCmd {
  uint8_t  cmd;
  uint8_t  len;
  uint16_t delayMs;
  uint8_t  data[4];
};

// From Waveshare's Arduino LVGL example for this board
static const InitCmd INIT_CMDS[] = {
  {0x11, 0, 600, {}},                       // Sleep out
  {0xFE, 1, 0,   {0x20}},                   // Page 0x20
  {0x19, 1, 0,   {0x10}},
  {0x1C, 1, 0,   {0xA0}},
  {0xFE, 1, 0,   {0x00}},                   // Page 0x00
  {0xC4, 1, 0,   {0x80}},                   // QSPI mode
  {0x3A, 1, 0,   {0x55}},                   // RGB565
  {0x35, 1, 0,   {0x00}},                   // Tearing effect on
  {0x36, 1, 0,   {AMOLED_MADCTL}},
  {0x53, 1, 0,   {0x20}},
  {0x51, 1, 0,   {0xFF}},                   // Brightness
  {0x63, 1, 0,   {0xFF}},
  {0x2A, 4, 0,   {0x00, 0x00, 0x01, 0xDF}}, // 0..479
  {0x2B, 4, 0,   {0x00, 0x00, 0x01, 0xDF}}, // 0..479
  {0x29, 0, 100, {}},                       // Display on
};

// AXP2101 PMIC
static bool pmicWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(PMIC_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static int pmicRead(uint8_t reg) {
  Wire.beginTransmission(PMIC_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((uint8_t)PMIC_I2C_ADDR, (uint8_t)1) != 1) return -1;
  return Wire.read();
}

static void pmicSetAldo(uint8_t bit, bool on) {
  int reg = pmicRead(0x90);  // LDO on/off control 0
  if (reg < 0) return;
  pmicWrite(0x90, on ? (reg | bit) : (reg & ~bit));
}

static void pmicInit() {
  // ALDO1-4 to 3.3V ((mV - 500) / 100), then make sure all four are on
  for (uint8_t reg = 0x92; reg <= 0x95; reg++) {
    pmicWrite(reg, (3300 - 500) / 100);
  }
  pmicSetAldo(0x0F, true);
}

static void writeCmd(uint8_t cmd, const uint8_t *data, size_t len) {
  esp_lcd_panel_io_tx_param(io, (0x02UL << 24) | ((uint32_t)cmd << 8), data, len);
}

static void setWindow(int x0, int y0, int x1, int y1) {  // inclusive
  const uint8_t col[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
  const uint8_t row[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
  writeCmd(0x2A, col, 4);
  writeCmd(0x2B, row, 4);
}

// Queues one band. Setting the window waits for earlier queued pixel
// transfers, so the buffer used two bands ago is always free to refill.
static void writeBand(int x, int y, int w, int h, const uint16_t *pixels) {
  setWindow(x, y, x + w - 1, y + h - 1);
  esp_lcd_panel_io_tx_color(io, (0x32UL << 24) | (0x2CUL << 8), pixels, (size_t)w * h * 2);
}

static uint16_t *nextBandBuffer() {
  return bandBuf[bandCount++ & 1];
}

static void clear() {
  for (int y = 0; y < AMOLED_H; y += BAND_ROWS) {
    uint16_t *buf = nextBandBuffer();
    memset(buf, 0, BAND_BYTES);
    writeBand(0, y, AMOLED_W, BAND_ROWS, buf);
  }
}

static void applyBrightness() {
  int16_t level = pendingBrightness;
  if (level < 0) return;
  pendingBrightness = -1;
  uint8_t val = (uint8_t)level;
  writeCmd(0x51, &val, 1);
}

inline bool begin() {
  Wire.begin(I2C_PIN_SDA, I2C_PIN_SCL, 400000);
  pmicInit();

  spi_bus_config_t buscfg = {};
  buscfg.sclk_io_num = AMOLED_PIN_SCLK;
  buscfg.data0_io_num = AMOLED_PIN_D0;
  buscfg.data1_io_num = AMOLED_PIN_D1;
  buscfg.data2_io_num = AMOLED_PIN_D2;
  buscfg.data3_io_num = AMOLED_PIN_D3;
  buscfg.max_transfer_sz = BAND_BYTES;
  if (spi_bus_initialize(AMOLED_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  esp_lcd_panel_io_spi_config_t iocfg = {};
  iocfg.cs_gpio_num = (gpio_num_t)AMOLED_PIN_CS;
  iocfg.dc_gpio_num = (gpio_num_t)-1;
  iocfg.spi_mode = 0;
  iocfg.pclk_hz = AMOLED_PCLK_HZ;
  iocfg.trans_queue_depth = 4;
  iocfg.lcd_cmd_bits = 32;
  iocfg.lcd_param_bits = 8;
  iocfg.flags.quad_mode = true;
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)AMOLED_SPI_HOST, &iocfg, &io) != ESP_OK) return false;

  bandBuf[0] = (uint16_t *)heap_caps_malloc(BAND_BYTES, MALLOC_CAP_DMA);
  bandBuf[1] = (uint16_t *)heap_caps_malloc(BAND_BYTES, MALLOC_CAP_DMA);
  if (!bandBuf[0] || !bandBuf[1]) return false;

  // Power-cycle the panel (its reset line is tied to ALDO3)
  pmicSetAldo(0x04, true);
  delay(100);
  pmicSetAldo(0x04, false);
  delay(100);
  pmicSetAldo(0x04, true);
  delay(100);

  for (const InitCmd &c : INIT_CMDS) {
    writeCmd(c.cmd, c.len ? c.data : nullptr, c.len);
    if (c.delayMs) delay(c.delayMs);
  }

  clear();
  return true;
}

// 0-255. Only stored here; applied by the UI task before its next transfer,
// so other tasks (e.g. auto-wake from the Spotify task) never touch the SPI bus.
inline void setBrightness(uint8_t val) {
  pendingBrightness = val;
}

inline void service() {
  if (io) applyBrightness();
}

// Pushes a 128x160 big-endian RGB565 frame, scaled by AMOLED_SCALE and centred
inline void pushScaled(const uint16_t *src, int srcW, int srcH) {
  if (!io || !src) return;
  applyBrightness();

  const int dstW = srcW * AMOLED_SCALE;
  for (int sy = 0; sy < srcH; sy += BAND_SRC_ROWS) {
    uint16_t *buf = nextBandBuffer();
    uint16_t *out = buf;
    for (int r = 0; r < BAND_SRC_ROWS; r++) {
      const uint16_t *in = src + (sy + r) * srcW;
      uint16_t *rowStart = out;
      for (int x = 0; x < srcW; x++) {
        uint16_t px = in[x];
        for (int k = 0; k < AMOLED_SCALE; k++) *out++ = px;
      }
      for (int k = 1; k < AMOLED_SCALE; k++) {
        memcpy(out, rowStart, dstW * 2);
        out += dstW;
      }
    }
    writeBand(AMOLED_X_OFFSET, AMOLED_Y_OFFSET + sy * AMOLED_SCALE, dstW, BAND_ROWS, buf);
  }
}

}  // namespace Amoled
