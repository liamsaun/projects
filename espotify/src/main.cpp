#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TJpg_Decoder.h>

#include "config.h"
#include "display.h"
#include "data_structs.h"
#include "nvs_store.h"

SemaphoreHandle_t uiMutex;

// Spotify credentials loaded from NVS at boot
String spotifyAccessToken = "";
uint32_t tokenExpirationTime = 0;

// Globals
#if defined(DISPLAY_AMOLED_QSPI)
TFT_eSprite sprite;
TFT_eSprite &screen = sprite;   // Boot/setup text is drawn into the sprite, then pushed
#else
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);
TFT_eSPI &screen = tft;         // Boot/setup text goes straight to the panel
#endif

#if defined(UI_LADYBUG)
#include "ui_ladybug.h"
#else
#include "ui_volos.h"
#endif

SpotifyData spotify   = {};

// Smooth progress & lyrics interpolation
unsigned long lastProgressUpdateMillis = 0;
int lastKnownProgressMs = 0;
int lastKnownDurationMs = 0;
// 

uint32_t lastUpdate    = 0;
bool     forceRedraw   = true;

uint16_t albumArt[ALBUM_ART_W * ALBUM_ART_H] = {0}; // Cover art, laid out by the UI theme

// Screen Timeout
uint32_t lastMusicActivityMs = 0;  // tracks when music was last playing
bool     screenOff = false;

// Factory Reset Button (RESET_BTN_PIN is set per board in config.h)
#define RESET_HOLD_MS  3000
uint32_t btnPressStart = 0;
bool     btnWasPressed = false;

// Playback Controls (touch gestures -> Spotify task)
enum PlayerCommand : uint8_t { CMD_TOGGLE_PLAY, CMD_NEXT, CMD_PREVIOUS };
QueueHandle_t playerCmdQueue = nullptr;

// Feedback badge drawn over the UI after a gesture (guarded by uiMutex)
ControlIcon overlayIcon  = ControlIcon::None;
const char *overlayMsg   = "";
uint32_t    overlayUntil = 0;

#if defined(HAS_TOUCH)
#include "touch_cst9217.h"
#include "gestures.h"
GestureDetector gestures;
uint32_t lastTouchPollMs = 0;
bool     touchWokeScreen = false;
#endif

#if defined(UI_LADYBUG)
uint16_t albumArtHi[ALBUM_ART_HI * ALBUM_ART_HI];  // Sharp cover, big-endian RGB565
volatile bool albumArtHiReady = false;
bool coverDecodeHiRes = false;                      // Which buffer tjpg_callback fills
uint16_t coverSrcW = 0, coverSrcH = 0;
char coverStatus[2][16] = {"H:-", "L:-"};          // Last result per cover size, for COVER_DEBUG
#endif

bool tjpg_callback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
#if defined(UI_LADYBUG)
  if (coverDecodeHiRes) {
    storeCoverBlock(x, y, w, h, bitmap, coverSrcW, coverSrcH);
    return 1;
  }
#endif
  if (y >= 64 || x >= 64) return 1;
  for (int16_t j = 0; j < h; j++) {
    for (int16_t i = 0; i < w; i++) {
      uint16_t raw = bitmap[j * w + i];

#if defined(UI_LADYBUG)
      // 64x64 cover shown 1:1 on the card. TJpgDec gives true RGB565 and
      // the AMOLED is RGB-ordered, so only the byte order needs swapping.
      if (x + i < ALBUM_ART_W && y + j < ALBUM_ART_H) {
        albumArt[(y + j) * ALBUM_ART_W + (x + i)] = (raw >> 8) | (raw << 8);
      }
      continue;
#endif

      // Get the raw color from TJpgDec (outputting BGR565)
      
      // Swap the Red and Blue channels to convert BGR565 into true RGB565
      uint16_t color = ((raw & 0x001F) << 11) | (raw & 0x07E0) | ((raw & 0xF800) >> 11);
      
      int px = x + i;
      int py = y + j;
      
      // Calculate a smooth vertical vignette (darker at top and bottom, brighter in the middle)
      int distY = abs(py - 32); // 0 to 32
      // Quadratic falloff: center = 110 alpha, edges = 20 alpha
      uint8_t alpha = 20 + (90 * (1024 - distY * distY)) / 1024;
      
      // High-precision alpha blend with the vignette alpha
      uint16_t dimColor = alphaBlend565(alpha, color, 0x0000); 
      uint16_t swappedColor = (dimColor >> 8) | (dimColor << 8);
      
      // Scale 64x64 to 128x120 (fits exactly between y=24 and y=144)
      int startX = px * 2;
      int endX = startX + 2;
      
      int startY = (py * 120) / 64;
      int endY = ((py + 1) * 120) / 64;
      
      for (int sy = startY; sy < endY; sy++) {
        for (int sx = startX; sx < endX; sx++) {
           if (sx < 128 && sy < 120) {
             albumArt[sy * 128 + sx] = swappedColor; 
           }
        }
      }
    }
  }
  return 1;
}

// LRC Sync Globals
#define MAX_LYRICS 180
struct LyricLine {
  uint32_t timeMs;
  String text;
};
LyricLine lyrics[MAX_LYRICS];
int numLyrics = 0;
String currentTrackForLyrics = "";

bool isFadingLyric = false;
bool fadeDirectionIn = false;
String targetLyric1 = "";
String targetLyric2 = "";

//  Prototypes
void refreshSpotifyToken();
void pollSpotifyAPI();
void fetchLyrics(String track, String artist, String album = "", uint32_t durationMs = 0);
void updateSyncedLyric();
void spotifyTask(void *pvParameters);
void applyScreenTimeout(uint16_t seconds);

inline void blWrite(uint8_t val) {
#if defined(DISPLAY_AMOLED_QSPI)
  Amoled::setBrightness(val);   // No backlight on an AMOLED: sets panel brightness
#elif ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_BL, val);
#else
  ledcWrite(BL_CHANNEL, val);
#endif
}

// External Assets
const char b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
String base64Encode(String text) {
  String encoded = "";
  encoded.reserve((text.length() + 2) / 3 * 4 + 1);
  int i = 0, j = 0;
  uint8_t char_array_3[3], char_array_4[4];
  for (int idx = 0; idx < text.length(); idx++) {
    char_array_3[i++] = text[idx];
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;
      for (i = 0; (i < 4); i++) encoded += b64_alphabet[char_array_4[i]];
      i = 0;
    }
  }
  if (i > 0) {
    for (j = i; j < 3; j++) char_array_3[j] = '\0';
    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
    char_array_4[3] = char_array_3[2] & 0x3f;
    for (j = 0; (j < i + 1); j++) encoded += b64_alphabet[char_array_4[j]];
    while ((i++ < 3)) encoded += '=';
  }
  return encoded;
}

// Include web portal after base64Encode and spotifyAccessToken are defined
#include "web_portal.h"

