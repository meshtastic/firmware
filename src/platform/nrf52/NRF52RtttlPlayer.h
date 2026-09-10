#pragma once

// Non-blocking RTTTL player for the nRF52 I2S speaker path (NRF52I2SOutput.h).
// Same parse algorithm as the NonBlockingRTTTL library, but drives
// NRF52I2SOutput::startTone/stopTone instead of tone()/noTone().

#include <Arduino.h>

class NRF52RtttlPlayer
{
  public:
    void begin(const char *songBuffer, uint8_t loopCount = 1, unsigned long loopGapMs = 1000);
    void play(); // call every tick
    void stop();
    bool isPlaying() const { return playing; }

  private:
    void nextNote();

    const char *buffer = "";
    const char *firstNote = "";
    uint8_t defaultDuration = 4;
    uint8_t defaultOctave = 6;
    int bpm = 63;
    long wholeNoteMs = 0;
    uint32_t noteStartAt = 0;
    uint32_t noteDurationMs = 0;
    unsigned long loopGap = 1000;
    uint8_t loopCount = 1;
    bool playing = false;
};

extern NRF52RtttlPlayer nrf52RtttlPlayer;
