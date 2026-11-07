#pragma once
#include <cstdint>

// Backlight only: networking and the flight workers keep running in standby.
class DisplayWake {
 public:
  static constexpr uint32_t peekDurationMs=15000;
  void peek(uint32_t now) { peekStarted_=now; peeking_=true; }
  bool shouldLight(uint32_t now, bool liveAircraft, bool setupVisible) {
    // Unsigned elapsed time remains correct across the millis() rollover.
    if(peeking_ && uint32_t(now-peekStarted_)>=peekDurationMs) peeking_=false;
    return liveAircraft || setupVisible || peeking_;
  }
 private:
  uint32_t peekStarted_=0;
  bool peeking_=false;
};