// Screen Timeout
void applyScreenTimeout(uint16_t seconds) {
  // Just save the value - actual timeout logic runs in loop()
  lastMusicActivityMs = millis();
  if (screenOff) {
    screenOff = false;
    blWrite(NVStore::creds.brightness);
  }
}

void applyBrightness(uint8_t val) {
  if (!screenOff) {
    blWrite(val);
  }
}

void wakeScreen() {
  if (screenOff) {
    screenOff = false;
    blWrite(NVStore::creds.brightness);
    forceRedraw = true;
  }
  lastMusicActivityMs = millis();
}

// Saved wake pin, unless it collides with a pin the board needs (display, touch, USB)
uint8_t wakeButtonPin() {
  uint8_t pin = NVStore::creds.wakePin;
  if (pin >= 64 || (RESERVED_PIN_MASK & (1ULL << pin))) return DEFAULT_WAKE_PIN;
  return pin;
}

#if defined(HAS_TOUCH)
// Touch Controls: tap = play/pause, swipe left = next, swipe right = previous
void handleTouch(uint32_t now) {
  if (now - lastTouchPollMs < 15) return;
  lastTouchPollMs = now;

  int16_t x = 0, y = 0;
  bool touched = Touch::read(x, y);
  if (touched) {
    if (screenOff) touchWokeScreen = true;  // This touch only wakes the screen
    wakeScreen();                           // Also restarts the screen timeout
  }

  Gesture g = gestures.update(touched, x, y, now);
  if (g == Gesture::None) {
    if (!gestures.isTouching()) touchWokeScreen = false;
    return;
  }
  if (touchWokeScreen) {
    touchWokeScreen = false;
    return;
  }

  PlayerCommand cmd;
  xSemaphoreTake(uiMutex, portMAX_DELAY);
  if (g == Gesture::Tap) {
    cmd = CMD_TOGGLE_PLAY;
    overlayIcon = spotify.isPlaying ? ControlIcon::Pause : ControlIcon::Play;
  } else if (g == Gesture::SwipeLeft) {
    cmd = CMD_NEXT;
    overlayIcon = ControlIcon::Next;
  } else {
    cmd = CMD_PREVIOUS;
    overlayIcon = ControlIcon::Previous;
  }
  overlayUntil = now + 700;
  forceRedraw = true;
  xSemaphoreGive(uiMutex);

  xQueueSend(playerCmdQueue, &cmd, 0);  // Dropped if 4 are already waiting
}
#endif

// Setup - NVS-Driven Boot Flow
static uint32_t apShutdownTime = 0;
static bool spotifyTaskStarted = false;

void setup() {
  Serial.begin(115200);
  
  uiMutex = xSemaphoreCreateMutex();
  playerCmdQueue = xQueueCreate(4, sizeof(PlayerCommand));

  pinMode(RESET_BTN_PIN, INPUT_PULLUP);

  // Wake button setup (configurable GPIO, loaded after NVS)

#if defined(DISPLAY_AMOLED_QSPI)
  if (!Amoled::begin()) {
    Serial.println("[Boot] AMOLED init failed");
  }
#if defined(HAS_TOUCH)
  Touch::begin();
#endif
#else
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_BL, BL_FREQ, BL_RES);
#else
  ledcSetup(BL_CHANNEL, BL_FREQ, BL_RES);
  ledcAttachPin(PIN_BL, BL_CHANNEL);
#endif
#endif
  blWrite(BL_FULL);

#if !defined(DISPLAY_AMOLED_QSPI)
  tft.begin();
  tft.setRotation(SCREEN_ROT);
#endif
  sprite.createSprite(UI_W, UI_H);
  screen.fillScreen(TFT_BLACK);
  
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(tjpg_callback);

// Load NVS Credentials
  NVStore::load();

  if (!NVStore::hasWiFi()) {
    screen.setTextColor(TFT_WHITE, TFT_BLACK);
    screen.setTextFont(2);
    screen.drawString("ESPotify", 10, 10);
    screen.setTextFont(1);
    screen.setTextColor(0x07E0, TFT_BLACK);
    screen.drawString("Setup Mode", 10, 32);
    screen.setTextColor(TFT_WHITE, TFT_BLACK);
    screen.drawString("Connect WiFi to:", 10, 52);
    screen.setTextColor(0x07FF, TFT_BLACK);
    screen.drawString("ESPotify-Setup", 10, 66);
    screen.setTextColor(0x7BEF, TFT_BLACK);
    screen.drawString("Then open browser", 10, 86);
    displayShowScreen();

    Portal::startAP();
    Serial.println("[Boot] No WiFi creds → AP mode");
    return;  // Don't start Spotify task yet
  }

// WiFi Credentials Exist → Connect
  screen.setTextColor(TFT_WHITE);
  screen.drawString("WiFi...", 10, 10);
  displayShowScreen();
  
  WiFi.begin(NVStore::creds.wifiSSID.c_str(), NVStore::creds.wifiPass.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    screen.print(".");
    displayShowScreen();
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    // WiFi failedfall back to AP mode
    screen.fillScreen(TFT_BLACK);
    screen.setTextColor(0xF800, TFT_BLACK);
    screen.drawString("WiFi Failed!", 10, 10);
    screen.setTextColor(TFT_WHITE, TFT_BLACK);
    screen.setTextFont(1);
    screen.drawString("Starting setup...", 10, 30);
    displayShowScreen();
    
    NVStore::clearAll();  // Clear bad creds
    delay(1500);
    Portal::startAP();
    Serial.println("[Boot] WiFi failed → AP mode");
    return;
  }

  screen.fillScreen(TFT_BLACK);
  screen.setCursor(10, 10);
  screen.setTextColor(TFT_WHITE, TFT_BLACK);
  screen.println("WiFi Connected!");
  screen.println(WiFi.localIP());
  displayShowScreen();

  if (!NVStore::hasSpotify()) {
// PHASE 2: WiFi OK but no Spotify → Setup Page
    screen.setTextColor(0x07E0, TFT_BLACK);
    screen.println("");
    screen.println("Visit to setup:");
    screen.setTextColor(0x07FF, TFT_BLACK);
    screen.println(WiFi.localIP());
    displayShowScreen();
    
    Portal::startSTA();
    Serial.println("[Boot] No Spotify creds → serving setup page");
    return;  // Don't start Spotify task yet
  }
  delay(1000);

  // Start web server for controls
  Portal::startSTA();

  // Apply saved brightness
  blWrite(NVStore::creds.brightness);

  // Setup wake button pin
  if (wakeButtonPin() != RESET_BTN_PIN) {
    pinMode(wakeButtonPin(), INPUT_PULLUP);
  }

  // Initialize activity timer
  lastMusicActivityMs = millis();
  
  // Launch Spotify Polling Task on Core 1
  xTaskCreatePinnedToCore(
    spotifyTask,        // Task function
    "SpotifyTask",      // Task name
    12288,              // Stack size (bytes)
    NULL,               // Parameters
    1,                  // Priority
    NULL,               // Task handle
    SPOTIFY_TASK_CORE   // Core 1 (UI Core) on dual-core chips
  );
  spotifyTaskStarted = true;
}

