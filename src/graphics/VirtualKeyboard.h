/*
 * VirtualKeyboard.h
 * Author TSAO (hey@tsao.dev) 2025
 */

#pragma once

#ifdef MESHCORE
#include <Adafruit_SSD1306.h>
#include <helpers/ui/DisplayDriver.h>
#elif defined(MESHTASTIC)
#include "graphics/Screen.h"

#include <OLEDDisplay.h>
#endif

// Callback function type for key press events
typedef void (*KeyPressCallback)(const char *pressedKey, const char *currentText);

#ifdef VIRTUAL_KEYBOARD_EN
static const int VIRTUAL_KEYBOARD_ROWS = 4;
static const int VIRTUAL_KEYBOARD_COLS = 11;

static const char *keyboardLayout[VIRTUAL_KEYBOARD_ROWS][VIRTUAL_KEYBOARD_COLS] = {
  { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "DEL" },
  { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "OK" },
  { "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "SPACE" },
  { "z", "x", "c", "v", "b", "n", "m", ".", ",", "?", "CAPS" },
};
#endif

#ifdef VIRTUAL_KEYBOARD_CN
static const int VIRTUAL_KEYBOARD_ROWS = 4;
static const int VIRTUAL_KEYBOARD_COLS = 11;

static const char *keyboardLayout[VIRTUAL_KEYBOARD_ROWS][VIRTUAL_KEYBOARD_COLS] = {
  { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "DEL" },
  { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "OK" },
  { "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "SPACE" },
  { "z", "x", "c", "v", "b", "n", "m", ".", ",", "?", "EN/CN" },
};
#endif

class VirtualKeyboard {
private:
#ifdef MESHCORE
  DisplayDriver *display;
#elif defined(MESHTASTIC)
  OLEDDisplay *display;
#endif

  int cursorRow = 0;
  int cursorColumn = 0;
  bool capsLockEnabled = false;

  char inputBuffer[160];
  int bufferLength = 0;

  // Keyboard positioning and dimensions
  int startX = 0;
  int startY = 0;
  int keyboardWidth = 0;
  int keyboardHeight = 0;

  // Callback function for key press events
  KeyPressCallback keyPressCallback = nullptr;

  void drawKeyboard();
  const char *getKeyText(int row, int col);

public:
#ifdef MESHCORE
  VirtualKeyboard(DisplayDriver *display, int startX = 0, int startY = 0, int keyboardWidth = 0,
                  int keyboardHeight = 0);
#elif defined(MESHTASTIC)
  VirtualKeyboard(OLEDDisplay *display, int startX = 0, int startY = 0, int keyboardWidth = 0,
                  int keyboardHeight = 0);
#endif

  void moveCursor(int deltaRow, int deltaColumn);
  void moveRight(int steps = 1);
  void moveLeft(int steps = 1);
  void moveUp(int steps = 1);
  void moveDown(int steps = 1);

  const char *pressCurrentKey();
  void clear();
  void reset();

  const char *getText() const { return inputBuffer; }
  int getTextLength() const { return bufferLength; }

  // Usage: kb.setKeyPressCallback([](const char* key, const char* text) { ... });
  void setKeyPressCallback(KeyPressCallback callback);

  void getTextBounds(const char *text, uint16_t *width, uint16_t *height);
  void render();

  int getRows() const { return VIRTUAL_KEYBOARD_ROWS; }
  int getCols() const { return VIRTUAL_KEYBOARD_COLS; }

  const char *getKeyAt(int row, int col) const {
    if (row >= 0 && row < VIRTUAL_KEYBOARD_ROWS && col >= 0 && col < VIRTUAL_KEYBOARD_COLS) {
      return keyboardLayout[row][col];
    }
    return "";
  }
};
