#pragma once
#include "display.h"
#include "data_structs.h"

// Ladybug theme (AMOLED build)
// 160x160 pixel-art layout, shown 3x on the 480x480 panel:
//   pastel polka-dot background, album art on a little card, and a ladybug
//   that walks along a vine as the song plays (and naps when it's paused).

// Low-res cover (Spotify's 64x64 image), drawn on the canvas
#define ALBUM_ART_W   64
#define ALBUM_ART_H   64
// Sharp cover (from the 300x300 image) shown at full panel resolution over the same spot
#define ALBUM_ART_HI  (ALBUM_ART_W * AMOLED_SCALE)

static constexpr uint16_t lb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static constexpr uint16_t LB_BG     = lb565(255, 230, 236);  // Soft pink
static constexpr uint16_t LB_DOT    = lb565(255, 212, 224);  // Background polka dots
static constexpr uint16_t LB_SHADOW = lb565(238, 190, 205);
static constexpr uint16_t LB_CARD   = lb565(255, 255, 255);
static constexpr uint16_t LB_TEXT   = lb565(96, 40, 66);     // Plum
static constexpr uint16_t LB_RED    = lb565(232, 44, 56);    // Ladybug red
static constexpr uint16_t LB_BLACK  = lb565(40, 24, 36);
static constexpr uint16_t LB_BLUSH  = lb565(255, 150, 172);
static constexpr uint16_t LB_STEM   = lb565(84, 168, 84);
static constexpr uint16_t LB_LEAF   = lb565(132, 206, 104);
static constexpr uint16_t LB_PETAL  = lb565(255, 255, 255);
static constexpr uint16_t LB_POLLEN = lb565(255, 206, 64);

static constexpr int LB_W = 160;
static constexpr int LB_TEXT_MAX_W = 150;
static constexpr int LB_VINE_X0 = 12, LB_VINE_X1 = 148, LB_VINE_Y = 153;

static constexpr int LB_ART_X = 48, LB_ART_Y = 8;
// Canvas colour meaning "sharp cover goes here". Loud magenta, so a compositing fault is obvious.
static constexpr uint16_t LB_ART_KEY = 0xF81F;

extern uint16_t albumArt[];
extern uint16_t albumArtHi[];
extern volatile bool albumArtHiReady;

// Decodes a JPEG cover straight into one of the cover buffers (big-endian RGB565, scaled to fit).
// Uses LovyanGFX's decoder: TJpg_Decoder can't be used on the ESP32-C6, whose ROM has an older
// tjpgd under the same function names. The build links that one, which lays out its data
// differently (sizes read as 300x0, colours scrambled).
inline bool decodeCover(const uint8_t *jpg, size_t len, uint16_t *dst, int size) {
  LGFX_Sprite canvas;
  canvas.setColorDepth(16);
  canvas.setBuffer(dst, size, size, 16);
  canvas.fillScreen(TFT_BLACK);
  return canvas.drawJpg(jpg, len, 0, 0, size, size, 0, 0, 0.0f, 0.0f);  // scale 0 = fit
}

// Little Pieces

// Side view, walking right with its feet on y = feetY. r = shell radius.
inline void drawLadybugSide(int cx, int feetY, int r, bool walking, bool sleeping) {
  int bellyY = feetY - 3;

  // Legs, shuffling while it walks
  int step = walking ? (int)((millis() / 160) % 2) : 0;
  for (int i = -1; i <= 1; i++) {
    int lx = cx + i * (r * 2 / 3) + ((i + 1 + step) % 2 ? 1 : 0);
    sprite.drawLine(lx, bellyY, lx + (walking ? (step ? 1 : -1) : 0), feetY, LB_BLACK);
  }

  // Head with an antenna
  int hx = cx + r - 1, hy = bellyY - 3, hr = r / 2 + 1;
  sprite.drawLine(hx + 1, hy - hr + 1, hx + hr + 2, hy - hr - 1, LB_BLACK);
  sprite.fillCircle(hx + hr + 2, hy - hr - 1, 1, LB_BLACK);
  sprite.fillCircle(hx, hy, hr, LB_BLACK);

  // Dome shell with spots and a shine
  sprite.fillArc(cx, bellyY, 0, r, 180, 360, LB_RED);
  sprite.drawFastHLine(cx - r, bellyY, r * 2 + 1, LB_BLACK);
  sprite.fillRect(cx - r / 2 - 1, bellyY - r / 2, 2, 2, LB_BLACK);
  sprite.fillRect(cx, bellyY - r + 2, 2, 2, LB_BLACK);
  sprite.fillRect(cx + r / 2, bellyY - r / 2, 2, 2, LB_BLACK);
  sprite.drawPixel(cx - r / 2, bellyY - r + 2, LB_CARD);

  // Eye and cheek
  int ex = hx + hr - 2, ey = hy - 1;
  if (sleeping) {
    sprite.drawFastHLine(ex - 1, ey + 1, 2, LB_CARD);
  } else {
    sprite.fillRect(ex - 1, ey, 2, 2, LB_CARD);
    sprite.drawPixel(ex, ey + 1, LB_BLACK);
  }
  sprite.drawPixel(ex - 1, ey + 3, LB_BLUSH);
}