// Loop
void loop() {
  uint32_t now = millis();

  displayService();

// Web Server (always runs)
  Portal::loop();

// AP Auto-Shutdown after WiFi connect
  if (Portal::setupComplete && Portal::apActive) {
    if (apShutdownTime == 0) {
      apShutdownTime = now + 5000;  // 5 seconds
    } else if (now >= apShutdownTime) {
      Portal::shutdownAP();
      apShutdownTime = 0;
      
      // If Spotify creds now exist, start Spotify task
      NVStore::load();
      if (NVStore::hasSpotify() && !spotifyTaskStarted) {
        xTaskCreatePinnedToCore(spotifyTask, "SpotifyTask", 12288, NULL, 1, NULL, SPOTIFY_TASK_CORE);
        spotifyTaskStarted = true;
      }
    }
  }

// Start Spotify task when creds become available
  if (!spotifyTaskStarted && NVStore::hasSpotify() && WiFi.status() == WL_CONNECTED) {
    screen.fillScreen(TFT_BLACK);
    screen.setTextColor(0x07E0, TFT_BLACK);
    screen.drawString("Setup Complete!", 10, 10);
    screen.setTextColor(TFT_WHITE, TFT_BLACK);
    screen.setTextFont(1);
    screen.drawString("Starting up...", 10, 30);
    displayShowScreen();
    delay(1500);
    lastMusicActivityMs = millis();
    xTaskCreatePinnedToCore(spotifyTask, "SpotifyTask", 12288, NULL, 1, NULL, SPOTIFY_TASK_CORE);
    spotifyTaskStarted = true;
  }

// If Spotify not configured, skip rendering
  if (!spotifyTaskStarted) {
    return;
  }

// Factory Reset: Long-Press Button
  if (digitalRead(RESET_BTN_PIN) == LOW) {
    if (!btnWasPressed) {
      btnWasPressed = true;
      btnPressStart = now;
    } else if (now - btnPressStart >= RESET_HOLD_MS) {
      // Show reset message on TFT
      sprite.fillSprite(TFT_BLACK);
      sprite.setTextDatum(TC_DATUM);
      sprite.setTextFont(2);
      sprite.setTextColor(0xF800);
      sprite.drawString("FACTORY RESET", UI_W / 2, 60);
      sprite.setTextFont(1);
      sprite.setTextColor(TFT_WHITE);
      sprite.drawString("Rebooting...", UI_W / 2, 85);
      displayPushFrame();
      delay(1500);
      NVStore::clearAll();
      ESP.restart();
    }
  } else {
    btnWasPressed = false;
  }

#if defined(HAS_TOUCH)
  handleTouch(now);
#endif

// Screen Timeout (based on music activity)
  if (NVStore::creds.screenTimeout > 0 && !screenOff) {
    // Only sleep if music is NOT playing AND timeout elapsed
    if (!spotify.isPlaying) {
      uint32_t currentMs = millis();
      // Ensure we don't underflow if currentMs < lastMusicActivityMs due to race conditions
      if (currentMs >= lastMusicActivityMs && (currentMs - lastMusicActivityMs >= (uint32_t)NVStore::creds.screenTimeout * 1000)) {
        screenOff = true;
        blWrite(BL_OFF);
      }
    } else {
      // Music is playing - keep resetting the timer
      lastMusicActivityMs = millis();
    }
  }

// Wake button check
  if (screenOff) {
    uint8_t wPin = wakeButtonPin();
    if (digitalRead(wPin) == LOW) {
      wakeScreen();
    }
  }

// Spotify UI Rendering
  if (screenOff) return;  // Skip rendering when screen is off
  
// Real-time Spotify Progress Interpolation & Lyric Checking
  if (lastKnownDurationMs > 0) {
    int interpolated = lastKnownProgressMs + (now - lastProgressUpdateMillis);
    if (interpolated > lastKnownDurationMs) {
      interpolated = lastKnownDurationMs;
    }
    spotify.progressMs = interpolated;
    
    // Check and trigger line transitions immediately on the interpolated timer
    updateSyncedLyric(); 
  }
// 

  xSemaphoreTake(uiMutex, portMAX_DELAY);
  
  bool isTitleScrolling = uiTitleScrolls(spotify.title);
  
  // Trigger fluid UI frames if we are actively viewing the playing Spotify layout
  bool isSpotifyAnimating = lastKnownDurationMs > 0;
  bool isOverlayShown = overlayIcon != ControlIcon::None;
  bool needsAnimation = isFadingLyric || isTitleScrolling || isSpotifyAnimating || isOverlayShown;

  // Draw screen every 1s OR if an animation is playing
  if (now - lastUpdate >= UPDATE_INTERVAL || forceRedraw || needsAnimation) {
    if (needsAnimation) {
       // Cap animation to ~30 FPS (33ms per frame)
       if (now - lastUpdate < 33) {
         xSemaphoreGive(uiMutex);
         return;
       }
    }
    
    if (isFadingLyric) {
       if (!fadeDirectionIn) {
         if (spotify.lyricAlpha >= 25) spotify.lyricAlpha -= 25;
         else {
           spotify.lyricAlpha = 0;
           fadeDirectionIn = true; // Hit bottom, swap text and fade back up
           strncpy(spotify.currentLyric1, targetLyric1.c_str(), sizeof(spotify.currentLyric1)-1);
           strncpy(spotify.currentLyric2, targetLyric2.c_str(), sizeof(spotify.currentLyric2)-1);
         }
       } else {
         if (spotify.lyricAlpha <= 230) spotify.lyricAlpha += 25;
         else {
           spotify.lyricAlpha = 255;
           isFadingLyric = false; // Animation finished
         }
       }
    }
    
    lastUpdate = now;
    forceRedraw = false;
    
    drawSpotify(spotify);

    if (isOverlayShown) {
      if ((int32_t)(now - overlayUntil) >= 0) {
        overlayIcon = ControlIcon::None;  // Expired: this frame redraws without it
      } else {
        drawControlOverlay(overlayIcon, overlayMsg);
      }
    }

    displayPushFrame();
  }
  
  xSemaphoreGive(uiMutex);
}

// LRC Parsing & Sanitization Logic
String urlEncode(const String& str) {
  String encodedString = "";
  encodedString.reserve(str.length() * 3);
  const char hexChars[] = "0123456789ABCDEF";
  for (size_t i = 0; i < str.length(); i++) {
    uint8_t c = (uint8_t)str.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      encodedString += (char)c;
    } else {
      encodedString += '%';
      encodedString += hexChars[(c >> 4) & 0x0F];
      encodedString += hexChars[c & 0x0F];
    }
  }
  return encodedString;
}

