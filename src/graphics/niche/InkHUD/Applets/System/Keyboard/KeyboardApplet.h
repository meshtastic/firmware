#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

System Applet to render an on-screen keyboard

*/

#pragma once

#include "configuration.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include <string>

namespace NicheGraphics::InkHUD
{

class KeyboardApplet : public SystemApplet
{
  public:
    KeyboardApplet();

    void onRender(bool full) override;
    void onForeground() override;
    void onBackground() override;
    void onButtonShortPress() override;
    void onButtonLongPress() override;
    void onExitShort() override;
    void onExitLong() override;
    void onNavUp() override;
    void onNavDown() override;
    void onNavLeft() override;
    void onNavRight() override;
    bool onTouchPoint(uint16_t x, uint16_t y, bool longPress) override;

    static uint16_t getKeyboardHeight(); // used to set the keyboard tile height

  private:
    enum KeyCode : int16_t {
        KEY_NONE = -1,
        KEY_BACKSPACE = 256,
        KEY_SEND,
        KEY_EMOTE_TOGGLE,
        KEY_PUNCT_TOGGLE,
        KEY_ALPHA_TOGGLE,
        KEY_EMOTE_UP,
        KEY_EMOTE_DOWN,
        KEY_EMOTE_SLOT_BASE = 512
    };

    enum KeyboardMode : uint8_t { MODE_TEXT = 0, MODE_PUNCT = 1, MODE_EMOTE = 2 };

    void drawKey(uint8_t index, bool selected);
    void drawKeyLabel(uint16_t left, uint16_t top, uint16_t width, const std::string &label, Color color);
    bool getKeyBounds(uint8_t index, uint16_t &left, uint16_t &top, uint16_t &width, uint16_t &height);
    int16_t getKeyIndexAt(uint16_t x, uint16_t y);
    void inputSelectedKey(bool longPress);
    void inputKeyCode(int16_t keyCode, bool longPress);
    int16_t getKeyCodeAt(uint8_t index) const;
    uint16_t getKeyWidthAt(uint8_t index) const;
    std::string getKeyLabelAt(uint8_t index) const;
    bool isKeyEnabledAt(uint8_t index) const;
    void normalizeSelection();
    void togglePunctuationMode();
    void toggleEmoteMode();
    void pageEmotes(bool down);
    void requestFastKeyboardRefresh(bool full = false);
    bool showSelectionHighlight() const;

    static const uint8_t KBD_COLS = 11;
    static const uint8_t KBD_ROWS = 5;
    static const uint8_t KBD_KEY_COUNT = KBD_COLS * KBD_ROWS;
    static const uint8_t EMOTE_SLOT_COUNT = KBD_COLS * (KBD_ROWS - 1); // top 4 rows
    static constexpr uint8_t fontEmotes[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08, 0x09, 0x0B, 0x0C, 0x0E, 0x0F, 0x10, 0x11,
                                             0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
    static constexpr uint8_t fontEmoteCount = sizeof(fontEmotes) / sizeof(fontEmotes[0]);

    // Text keyboard (requested layout):
    // row 0: 1..0
    // row 1: q..p
    // row 2: a..l
    // row 3: EMO, z..m, DEL
    // row 4: !#1, comma, space, period, SEND
    const int16_t textKeys[KBD_KEY_COUNT] = {
        // row 0
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', KEY_NONE,
        // row 1
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', KEY_NONE,
        // row 2
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', KEY_NONE, KEY_NONE,
        // row 3
        KEY_EMOTE_TOGGLE, 'z', 'x', 'c', 'v', 'b', 'n', 'm', KEY_BACKSPACE, KEY_NONE, KEY_NONE,
        // row 4
        KEY_PUNCT_TOGGLE, ',', ' ', '.', KEY_SEND, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE};

    // Punctuation keyboard (toggle via !#1/ABC)
    const int16_t punctKeys[KBD_KEY_COUNT] = {
        // row 0
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', KEY_NONE,
        // row 1
        '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', KEY_NONE,
        // row 2
        '-', '_', '=', '+', '[', ']', '{', '}', '/', '?', KEY_NONE,
        // row 3
        KEY_EMOTE_TOGGLE, ';', ':', '\'', '"', '<', '>', '\\', KEY_BACKSPACE, KEY_NONE, KEY_NONE,
        // row 4
        KEY_ALPHA_TOGGLE, ',', ' ', '.', KEY_SEND, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE};

    const uint16_t typingKeyWidths[KBD_KEY_COUNT] = {// row 0
                                                     12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 0,
                                                     // row 1
                                                     12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 0,
                                                     // row 2
                                                     12, 12, 12, 12, 12, 12, 12, 12, 12, 0, 0,
                                                     // row 3
                                                     18, 12, 12, 12, 12, 12, 12, 12, 20, 0, 0,
                                                     // row 4
                                                     20, 12, 56, 12, 24, 0, 0, 0, 0, 0, 0};

    const uint16_t emoteKeyWidths[KBD_KEY_COUNT] = {// row 0
                                                    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
                                                    // row 1
                                                    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
                                                    // row 2
                                                    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
                                                    // row 3
                                                    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
                                                    // row 4 controls
                                                    14, 14, 18, 12, 40, 12, 20, 18, 0, 0, 0};

    uint8_t selectedKey = 0;
    uint8_t prevSelectedKey = 0;
    uint8_t emotePage = 0;
    KeyboardMode mode = MODE_TEXT;
    KeyboardMode lastTypingMode = MODE_TEXT;
    static constexpr uint8_t KEY_GAP_X = 3;
    static constexpr uint8_t KEY_GAP_Y = 4;
    static constexpr uint8_t KEY_RADIUS = 4;
};

} // namespace NicheGraphics::InkHUD

#endif
