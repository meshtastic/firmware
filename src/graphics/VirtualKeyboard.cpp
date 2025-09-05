/*
 * VirtualKeyboard.cpp
 * Author TSAO (hey@tsao.dev) 2025
 */

#include "VirtualKeyboard.h"

#ifdef MESHCORE
#include "MeshCore.h"
#elif defined(MESHTASTIC)
#include "configuration.h"
#include "graphics/ScreenFonts.h"
#endif

#include <Arduino.h>
#include <string.h>

#ifdef MESHCORE
VirtualKeyboard::VirtualKeyboard(DisplayDriver *displayDriver, int startX, int startY, int keyboardWidth,
                                 int keyboardHeight)
    : display(displayDriver) {
#elif defined(MESHTASTIC)
VirtualKeyboard::VirtualKeyboard(OLEDDisplay *displayDriver, int startX, int startY, int keyboardWidth,
                                 int keyboardHeight)
    : display(displayDriver) {
#endif
  memset(inputBuffer, 0, sizeof(inputBuffer));
  bufferLength = 0;

  this->startX = startX;
  this->startY = startY;
  this->keyboardWidth = keyboardWidth;
  this->keyboardHeight = keyboardHeight;
}

void VirtualKeyboard::moveCursor(int deltaRow, int deltaColumn) {
  cursorRow += deltaRow;
  cursorColumn += deltaColumn;

  // Handle column overflow/underflow with row wrapping
  if (cursorColumn >= getCols()) {
    cursorColumn = 0;
    cursorRow++;
  } else if (cursorColumn < 0) {
    cursorColumn = getCols() - 1;
    cursorRow--;
  }

  // Handle row overflow/underflow
  if (cursorRow >= getRows()) {
    cursorRow = 0;
  } else if (cursorRow < 0) {
    cursorRow = getRows() - 1;
  }
}

void VirtualKeyboard::moveRight(int steps) {
  moveCursor(0, steps);
}

void VirtualKeyboard::moveLeft(int steps) {
  moveCursor(0, -steps);
}

void VirtualKeyboard::moveUp(int steps) {
  moveCursor(-steps, 0);
}

void VirtualKeyboard::moveDown(int steps) {
  moveCursor(steps, 0);
}

const char *VirtualKeyboard::getKeyText(int row, int column) {
  const char *keyString = getKeyAt(row, column);
  if (!keyString || strlen(keyString) == 0) {
    return "";
  }

  // Handle caps lock for letters
  if (capsLockEnabled && strlen(keyString) == 1 && keyString[0] >= 'a' && keyString[0] <= 'z') {
    static char uppercaseKey[2];
    uppercaseKey[0] = keyString[0] - 'a' + 'A';
    uppercaseKey[1] = '\0';
    return uppercaseKey;
  }

  return keyString;
}

const char *VirtualKeyboard::pressCurrentKey() {
  const char *pressedKey = getKeyText(cursorRow, cursorColumn);

  if (strcmp(pressedKey, "DEL") == 0) {
    // Delete/Backspace
    if (bufferLength > 0) {
      bufferLength--;
      inputBuffer[bufferLength] = '\0';
    }
  } else if (strcmp(pressedKey, "OK") == 0) {
    // Enter - could trigger submission or new line
    if (bufferLength < sizeof(inputBuffer) - 1) {
      inputBuffer[bufferLength++] = '\n';
      inputBuffer[bufferLength] = '\0';
    }
  } else if (strcmp(pressedKey, "SPACE") == 0) {
    // Space
    if (bufferLength < sizeof(inputBuffer) - 1) {
      inputBuffer[bufferLength++] = ' ';
      inputBuffer[bufferLength] = '\0';
    }
  } else if (strcmp(pressedKey, "CAPS") == 0) {
    // Toggle caps lock
    capsLockEnabled = !capsLockEnabled;
  } else {
    // Regular character
    int keyLength = strlen(pressedKey);
    if (bufferLength + keyLength < sizeof(inputBuffer) - 1) {
      strcat(inputBuffer, pressedKey);
      bufferLength += keyLength;
    }
  }

  // Call user callback if one is set
  if (keyPressCallback != nullptr) {
    keyPressCallback(pressedKey, inputBuffer);
  }

  return pressedKey;
}

void VirtualKeyboard::clear() {
  memset(inputBuffer, 0, sizeof(inputBuffer));
  bufferLength = 0;
}

void VirtualKeyboard::reset() {
  clear();
  cursorRow = 0;
  cursorColumn = 0;
}

void VirtualKeyboard::setKeyPressCallback(KeyPressCallback callback) {
  keyPressCallback = callback;
}

void VirtualKeyboard::getTextBounds(const char *text, uint16_t *width, uint16_t *height) {
  if (!display || !text) {
    if (width) *width = 0;
    if (height) *height = 0;
    return;
  }

#ifdef MESHCORE
  Adafruit_SSD1306 *ssd1306Display = static_cast<Adafruit_SSD1306 *>(display->getDisplay());
  if (ssd1306Display) {
    int16_t x1, y1;
    uint16_t w, h;
    ssd1306Display->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    if (width) *width = w;
    if (height) *height = h;
  } else {
    // Fallback if getDisplay() doesn't work
    if (width) *width = display->getTextWidth(text);
    if (height) *height = 8; // Default text height
  }
#elif defined(MESHTASTIC)
  if (width) *width = display->getStringWidth(text);
  // FONT_SMALL is 7 by 🧐
  if (height) *height = 7; // FONT_HEIGHT_SMALL;
#endif
}

void VirtualKeyboard::drawKeyboard() {
  if (!display) return;

#ifdef MESHCORE
  display->setTextSize(1);
#elif defined(MESHTASTIC)
  display->setFont(FONT_SMALL);
  // display->clear();
#endif

  // Calculate max standard key width (kw)
  uint16_t kw = 0;
  for (int row = 0; row < getRows(); row++) {
    for (int col = 0; col < getCols() - 1; col++) { // Exclude rightmost column (control keys)
      uint16_t keyWidth = 0;
      const char *keyText = getKeyText(row, col);
      getTextBounds(keyText, &keyWidth, nullptr);
      if (keyWidth > kw) {
        kw = keyWidth;
      }
    }
  }

  // Calculate max control key width (cw)
  uint16_t cw = 0;
  for (int row = 0; row < getRows(); row++) {
    int col = getCols() - 1; // Only check rightmost column
    uint16_t keyWidth = 0;
    const char *keyText = getKeyText(row, col);
    getTextBounds(keyText, &keyWidth, nullptr);
    if (keyWidth > cw) {
      cw = keyWidth;
    }
  }

  // Calculate horizontal spacing
  uint16_t totalKeyWidth = (getCols() - 1) * kw + cw;
  uint16_t spacingX = (keyboardWidth - totalKeyWidth) / (getCols() - 1);
  uint16_t fraction = (keyboardWidth - totalKeyWidth) % (getCols() - 1);

  // Calculate key height and vertical spacing
  uint16_t keyH = 0;
  getTextBounds(getKeyText(0, 0), nullptr, &keyH);
  uint16_t spacingY = (keyboardHeight - getRows() * keyH) / (getRows() - 1);

#ifdef MESHTASTIC
  spacingY = 2;
#endif

  for (int row = 0; row < getRows(); row++) {
    for (int col = 0; col < getCols(); col++) {
      const char *label = getKeyText(row, col);

      uint16_t currentKeyWidth = (col == getCols() - 1) ? cw : kw;

      // Calculate x position dynamically
      int currentX = startX + col * (kw + spacingX) + ((col == getCols() - 1) ? (cw - kw) : 0);

      if (col == getCols() - 1) {
        currentX = keyboardWidth - cw;
        // currentX += fraction * (getCols() - 1);
      }

      // Calculate y position
      int y = startY + row * keyH + row * spacingY;

      // Check if this is the currently selected key
      bool selected = (row == cursorRow && col == cursorColumn);

      if (selected) {
        // Highlight the selected key with inverted colors
#ifdef MESHCORE
        display->setColor(DisplayDriver::LIGHT);
        display->fillRect(currentX - 1, y - 1, currentKeyWidth + 2, keyH + 2);
        display->setColor(DisplayDriver::DARK); // Dark text on light background
#elif defined(MESHTASTIC)
        display->setColor(OLEDDISPLAY_COLOR::WHITE);
        if (col == 0 && startX > 0) {
          display->fillRect(currentX - 1, y + 2, currentKeyWidth + 1, keyH + 2);
        } else {
          display->fillRect(currentX - 2, y + 2, currentKeyWidth + 1, keyH + 2);
        }
        display->setColor(OLEDDISPLAY_COLOR::BLACK); // Dark text on light background
#endif
      } else {
#ifdef MESHCORE
        display->setColor(DisplayDriver::LIGHT); // Light text on dark background
#elif defined(MESHTASTIC)
        display->setColor(OLEDDISPLAY_COLOR::WHITE); // Light text on dark background
#endif
      }

      // Draw the key text at the key position
#ifdef MESHCORE
      display->setCursor(currentX, y);
      display->print(label);
#elif defined(MESHTASTIC)
      display->drawString(currentX, y, label);
#endif
    }
  }

  // Reset text color to default
#ifdef MESHCORE
  display->setColor(DisplayDriver::LIGHT);
#elif defined(MESHTASTIC)
  display->setColor(OLEDDISPLAY_COLOR::WHITE);
#endif
}

void VirtualKeyboard::render() {
  drawKeyboard();
}