String cleanTrackTitle(String title) {
  // Normalize quotes and dashes
  title.replace("’", "'");
  title.replace("`", "'");
  title.replace("“", "\"");
  title.replace("”", "\"");
  title.replace("–", "-");
  title.replace("—", "-");

  // Cut common suffixes like " - Remastered", " - Live", " - Radio Edit", etc.
  int dashIdx = title.indexOf(" - ");
  if (dashIdx != -1) {
    String suffix = title.substring(dashIdx + 3);
    suffix.toLowerCase();
    if (suffix.indexOf("remaster") != -1 ||
        suffix.indexOf("live") != -1 ||
        suffix.indexOf("radio edit") != -1 ||
        suffix.indexOf("single version") != -1 ||
        suffix.indexOf("bonus") != -1 ||
        suffix.indexOf("deluxe") != -1 ||
        suffix.indexOf("mix") != -1 ||
        suffix.indexOf("version") != -1 ||
        suffix.indexOf("feat") != -1 ||
        suffix.indexOf("anniversary") != -1) {
      title = title.substring(0, dashIdx);
    }
  }

  // Remove feat/with/version inside brackets e.g. "(feat. ...)" or "[feat. ...]"
  auto stripPattern = [](String& str, const char* openB, const char* closeB) {
    int openIdx = str.indexOf(openB);
    while (openIdx != -1) {
      int closeIdx = str.indexOf(closeB, openIdx);
      if (closeIdx != -1) {
        String inside = str.substring(openIdx + 1, closeIdx);
        inside.toLowerCase();
        if (inside.indexOf("feat") != -1 ||
            inside.indexOf("with ") != -1 ||
            inside.indexOf("remaster") != -1 ||
            inside.indexOf("live") != -1 ||
            inside.indexOf("bonus") != -1 ||
            inside.indexOf("version") != -1) {
          str = str.substring(0, openIdx) + str.substring(closeIdx + 1);
          str.trim();
          openIdx = str.indexOf(openB);
          continue;
        }
      }
      openIdx = str.indexOf(openB, openIdx + 1);
    }
  };

  stripPattern(title, "(", ")");
  stripPattern(title, "[", "]");

  title.trim();
  return title;
}

String cleanArtistName(String artist) {
  int featIdx = artist.indexOf(" feat.");
  if (featIdx == -1) featIdx = artist.indexOf(" ft.");
  if (featIdx != -1) {
    artist = artist.substring(0, featIdx);
  }
  artist.trim();
  return artist;
}

void parseLrc(const String& lrc) {
  numLyrics = 0;
  int pos = 0;
  int len = lrc.length();

  while (pos < len && numLyrics < MAX_LYRICS) {
    int nextLine = lrc.indexOf('\n', pos);
    if (nextLine == -1) nextLine = len;

    String line = lrc.substring(pos, nextLine);
    pos = nextLine + 1;
    line.trim();
    if (line.length() == 0) continue;

    // Line might have multiple timestamp tags: [00:10.00][00:20.00]Chorus
    int textStart = 0;
    uint32_t timestamps[8];
    int tsCount = 0;

    int scanIdx = 0;
    while (scanIdx < line.length() && line[scanIdx] == '[') {
      int closeBracket = line.indexOf(']', scanIdx);
      if (closeBracket == -1) break;

      String tag = line.substring(scanIdx + 1, closeBracket);
      tag.trim();

      // Check if tag is timestamp: e.g. "01:23.45" or "01:23" or "1:23.456"
      int colonIdx = tag.indexOf(':');
      if (colonIdx > 0 && tsCount < 8) {
        String minStr = tag.substring(0, colonIdx);
        String secRest = tag.substring(colonIdx + 1);
        int dotIdx = secRest.indexOf('.');
        if (dotIdx == -1) dotIdx = secRest.indexOf(':');

        String secStr = (dotIdx != -1) ? secRest.substring(0, dotIdx) : secRest;
        String fracStr = (dotIdx != -1) ? secRest.substring(dotIdx + 1) : "";

        bool validTs = true;
        for (size_t c = 0; c < minStr.length(); c++) {
          if (!isDigit(minStr[c])) validTs = false;
        }
        for (size_t c = 0; c < secStr.length(); c++) {
          if (!isDigit(secStr[c])) validTs = false;
        }

        if (validTs && minStr.length() > 0 && secStr.length() > 0) {
          uint32_t m = minStr.toInt();
          uint32_t s = secStr.toInt();
          uint32_t ms = 0;
          if (fracStr.length() == 1) {
            ms = fracStr.toInt() * 100;
          } else if (fracStr.length() == 2) {
            ms = fracStr.toInt() * 10;
          } else if (fracStr.length() >= 3) {
            ms = fracStr.substring(0, 3).toInt();
          }

          timestamps[tsCount++] = (m * 60000) + (s * 1000) + ms;
          textStart = closeBracket + 1;
        }
      }
      scanIdx = closeBracket + 1;
      while (scanIdx < line.length() && line[scanIdx] == ' ') scanIdx++;
    }

    if (tsCount > 0) {
      String lyricText = "";
      if (textStart < line.length()) {
        lyricText = line.substring(textStart);
        lyricText.trim();
      }
      for (int t = 0; t < tsCount && numLyrics < MAX_LYRICS; t++) {
        lyrics[numLyrics].timeMs = timestamps[t];
        lyrics[numLyrics].text = lyricText;
        numLyrics++;
      }
    }
  }

  // Sort timestamps in ascending order if multi-timestamp tags were present
  for (int i = 0; i < numLyrics - 1; i++) {
    for (int j = i + 1; j < numLyrics; j++) {
      if (lyrics[j].timeMs < lyrics[i].timeMs) {
        LyricLine tmp = lyrics[i];
        lyrics[i] = lyrics[j];
        lyrics[j] = tmp;
      }
    }
  }
}

int executeLrclibGet(const String& url, String& responseBody) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(12000);

  HTTPClient http;
  http.begin(client, url);
  http.setUserAgent("ESPotify/1.0.0 (https://github.com/meritman/espotify)");
  http.addHeader("X-User-Agent", "ESPotify/1.0.0 (https://github.com/meritman/espotify)");
  http.addHeader("Lrclib-Client", "ESPotify/1.0.0 (https://github.com/meritman/espotify)");
  http.addHeader("Accept", "application/json");
  http.addHeader("Connection", "close");
  http.setTimeout(12000);

  const char* headerKeys[] = {"Retry-After"};
  http.collectHeaders(headerKeys, 1);

  int code = http.GET();

  // Handle 503 ServerOverloaded (retry once per LRCLIB documentation)
  if (code == 503) {
    String retryAfter = http.header("Retry-After");
    int waitMs = retryAfter.toInt() * 1000;
    if (waitMs <= 0 || waitMs > 3000) waitMs = 1000;
    http.end();
    delay(waitMs);

    http.begin(client, url);
    http.setUserAgent("ESPotify/1.0.0 (https://github.com/meritman/espotify)");
    http.addHeader("X-User-Agent", "ESPotify/1.0.0 (https://github.com/meritman/espotify)");
    http.addHeader("Lrclib-Client", "ESPotify/1.0.0 (https://github.com/meritman/espotify)");
    http.addHeader("Accept", "application/json");
    http.addHeader("Connection", "close");
    http.setTimeout(12000);
    code = http.GET();
  }

  if (code == 200) {
    responseBody = http.getString();
  }
  http.end();
  return code;
}