// Front view with a big friendly face, for when there's no cover art. r = shell radius.
inline void drawLadybugFace(int cx, int cy, int r) {
  int hr = r * 3 / 5;
  int hy = cy - r + hr / 2;

  // Antennae
  sprite.drawLine(cx - 3, hy - hr + 1, cx - hr - 2, hy - hr - 6, LB_BLACK);
  sprite.drawLine(cx + 3, hy - hr + 1, cx + hr + 2, hy - hr - 6, LB_BLACK);
  sprite.fillCircle(cx - hr - 2, hy - hr - 6, 2, LB_BLACK);
  sprite.fillCircle(cx + hr + 2, hy - hr - 6, 2, LB_BLACK);

  // Shell, split down the middle, with spots and a shine
  sprite.fillCircle(cx, cy + 3, r, LB_RED);
  sprite.drawFastVLine(cx, hy, cy + 3 + r - hy, LB_BLACK);
  sprite.fillCircle(cx - r / 2 - 1, cy + 1, 3, LB_BLACK);
  sprite.fillCircle(cx + r / 2 + 1, cy + 1, 3, LB_BLACK);
  sprite.fillCircle(cx - r / 3, cy + r / 2 + 3, 2, LB_BLACK);
  sprite.fillCircle(cx + r / 3, cy + r / 2 + 3, 2, LB_BLACK);
  sprite.fillCircle(cx - r + 4, cy - 3, 1, LB_CARD);

  // Head and face
  sprite.fillCircle(cx, hy, hr, LB_BLACK);
  int eo = hr / 2;
  sprite.fillCircle(cx - eo, hy - 1, 3, LB_CARD);
  sprite.fillCircle(cx + eo, hy - 1, 3, LB_CARD);
  sprite.fillCircle(cx - eo, hy, 2, LB_BLACK);
  sprite.fillCircle(cx + eo, hy, 2, LB_BLACK);
  sprite.drawPixel(cx - eo - 1, hy - 2, LB_CARD);
  sprite.drawPixel(cx + eo - 1, hy - 2, LB_CARD);
  sprite.fillCircle(cx - eo - 2, hy + 4, 1, LB_BLUSH);
  sprite.fillCircle(cx + eo + 2, hy + 4, 1, LB_BLUSH);
  sprite.drawLine(cx - 2, hy + 4, cx, hy + 5, LB_CARD);
  sprite.drawLine(cx, hy + 5, cx + 2, hy + 4, LB_CARD);
}

inline void drawFlower(int cx, int cy, uint16_t petal) {
  sprite.fillCircle(cx - 2, cy, 2, petal);
  sprite.fillCircle(cx + 2, cy, 2, petal);
  sprite.fillCircle(cx, cy - 2, 2, petal);
  sprite.fillCircle(cx, cy + 2, 2, petal);
  sprite.fillCircle(cx, cy, 1, LB_POLLEN);
}

inline void drawHeart(int cx, int cy, uint16_t color) {
  sprite.fillCircle(cx - 2, cy - 1, 2, color);
  sprite.fillCircle(cx + 2, cy - 1, 2, color);
  sprite.fillTriangle(cx - 4, cy, cx + 4, cy, cx, cy + 4, color);
}

