#pragma once
#include <stdint.h>
#include <stdlib.h>

// Turns raw touch samples into taps and horizontal swipes.
// Feed it one sample per poll; it reports a gesture once the finger lifts.

enum class Gesture : uint8_t { None, Tap, SwipeLeft, SwipeRight };

class GestureDetector {
public:
  static constexpr int      SWIPE_MIN_PX    = 80;   // Horizontal travel for a swipe
  static constexpr int      TAP_MAX_MOVE_PX = 30;   // Wobble allowed for a tap
  static constexpr uint32_t TAP_MAX_MS      = 600;
  static constexpr uint32_t SWIPE_MAX_MS    = 1200;
  static constexpr uint8_t  RELEASE_SAMPLES = 2;    // Empty reads before the finger counts as lifted

  Gesture update(bool touched, int x, int y, uint32_t nowMs) {
    if (touched) {
      if (!_down) {
        _down = true;
        _x0 = x;
        _y0 = y;
        _t0 = nowMs;
      }
      _x = x;
      _y = y;
      _tLast = nowMs;
      _misses = 0;
      return Gesture::None;
    }

    if (!_down) return Gesture::None;
    if (++_misses < RELEASE_SAMPLES) return Gesture::None;  // Ride out single dropped reads mid-swipe

    _down = false;
    _misses = 0;

    int dx = _x - _x0;
    int dy = _y - _y0;
    uint32_t held = _tLast - _t0;

    if (abs(dx) >= SWIPE_MIN_PX && abs(dx) > 2 * abs(dy) && held <= SWIPE_MAX_MS) {
      return dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight;
    }
    if (abs(dx) <= TAP_MAX_MOVE_PX && abs(dy) <= TAP_MAX_MOVE_PX && held <= TAP_MAX_MS) {
      return Gesture::Tap;
    }
    return Gesture::None;
  }

  bool isTouching() const { return _down; }

private:
  bool     _down = false;
  uint8_t  _misses = 0;
  int      _x0 = 0, _y0 = 0, _x = 0, _y = 0;
  uint32_t _t0 = 0, _tLast = 0;
};
