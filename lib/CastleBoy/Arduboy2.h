#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <string.h>

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#endif

#define WIDTH 128
#define HEIGHT 64
#define BLACK 0
#define WHITE 1

#define LEFT_BUTTON  (1u << 0)
#define RIGHT_BUTTON (1u << 1)
#define UP_BUTTON    (1u << 2)
#define DOWN_BUTTON  (1u << 3)
#define A_BUTTON     (1u << 4)
#define B_BUTTON     (1u << 5)

class ArduboyAudioCompat {
  bool _enabled = true;
public:
  bool enabled() const { return _enabled; }
  void on() { _enabled = true; }
  void off() { _enabled = false; }
  void toggle() { _enabled = !_enabled; }
  void saveOnOff() {}
};

class Arduboy2Base {
  uint8_t _buffer[WIDTH * HEIGHT / 8] = {};
  uint8_t _buttons = 0;
  uint8_t _previous = 0;
  uint32_t _frame = 0;

  void drawPixel(int16_t x, int16_t y, uint8_t color);

public:
  ArduboyAudioCompat audio;

  void begin() {}
  void setFrameRate(uint8_t) {}
  bool nextFrame() { return true; }
  void clear() { memset(_buffer, 0, sizeof(_buffer)); }
  void display() {}
  void pollButtons() {}
  bool everyXFrames(uint8_t frames) const { return frames != 0 && (_frame % frames) == 0; }
  bool pressed(uint8_t mask) const { return (_buttons & mask) == mask; }
  bool justPressed(uint8_t mask) const { return (_buttons & mask) && !(_previous & mask); }
  uint8_t cpuLoad() const { return 0; }
  void fillRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color);

  void setButtons(uint8_t buttons) {
    _previous = _buttons;
    _buttons = buttons;
    ++_frame;
  }

  uint8_t *getBuffer() { return _buffer; }
  const uint8_t *getBuffer() const { return _buffer; }

  friend class Sprites;
};

class Sprites {
  static void drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame,
                         bool masked, bool overwrite);
public:
  static void drawOverwrite(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame);
  static void drawSelfMasked(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame);
  static void drawPlusMask(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame);
};

extern Arduboy2Base ab;