inline void drawPolkaDots() {
  for (int y = 6, row = 0; y < 160; y += 16, row++) {
    for (int x = (row % 2) ? 14 : 6; x < LB_W; x += 16) {
      sprite.fillCircle(x, y, 2, LB_DOT);
    }
  }
}

inline bool albumArtMissing() {
  return albumArt[0] == 0 && albumArt[ALBUM_ART_W * ALBUM_ART_H / 2 + ALBUM_ART_W / 2] == 0 &&
         albumArt[ALBUM_ART_W * ALBUM_ART_H - 1] == 0;
}

inline void drawAlbumCard() {
  const int x = LB_ART_X, y = LB_ART_Y;
  sprite.fillRoundRect(x - 4 + 2, y - 4 + 3, 72, 72, 9, LB_SHADOW);
  sprite.fillRoundRect(x - 4, y - 4, 72, 72, 9, LB_CARD);

  // Only key the panel while a sharp cover is loaded, so stray key-coloured pixels elsewhere are left alone
  bool sharp = albumArtHiReady;
  Amoled::setKeyedImage(sharp ? albumArtHi : nullptr, Amoled::X_OFFSET + x * AMOLED_SCALE,
                        Amoled::Y_OFFSET + y * AMOLED_SCALE, ALBUM_ART_HI, ALBUM_ART_HI, LB_ART_KEY);

  if (sharp) {
    sprite.fillRect(x, y, ALBUM_ART_W, ALBUM_ART_H, LB_ART_KEY);
  } else if (albumArtMissing()) {
    // No cover yet: a big ladybug says hi instead
    sprite.fillRoundRect(x, y, ALBUM_ART_W, ALBUM_ART_H, 6, LB_BG);
    drawLadybugFace(x + 32, y + 30, 18);
    drawHeart(x + 55, y + 10, LB_BLUSH);
  } else {
    sprite.pushImage(x, y, ALBUM_ART_W, ALBUM_ART_H, albumArt);
  }

  // A flower pinned to the corner of the card
  drawFlower(x - 2, y - 2, LB_BLUSH);
}

// Vine progress bar: leaves along the stem, flowers bloom where the ladybug has been
inline void drawVine(const SpotifyData &d) {
  sprite.fillRect(LB_VINE_X0, LB_VINE_Y, LB_VINE_X1 - LB_VINE_X0, 2, LB_STEM);

  int bugX = LB_VINE_X0;
  if (d.durationMs > 0) {
    uint32_t p = d.progressMs > d.durationMs ? d.durationMs : d.progressMs;
    bugX = LB_VINE_X0 + (int)((uint64_t)(LB_VINE_X1 - LB_VINE_X0) * p / d.durationMs);
  }

  for (int i = 0, x = LB_VINE_X0 + 10; x < LB_VINE_X1; x += 17, i++) {
    int ly = (i % 2) ? LB_VINE_Y + 3 : LB_VINE_Y - 2;
    if (x < bugX - 8) {
      drawFlower(x, ly, (i % 3 == 1) ? LB_BLUSH : LB_PETAL);
    } else {
      sprite.fillEllipse(x, ly, 4, 2, LB_LEAF);
    }
  }

  bool sleeping = !d.isPlaying;
  int bob = (d.isPlaying && (millis() / 320) % 2) ? -1 : 0;  // A little bounce in its step
  drawLadybugSide(bugX, LB_VINE_Y + bob, 7, d.isPlaying, sleeping);

  if (sleeping) {
    // Zzz drifting up while it naps
    int drift = (millis() / 1000) % 2;
    sprite.setTextFont(1);
    sprite.setTextDatum(BL_DATUM);
    sprite.setTextColor(LB_TEXT);
    sprite.drawString("z", bugX + 12, LB_VINE_Y - 10 - drift);
    sprite.setTextFont(2);
    sprite.drawString("Z", bugX + 18, LB_VINE_Y - 15 + drift);
  }
}

// Lyrics

