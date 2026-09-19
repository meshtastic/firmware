#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_I2C

#include "detect/ScanI2C.h"
#include <Wire.h>

// I2C buzzer found by the scanner: Arduino Modulino Buzzer or SparkFun Qwiic Buzzer. Both time the
// note themselves, so tone() never blocks; RTTTL parsing mirrors NRF52RtttlPlayer.
class I2CBuzzer
{
  public:
    explicit I2CBuzzer(const ScanI2C::FoundDevice &device);

    void tone(uint32_t frequencyHz, uint32_t durationMs);
    void noTone();

    void beginRtttl(const char *songBuffer);
    void playRtttl(); // call every tick
    void stopRtttl();
    bool isRtttlPlaying() const { return playing; }

  private:
    enum class Protocol { Modulino, Qwiic };

    void nextNote();
    bool writeBytes(const uint8_t *buf, size_t len);
    bool writeModulino(uint32_t frequencyHz, uint32_t durationMs);
    bool writeQwiic(uint16_t frequencyHz, uint16_t durationMs, bool active);

    TwoWire *wire;
    uint8_t address;
    Protocol protocol;

    const char *buffer = "";
    uint8_t defaultDuration = 4;
    uint8_t defaultOctave = 6;
    int bpm = 63;
    long wholeNoteMs = 0;
    uint32_t noteStartAt = 0;
    uint32_t noteDurationMs = 0;
    bool playing = false;
};

// Set by main.cpp when the I2C scan finds a buzzer; nullptr otherwise.
extern I2CBuzzer *i2cBuzzer;

#endif // !MESHTASTIC_EXCLUDE_I2C
