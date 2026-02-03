#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

System Applet to render an on-screeen keyboard

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

    static uint16_t getKeyboardHeight(); // used to set the keyboard tile height

  private:
    void drawKeyLabel(uint16_t left, uint16_t top, uint16_t width, char key, Color color);

    static const uint8_t KBD_COLS = 11;
    static const uint8_t KBD_ROWS = 4;

    const char keys[KBD_COLS * KBD_ROWS] = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\b',  // row 0
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '\n',  // row 1
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '!', ' ',   // row 2
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '?', '\x1b' // row 3
    };

    // This array represents the widths of each key in points
    // 16 pt = line height of the text
    const uint16_t keyWidths[KBD_COLS * KBD_ROWS] = {
        16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 24, // row 0
        16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 24, // row 1
        16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 24, // row 2
        16, 16, 16, 16, 16, 16, 16, 10, 10, 12, 40  // row 3
    };

    uint16_t rowWidths[KBD_ROWS];
    uint8_t selectedKey = 0; // selected key index
    uint8_t prevSelectedKey = 0;
};

} // namespace NicheGraphics::InkHUD

#endif