// Splits text into rows of whole words that fit LB_TEXT_MAX_W in the current font.
// rowStart[i] is the index of the first word on row i.
inline int lbWrapWords(String words[], int numWords, int rowStart[], int maxRows) {
  int spaceW = sprite.textWidth(" ");
  int rows = 0, w = 0;
  for (int i = 0; i < numWords; i++) {
    int ww = sprite.textWidth(words[i]);
    if (rows == 0 || (w > 0 && w + spaceW + ww > LB_TEXT_MAX_W)) {
      if (rows == maxRows) break;
      rowStart[rows++] = i;
      w = ww;
    } else {
      w += spaceW + ww;
    }
  }
  return rows;
}

// Draws one lyric line centred at y, the word being sung in red. Returns the next free y.
inline int drawLyricLineLb(const char *text, int y, int activeWordIdx, uint8_t alpha) {
  if (strlen(text) == 0) return y;

  String words[16];
  int numWords = 0;
  const char *p = text;
  while (*p && numWords < 16) {
    while (*p == ' ') p++;
    const char *start = p;
    while (*p && *p != ' ') p++;
    if (p > start) words[numWords++] = String(text).substring(start - text, p - text);
  }
  if (numWords == 0) return y;

  sprite.setTextFont(2);
  if (sprite.textWidth(text) > LB_TEXT_MAX_W) sprite.setTextFont(1);
  int rowH = sprite.fontHeight() + 1;
  int spaceW = sprite.textWidth(" ");

  int rowStart[3];
  int rows = lbWrapWords(words, numWords, rowStart, 2);

  uint16_t normal = alphaBlend565(alpha, LB_TEXT, LB_BG);
  uint16_t active = alphaBlend565(alpha, LB_RED, LB_BG);
  sprite.setTextDatum(TL_DATUM);

  for (int r = 0; r < rows; r++) {
    int first = rowStart[r];
    int last = (r + 1 < rows) ? rowStart[r + 1] : numWords;
    int rowW = 0;
    for (int i = first; i < last; i++) rowW += sprite.textWidth(words[i]) + (i > first ? spaceW : 0);

    int x = LB_W / 2 - rowW / 2;
    for (int i = first; i < last; i++) {
      sprite.setTextColor(i == activeWordIdx ? active : normal);
      sprite.drawString(words[i], x, y);
      x += sprite.textWidth(words[i]) + spaceW;
    }
    y += rowH;
  }
  return y;
}

// Main Screen

inline bool uiTitleScrolls(const char *title) {
  sprite.setTextFont(2);
  return sprite.textWidth(title) > LB_TEXT_MAX_W;
}