bool parseGetResult(const String& body, bool& isInstrumental) {
  isInstrumental = false;
  StaticJsonDocument<384> filter;
  filter["instrumental"] = true;
  filter["syncedLyrics"] = true;

  DynamicJsonDocument doc(6144);
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) return false;

  if (doc["instrumental"] == true) {
    isInstrumental = true;
    return true;
  }

  const char* synced = doc["syncedLyrics"];
  if (synced != nullptr && strlen(synced) >= 10) {
    parseLrc(String(synced));
    return (numLyrics > 0);
  }
  return false;
}

bool parseSearchResult(const String& body, uint32_t targetDurationSec, bool& isInstrumental) {
  isInstrumental = false;
  StaticJsonDocument<384> filter;
  filter[0]["instrumental"] = true;
  filter[0]["syncedLyrics"] = true;
  filter[0]["duration"] = true;

  DynamicJsonDocument doc(12288);
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err || !doc.is<JsonArray>()) return false;

  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() == 0) return false;

  int bestIdx = -1;
  float minDurationDiff = 99999.0f;
  bool foundInst = false;

  for (size_t i = 0; i < arr.size(); i++) {
    JsonObject item = arr[i];
    if (item["instrumental"] == true) foundInst = true;

    const char* sLrc = item["syncedLyrics"];
    if (sLrc != nullptr && strlen(sLrc) >= 10) {
      if (targetDurationSec > 0) {
        float itemDur = item["duration"] | 0.0f;
        float diff = fabs(itemDur - (float)targetDurationSec);
        if (diff < minDurationDiff) {
          minDurationDiff = diff;
          bestIdx = (int)i;
        }
      } else if (bestIdx == -1) {
        bestIdx = (int)i;
      }
    }
  }

  if (bestIdx != -1) {
    const char* sLrc = arr[bestIdx]["syncedLyrics"];
    parseLrc(String(sLrc));
    return (numLyrics > 0);
  }

  if (foundInst) {
    isInstrumental = true;
    return true;
  }

  return false;
}

void splitLyric(String text, String &l1, String &l2) {
  l1 = ""; l2 = "";
  text.trim();
  if (text.length() <= 18) {
    l1 = text;
    return;
  }
  int mid = text.length() / 2;
  int splitIndex = -1;
  int minDiff = 999;
  for (int i = 0; i < text.length(); i++) {
    if (text[i] == ' ') {
      int diff = abs(i - mid);
      if (diff < minDiff) {
        minDiff = diff;
        splitIndex = i;
      }
    }
  }
  if (splitIndex == -1 || minDiff > text.length()/3) {
    if (text.length() > 18) {
       l1 = text.substring(0, 18);
       l2 = text.substring(18);
    } else {
       l1 = text;
    }
  } else {
    l1 = text.substring(0, splitIndex);
    l2 = text.substring(splitIndex + 1);
  }
  l1.trim();
  l2.trim();
}

void fetchLyrics(String track, String artist, String album, uint32_t durationMs) {
  numLyrics = 0;

  xSemaphoreTake(uiMutex, portMAX_DELAY);
  strcpy(spotify.currentLyric1, "Searching lyrics...");
  strcpy(spotify.currentLyric2, "");
  spotify.lyricAlpha = 255;
  isFadingLyric = false;
  xSemaphoreGive(uiMutex);
  forceRedraw = true;

  if (WiFi.status() != WL_CONNECTED) {
    xSemaphoreTake(uiMutex, portMAX_DELAY);
    strcpy(spotify.currentLyric1, "No WiFi");
    strcpy(spotify.currentLyric2, "");
    xSemaphoreGive(uiMutex);
    return;
  }

  String cleanTitle = cleanTrackTitle(track);
  String cleanArtist = cleanArtistName(artist);
  uint32_t durationSec = durationMs / 1000;

  bool isInstrumental = false;
  bool found = false;
  String responseBody;

  // 1. Exact match with duration (if duration between 1 and 3600s per LRCLIB docs)
  if (durationSec > 0 && durationSec <= 3600) {
    String url = "https://lrclib.net/api/get?track_name=" + urlEncode(track) +
                 "&artist_name=" + urlEncode(artist) +
                 "&duration=" + String(durationSec);
    int code = executeLrclibGet(url, responseBody);
    if (code == 200) {
      found = parseGetResult(responseBody, isInstrumental);
    }
  }

  // 2. Exact match without duration
  if (!found && !isInstrumental) {
    delay(200); // 200ms spacing per rate-limit guidelines
    String url = "https://lrclib.net/api/get?track_name=" + urlEncode(track) +
                 "&artist_name=" + urlEncode(artist);
    int code = executeLrclibGet(url, responseBody);
    if (code == 200) {
      found = parseGetResult(responseBody, isInstrumental);
    }
  }

  // 3. Cleaned title match (if different from raw title)
  if (!found && !isInstrumental && (cleanTitle != track || cleanArtist != artist)) {
    delay(200);
    String url = "https://lrclib.net/api/get?track_name=" + urlEncode(cleanTitle) +
                 "&artist_name=" + urlEncode(cleanArtist);
    if (durationSec > 0 && durationSec <= 3600) {
      url += "&duration=" + String(durationSec);
    }
    int code = executeLrclibGet(url, responseBody);
    if (code == 200) {
      found = parseGetResult(responseBody, isInstrumental);
    }
  }

  // 4. Structured search fallback
  if (!found && !isInstrumental) {
    delay(200);
    String searchTitle = (cleanTitle.length() > 0) ? cleanTitle : track;
    String searchArtist = (cleanArtist.length() > 0) ? cleanArtist : artist;
    String url = "https://lrclib.net/api/search?track_name=" + urlEncode(searchTitle) +
                 "&artist_name=" + urlEncode(searchArtist);
    int code = executeLrclibGet(url, responseBody);
    if (code == 200) {
      found = parseSearchResult(responseBody, durationSec, isInstrumental);
    }
  }

  // 5. Query string search fallback (q=)
  if (!found && !isInstrumental) {
    delay(200);
    String q = cleanTitle + " " + cleanArtist;
    q.trim();
    if (q.length() > 0) {
      String url = "https://lrclib.net/api/search?q=" + urlEncode(q);
      int code = executeLrclibGet(url, responseBody);
      if (code == 200) {
        found = parseSearchResult(responseBody, durationSec, isInstrumental);
      }
    }
  }

  // Update UI state with results safely
  xSemaphoreTake(uiMutex, portMAX_DELAY);
  if (isInstrumental) {
    strcpy(spotify.currentLyric1, "♪ Instrumental ♪");
    strcpy(spotify.currentLyric2, "");
  } else if (found && numLyrics > 0) {
    strcpy(spotify.currentLyric1, "♪ Lyrics Synced ♪");
    strcpy(spotify.currentLyric2, "");
  } else {
    strcpy(spotify.currentLyric1, "Lyrics not found");
    strcpy(spotify.currentLyric2, "");
  }
  xSemaphoreGive(uiMutex);
}