inline void drawSpotify(const SpotifyData &d) {
  sprite.fillSprite(LB_BG);
  drawPolkaDots();
  drawAlbumCard();

  // Time, tucked into the top corners
  if (d.durationMs > 0) {
    char t[12];
    sprite.setTextFont(1);
    sprite.setTextColor(LB_TEXT);
    sprite.setTextDatum(TL_DATUM);
    snprintf(t, sizeof(t), "%lu:%02lu", (unsigned long)(d.progressMs / 60000), (unsigned long)(d.progressMs / 1000 % 60));
    sprite.drawString(t, 5, 5);
    sprite.setTextDatum(TR_DATUM);
    snprintf(t, sizeof(t), "%lu:%02lu", (unsigned long)(d.durationMs / 60000), (unsigned long)(d.durationMs / 1000 % 60));
    sprite.drawString(t, LB_W - 4, 5);
  }

  // Title (scrolls when too long)
  sprite.setTextFont(2);
  sprite.setTextColor(LB_TEXT);
  int tWidth = sprite.textWidth(d.title);
  if (tWidth > LB_TEXT_MAX_W) {
    sprite.setTextDatum(TL_DATUM);
    int loop = tWidth + 30;
    int offset = (millis() / 30) % loop;
    sprite.drawString(d.title, 5 - offset, 80);
    sprite.drawString(d.title, 5 - offset + loop, 80);
  } else {
    sprite.setTextDatum(TC_DATUM);
    sprite.drawString(d.title, LB_W / 2, 80);
  }

  // Artist
  sprite.setTextFont(1);
  sprite.setTextDatum(TC_DATUM);
  sprite.setTextColor(LB_RED);
  sprite.drawString(d.artist, LB_W / 2, 97);

  // Lyrics
  if (d.isPlaying) {
    if (strlen(d.currentLyric2) > 0) {
      int nextY = drawLyricLineLb(d.currentLyric1, 106, d.activeWordIdx1, d.lyricAlpha);
      drawLyricLineLb(d.currentLyric2, nextY, d.activeWordIdx2, d.lyricAlpha);
    } else {
      drawLyricLineLb(d.currentLyric1, 114, d.activeWordIdx1, d.lyricAlpha);
    }
  }

  drawVine(d);

#if defined(COVER_DEBUG)
  // Cover download diagnostics: last result for the sharp (H) and fallback (L) cover, free heap / largest block in KB
  extern char coverStatus[2][16];
  char dbg[40];
  snprintf(dbg, sizeof(dbg), "%s %s %u/%u", coverStatus[0], coverStatus[1],
           (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
  sprite.setTextFont(1);
  sprite.setTextDatum(TL_DATUM);
  sprite.setTextColor(LB_TEXT, LB_BG);
  sprite.drawString(dbg, 2, 141);
#endif
}

// Playback control feedback: a bubble over the album art (or a speech bubble for errors)
inline void drawControlOverlay(ControlIcon icon, const char *msg) {
  if (icon == ControlIcon::None) return;

  if (icon == ControlIcon::Message) {
    sprite.setTextFont(2);
    int w = sprite.textWidth(msg) + 16;
    if (w > 152) w = 152;
    int x = LB_W / 2 - w / 2;
    // Clear the lyrics behind the bubble (keeping the polka dots)
    sprite.fillRect(0, 105, LB_W, 37, LB_BG);
    sprite.setClipRect(0, 105, LB_W, 37);
    drawPolkaDots();
    sprite.clearClipRect();
    sprite.fillRoundRect(x, 108, w, 24, 8, LB_CARD);
    sprite.drawRoundRect(x, 108, w, 24, 8, LB_RED);
    sprite.fillTriangle(LB_W / 2 - 4, 131, LB_W / 2 + 4, 131, LB_W / 2, 138, LB_CARD);
    sprite.drawLine(LB_W / 2 - 4, 132, LB_W / 2, 138, LB_RED);
    sprite.drawLine(LB_W / 2 + 4, 132, LB_W / 2, 138, LB_RED);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(LB_TEXT);
    sprite.drawString(msg, LB_W / 2, 120);
    return;
  }

  const int cx = 80, cy = 40;
  sprite.fillCircle(cx + 1, cy + 2, 23, LB_SHADOW);
  sprite.fillCircle(cx, cy, 23, LB_CARD);
  sprite.drawCircle(cx, cy, 23, LB_RED);
  sprite.drawCircle(cx, cy, 22, LB_RED);

  switch (icon) {
    case ControlIcon::Play:
      sprite.fillTriangle(cx - 6, cy - 11, cx - 6, cy + 11, cx + 12, cy, LB_RED);
      break;
    case ControlIcon::Pause:
      sprite.fillRoundRect(cx - 10, cy - 11, 7, 22, 2, LB_RED);
      sprite.fillRoundRect(cx + 3, cy - 11, 7, 22, 2, LB_RED);
      break;
    case ControlIcon::Next:
      sprite.fillTriangle(cx - 13, cy - 10, cx - 13, cy + 10, cx - 1, cy, LB_RED);
      sprite.fillTriangle(cx - 1, cy - 10, cx - 1, cy + 10, cx + 11, cy, LB_RED);
      sprite.fillRect(cx + 11, cy - 10, 3, 20, LB_RED);
      break;
    case ControlIcon::Previous:
      sprite.fillTriangle(cx + 13, cy - 10, cx + 13, cy + 10, cx + 1, cy, LB_RED);
      sprite.fillTriangle(cx + 1, cy - 10, cx + 1, cy + 10, cx - 11, cy, LB_RED);
      sprite.fillRect(cx - 14, cy - 10, 3, 20, LB_RED);
      break;
    default:
      break;
  }

  // Spots on the bubble, so it reads as a ladybug button
  sprite.fillCircle(cx - 15, cy - 13, 2, LB_BLACK);
  sprite.fillCircle(cx + 15, cy + 13, 2, LB_BLACK);
}