void triggerLyricChange(String newText) {
  String l1, l2;
  if (newText.length() > 0) {
    splitLyric(newText, l1, l2);
  } else {
    l1 = "";
    l2 = "";
  }

  xSemaphoreTake(uiMutex, portMAX_DELAY);
  if (String(spotify.currentLyric1) != l1 || String(spotify.currentLyric2) != l2) {
     if (!isFadingLyric) {
        targetLyric1 = l1;
        targetLyric2 = l2;
        isFadingLyric = true;
        fadeDirectionIn = false;
     } else if (fadeDirectionIn) {
        targetLyric1 = l1;
        targetLyric2 = l2;
        fadeDirectionIn = false;
     }
  }
  xSemaphoreGive(uiMutex);
}

int splitWordsHelper(const String& str, String wordsOut[], int maxWords) {
  int count = 0;
  int start = 0;
  int len = str.length();
  while (start < len && count < maxWords) {
    while (start < len && str[start] == ' ') start++;
    if (start >= len) break;
    int end = str.indexOf(' ', start);
    if (end == -1) end = len;
    wordsOut[count++] = str.substring(start, end);
    start = end + 1;
  }
  return count;
}

void computeActiveWord(const String& fullText, uint32_t elapsed, uint32_t duration) {
  String l1, l2;
  splitLyric(fullText, l1, l2);

  String w1[16];
  int count1 = splitWordsHelper(l1, w1, 16);
  String w2[16];
  int count2 = splitWordsHelper(l2, w2, 16);

  int totalWords = count1 + count2;
  if (totalWords == 0 || duration == 0) {
    xSemaphoreTake(uiMutex, portMAX_DELAY);
    spotify.activeWordIdx1 = -1;
    spotify.activeWordIdx2 = -1;
    xSemaphoreGive(uiMutex);
    return;
  }

  // Length-weighted timing (longer words get proportionally more time)
  int weights[32];
  int totalWeight = 0;
  for (int i = 0; i < count1; i++) {
    int w = w1[i].length();
    if (w < 2) w = 2;
    weights[i] = w;
    totalWeight += w;
  }
  for (int i = 0; i < count2; i++) {
    int w = w2[i].length();
    if (w < 2) w = 2;
    weights[count1 + i] = w;
    totalWeight += w;
  }

  // Active singing duration estimation
  uint32_t estSingingMs = totalWords * 380;
  if (estSingingMs < 1200) estSingingMs = 1200;
  uint32_t activeDuration = (duration < estSingingMs) ? duration : estSingingMs;

  int idx1 = -1;
  int idx2 = -1;

  if (elapsed < activeDuration) {
    int currentWeightPos = (elapsed * totalWeight) / activeDuration;
    if (currentWeightPos >= totalWeight) currentWeightPos = totalWeight - 1;

    int activeGlobalIdx = 0;
    int accumWeight = 0;
    for (int i = 0; i < totalWords; i++) {
      accumWeight += weights[i];
      if (currentWeightPos < accumWeight) {
        activeGlobalIdx = i;
        break;
      }
    }

    if (activeGlobalIdx < count1) {
      idx1 = activeGlobalIdx;
      idx2 = -1;
    } else {
      idx1 = -1;
      idx2 = activeGlobalIdx - count1;
    }
  }

  xSemaphoreTake(uiMutex, portMAX_DELAY);
  spotify.activeWordIdx1 = idx1;
  spotify.activeWordIdx2 = idx2;
  xSemaphoreGive(uiMutex);
}

void updateSyncedLyric() {
  if (numLyrics == 0) {
    xSemaphoreTake(uiMutex, portMAX_DELAY);
    spotify.activeWordIdx1 = -1;
    spotify.activeWordIdx2 = -1;
    xSemaphoreGive(uiMutex);
    return;
  }

  // Natural lead time (250ms) for display anticipation
  uint32_t adjMs = spotify.progressMs + 250;

  if (adjMs < lyrics[0].timeMs) {
    triggerLyricChange("");
    xSemaphoreTake(uiMutex, portMAX_DELAY);
    spotify.activeWordIdx1 = -1;
    spotify.activeWordIdx2 = -1;
    xSemaphoreGive(uiMutex);
    return;
  }

  int matchedIndex = -1;
  for (int i = 0; i < numLyrics; i++) {
    if (lyrics[i].timeMs <= adjMs) {
      if (i == numLyrics - 1 || lyrics[i + 1].timeMs > adjMs) {
        matchedIndex = i;
        break;
      }
    }
  }

  if (matchedIndex != -1) {
    String lineText = lyrics[matchedIndex].text;
    triggerLyricChange(lineText);

    if (lineText.length() > 0) {
      uint32_t lineStart = lyrics[matchedIndex].timeMs;
      uint32_t lineEnd = (matchedIndex < numLyrics - 1) ? lyrics[matchedIndex + 1].timeMs : (lineStart + 4000);
      uint32_t lineDuration = (lineEnd > lineStart) ? (lineEnd - lineStart) : 3000;
      if (lineDuration > 6000) lineDuration = 6000;
      if (lineDuration < 800) lineDuration = 800;

      uint32_t elapsed = (adjMs >= lineStart) ? (adjMs - lineStart) : 0;
      computeActiveWord(lineText, elapsed, lineDuration);
    } else {
      xSemaphoreTake(uiMutex, portMAX_DELAY);
      spotify.activeWordIdx1 = -1;
      spotify.activeWordIdx2 = -1;
      xSemaphoreGive(uiMutex);
    }
  }
}

// Spotify Token Logic
void refreshSpotifyToken() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(15000);
  
  HTTPClient http;
  http.begin(client, "https://accounts.spotify.com/api/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  String auth = NVStore::creds.spClientId + ":" + NVStore::creds.spClientSecret;
  http.addHeader("Authorization", "Basic " + base64Encode(auth));

  String payload = "grant_type=refresh_token&refresh_token=" + NVStore::creds.spRefreshToken;
  
  const char * headerKeys[] = {"Retry-After"};
  http.collectHeaders(headerKeys, 1);
  int httpCode = http.POST(payload);

  if (httpCode == 200) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, http.getStream());
    if (!error) {
      spotifyAccessToken = doc["access_token"].as<String>();
      tokenExpirationTime = millis() + (doc["expires_in"].as<int>() * 1000) - 60000;
    } else {
      tokenExpirationTime = millis() + 10000; // Backoff on JSON error
    }
  } else if (httpCode == 429) {
    String retryAfterStr = http.header("Retry-After");
    int waitSecs = retryAfterStr.toInt();
    if (waitSecs <= 0) waitSecs = 60; // fallback
    tokenExpirationTime = millis() + (waitSecs * 1000);
    
    String err = "Wait " + String(waitSecs) + "s (429)";
    strncpy(spotify.title, err.c_str(), sizeof(spotify.title)-1);
    spotify.title[sizeof(spotify.title)-1] = '\0';
  } else {
    tokenExpirationTime = millis() + 10000;
  }
  http.end();
}

// Downloads and decodes a cover. hiRes decodes into albumArtHi (any size, scaled to fit).
bool fetchAlbumArt(String url, bool hiRes = false) {
#if defined(UI_LADYBUG)
  char *status = coverStatus[hiRes ? 0 : 1];
  const char tag = hiRes ? 'H' : 'L';
  snprintf(status, sizeof(coverStatus[0]), "%c:none", tag);
#endif
  if (url == "") return false;
  bool decoded = false;
  const int maxLen = hiRes ? 80000 : 20000;  // ~300x300 jpeg : 64x64 jpeg
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  int httpCode = http.GET();
  
#if defined(UI_LADYBUG)
  snprintf(status, sizeof(coverStatus[0]), "%c:http%d", tag, httpCode);
#endif
  if (httpCode == 200) {
    int len = http.getSize();
#if defined(UI_LADYBUG)
    snprintf(status, sizeof(coverStatus[0]), "%c:size%d", tag, len);
#endif
    if (len > 0 && len < maxLen) { // Safety check on jpeg size
      uint8_t * buff = (uint8_t *) malloc(len);
#if defined(UI_LADYBUG)
      if (!buff) snprintf(status, sizeof(coverStatus[0]), "%c:nomem%dk", tag, len / 1024);
#endif
      if (buff) {
        WiFiClient * stream = http.getStreamPtr();
        int bytesRead = 0;
        uint32_t startWait = millis();
        while (bytesRead < len && millis() - startWait < 5000) {
          if (stream->available()) {
            bytesRead += stream->read(&buff[bytesRead], len - bytesRead);
            startWait = millis();
          }
          delay(1);
        }
#if defined(UI_LADYBUG)
        if (bytesRead != len) snprintf(status, sizeof(coverStatus[0]), "%c:rd%d/%d", tag, bytesRead / 1024, len / 1024);
#endif
        if (bytesRead == len) {
#if defined(UI_LADYBUG)
          coverDecodeHiRes = hiRes;
          if (hiRes && TJpgDec.getJpgSize(&coverSrcW, &coverSrcH, buff, len) != JDR_OK) {
            coverSrcW = coverSrcH = 0;
          }
          JRESULT jr = JDR_FMT1;
          if (!hiRes || coverSrcW > 0) {
            jr = TJpgDec.drawJpg(0, 0, buff, len);
            decoded = jr == JDR_OK;
          }
          coverDecodeHiRes = false;
          if (decoded && hiRes) {
            // A decode that left the buffer one flat colour didn't really work
            bool flat = true;
            for (int i = 1; i < ALBUM_ART_HI * ALBUM_ART_HI && flat; i += 97) flat = albumArtHi[i] == albumArtHi[0];
            if (flat) decoded = false;
            snprintf(status, sizeof(coverStatus[0]), "H:%s%dx%d", flat ? "flat" : "ok", coverSrcW, coverSrcH);
          } else {
            snprintf(status, sizeof(coverStatus[0]), "%c:%s%d", tag, decoded ? "ok" : "jpg", decoded ? len / 1024 : (int)jr);
          }
#else
          decoded = TJpgDec.drawJpg(0, 0, buff, len) == JDR_OK;
#endif
        }
        free(buff);
      }
    }
  }
  http.end();
#if defined(UI_LADYBUG)
  Serial.printf("[Cover] %s %s heap=%u largest=%u\n", hiRes ? "hi" : "lo", status,
                (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif
  return decoded;
}

void pollSpotifyAPI() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  if (millis() > tokenExpirationTime) {
    refreshSpotifyToken();
  }
  
  if (spotifyAccessToken == "") {
     String err = "No Token (Wait)";
     strncpy(spotify.title, err.c_str(), sizeof(spotify.title)-1);
     spotify.title[sizeof(spotify.title)-1] = '\0';
     return;
  }

  // Track music state for auto-wake
  static bool wasPlaying = false;

  bool pendingFetch = false;
  String pendingTitle = "";
  String pendingArtist = "";
  String pendingAlbum = "";
  uint32_t pendingDurationMs = 0;
  String pendingCoverUrl = "";
  String pendingCoverUrlHi = "";  // 300x300, for themes that show a sharp cover
  int httpCode = 0;
  
  static uint32_t apiBackoff = 0;
  if (millis() < apiBackoff) {
     return; // Silently wait
  }
  
  {
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(15000);

    HTTPClient http;
    const char * headerKeys[] = {"Retry-After"};
    http.collectHeaders(headerKeys, 1);
    
    http.begin(client, "https://api.spotify.com/v1/me/player/currently-playing");
    http.addHeader("Authorization", "Bearer " + spotifyAccessToken);
    
    httpCode = http.GET();
  
    if (httpCode == 200) {
      StaticJsonDocument<512> filter;
      filter["is_playing"] = true;
      filter["progress_ms"] = true;
      filter["item"]["name"] = true;
      filter["item"]["artists"][0]["name"] = true;
      filter["item"]["album"]["name"] = true;
      filter["item"]["duration_ms"] = true;
      filter["item"]["album"]["images"][0]["url"] = true;
      filter["item"]["album"]["images"][2]["url"] = true;
      
      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
      
      xSemaphoreTake(uiMutex, portMAX_DELAY);
      if (error) {
        strcpy(spotify.title, "Parse Error");
        strcpy(spotify.artist, "");
      } else if (doc["is_playing"] == true) {
        spotify.isPlaying = true;
        // Auto-wake screen when music starts playing
        if (!wasPlaying && screenOff) {
          wakeScreen();
        }
        if (doc["item"].isNull()) {
          strcpy(spotify.title, "Ad / Local File");
          strcpy(spotify.artist, "");
        } else {
          String title = doc["item"]["name"].as<String>();
          String artist = doc["item"]["artists"][0]["name"].as<String>();
          String album = doc["item"]["album"]["name"].as<String>();
          
          strncpy(spotify.title, title.c_str(), sizeof(spotify.title)-1);
          spotify.title[sizeof(spotify.title)-1] = '\0';
          strncpy(spotify.artist, artist.c_str(), sizeof(spotify.artist)-1);
          spotify.artist[sizeof(spotify.artist)-1] = '\0';
          
          spotify.progressMs = doc["progress_ms"].as<uint32_t>();
          spotify.durationMs = doc["item"]["duration_ms"].as<uint32_t>();
          
// Sync Interpolation Anchors on Valid Network Metric
          lastKnownProgressMs = spotify.progressMs;
          lastKnownDurationMs = spotify.durationMs;
          lastProgressUpdateMillis = millis();
// 
          
          // Flag for lyric fetch after closing Spotify connection
          if (title != currentTrackForLyrics) {
            pendingTitle = title;
            pendingArtist = artist;
            pendingAlbum = album;
            pendingDurationMs = spotify.durationMs;
            pendingCoverUrl = doc["item"]["album"]["images"][2]["url"].as<String>();
            if (pendingCoverUrl == "null" || pendingCoverUrl.length() < 5) {
               pendingCoverUrl = doc["item"]["album"]["images"][0]["url"].as<String>();
            }
            if (pendingCoverUrl == "null") pendingCoverUrl = ""; // Handle missing art
            pendingCoverUrlHi = doc["item"]["album"]["images"][1]["url"].as<String>();
            if (pendingCoverUrlHi == "null") pendingCoverUrlHi = "";
            pendingFetch = true;
          }
        }
      } else {
        spotify.isPlaying = false;
        strcpy(spotify.title, "Paused");
        strcpy(spotify.artist, "");
        strcpy(spotify.currentLyric1, "");
        strcpy(spotify.currentLyric2, "");
        
        lastKnownDurationMs = 0; 
      }
      xSemaphoreGive(uiMutex);
    } else if (httpCode == 204) {
      xSemaphoreTake(uiMutex, portMAX_DELAY);
      spotify.isPlaying = false;
      strcpy(spotify.title, "Not Playing");
      strcpy(spotify.artist, "");
      strcpy(spotify.currentLyric1, "");
      strcpy(spotify.currentLyric2, "");
      
      lastKnownDurationMs = 0; 
      xSemaphoreGive(uiMutex);
    } else if (httpCode == 429) {
      String retryAfterStr = http.header("Retry-After");
      int waitSecs = retryAfterStr.toInt();
      if (waitSecs <= 0) waitSecs = 60;
      
      apiBackoff = millis() + (waitSecs * 1000); // Backoff exact seconds
      String err = "Wait " + String(waitSecs) + "s (429)";
      xSemaphoreTake(uiMutex, portMAX_DELAY);
      strncpy(spotify.title, err.c_str(), sizeof(spotify.title)-1);
      spotify.title[sizeof(spotify.title)-1] = '\0';
      strcpy(spotify.artist, "Rate limited");
      strcpy(spotify.currentLyric1, "");
      strcpy(spotify.currentLyric2, "");
      
      lastKnownDurationMs = 0; 
      xSemaphoreGive(uiMutex);
    } else if (httpCode < 0) {
      apiBackoff = millis() + 3000;
    } else {
      String err = "HTTP Error " + String(httpCode);
      xSemaphoreTake(uiMutex, portMAX_DELAY);
      strncpy(spotify.title, err.c_str(), sizeof(spotify.title)-1);
      spotify.title[sizeof(spotify.title)-1] = '\0';
      strcpy(spotify.artist, "");
      strcpy(spotify.currentLyric1, "");
      strcpy(spotify.currentLyric2, "");
      
      lastKnownDurationMs = 0; 
      xSemaphoreGive(uiMutex);
      apiBackoff = millis() + 5000; 
    }
    
    http.end();
  }
  
  if (pendingFetch) {
    currentTrackForLyrics = pendingTitle;
    
    memset(albumArt, 0, sizeof(albumArt));

#if defined(UI_LADYBUG)
    // Sharp 300x300 cover first; the 64x64 one if that can't be fetched or doesn't fit in memory
    albumArtHiReady = false;
    strcpy(coverStatus[1], "L:-");
    if (fetchAlbumArt(pendingCoverUrlHi, true)) {
      albumArtHiReady = true;
    } else {
      fetchAlbumArt(pendingCoverUrl);
    }
#else
    fetchAlbumArt(pendingCoverUrl);
#endif
    fetchLyrics(pendingTitle, pendingArtist, pendingAlbum, pendingDurationMs);
  }
  
  if (httpCode == 200 && pendingFetch == false) {
    updateSyncedLyric();
  }
  
  // Update music state tracking
  wasPlaying = spotify.isPlaying;

  forceRedraw = true;
}

// Playback Control (needs the user-modify-playback-state scope the setup page requests)
int sendPlayerRequest(const char *method, const char *endpoint) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(15000);

  HTTPClient http;
  http.begin(client, String("https://api.spotify.com/v1/me/player/") + endpoint);
  http.addHeader("Authorization", "Bearer " + spotifyAccessToken);
  http.addHeader("Content-Length", "0");  // Spotify rejects a body-less POST/PUT without it
  int code = http.sendRequest(method);
  http.end();
  return code;
}

void handlePlayerCommand(PlayerCommand cmd) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() > tokenExpirationTime || spotifyAccessToken == "") {
    refreshSpotifyToken();
  }
  if (spotifyAccessToken == "") return;

  xSemaphoreTake(uiMutex, portMAX_DELAY);
  bool wasPlaying = spotify.isPlaying;
  xSemaphoreGive(uiMutex);

  const char *method = "POST";
  const char *endpoint;
  if (cmd == CMD_NEXT) {
    endpoint = "next";
  } else if (cmd == CMD_PREVIOUS) {
    endpoint = "previous";
  } else {
    method = "PUT";
    endpoint = wasPlaying ? "pause" : "play";
  }

  int code = sendPlayerRequest(method, endpoint);
  if (code == 401) {
    // Token expired early: refresh once and retry
    refreshSpotifyToken();
    code = sendPlayerRequest(method, endpoint);
  }
  Serial.printf("[Control] %s %s -> %d\n", method, endpoint, code);

  const char *err = nullptr;
  if (code == 404)      err = "No active device";
  else if (code == 403) err = "Not allowed";
  else if (code == 429) err = "Rate limited";
  else if (code < 200 || code >= 300) err = "Control failed";

  xSemaphoreTake(uiMutex, portMAX_DELAY);
  if (err) {
    overlayIcon = ControlIcon::Message;
    overlayMsg = err;
    overlayUntil = millis() + 1500;
  } else if (cmd == CMD_TOGGLE_PLAY) {
    spotify.isPlaying = !wasPlaying;  // Keeps a quick second tap right until the next poll
  }
  forceRedraw = true;
  xSemaphoreGive(uiMutex);
}

// FreeRTOS Task (Core 0)
void spotifyTask(void *pvParameters) {
  while (true) {
    pollSpotifyAPI();

    // Sleep until the next poll, but wake early for touch controls
    PlayerCommand cmd;
    if (xQueueReceive(playerCmdQueue, &cmd, 3500 / portTICK_PERIOD_MS) == pdTRUE) {
      do {
        handlePlayerCommand(cmd);
      } while (xQueueReceive(playerCmdQueue, &cmd, 0) == pdTRUE);
      vTaskDelay(400 / portTICK_PERIOD_MS);  // Give Spotify a moment before re-polling
    }
  }
}