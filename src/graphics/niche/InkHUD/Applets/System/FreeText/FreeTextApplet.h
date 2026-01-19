#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

System Applet to send a message using a virtual keyboard

*/

#pragma once

#include "configuration.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include <string>
namespace NicheGraphics::InkHUD
{

class FreeTextApplet : public SystemApplet
{
  public:
    void onRender() override;
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

  protected:
    void drawInputField(uint16_t left, uint16_t top, uint16_t width, uint16_t height, std::string text);
    void drawKeyboard(uint16_t left, uint16_t top, uint16_t width, uint16_t height, uint16_t selectCol, uint8_t selectRow);

  private:
    static const uint8_t KBD_COLS = 11;
    static const uint8_t KBD_ROWS = 4;

    const char keys[KBD_COLS * KBD_ROWS] = {'1', '2', '3', '4', '5', '6', '7',  '8', '9', '0', '\b', 'q', 'w', 'e',   'r',
                                            't', 'y', 'u', 'i', 'o', 'p', '\n', 'a', 's', 'd', 'f',  'g', 'h', 'j',   'k',
                                            'l', '!', ' ', 'z', 'x', 'c', 'v',  'b', 'n', 'm', ',',  '.', '?', '\x1b'};

    // This array represents the widths of each key in points
    // 16 pt = line height of the text
    const uint16_t keyWidths[KBD_COLS * KBD_ROWS] = {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 24, 16, 16, 16, 16,
                                                     16, 16, 16, 16, 16, 16, 24, 16, 16, 16, 16, 16, 16, 16, 16,
                                                     16, 16, 24, 16, 16, 16, 16, 16, 16, 16, 10, 10, 12, 40};

    uint8_t selectCol = 0;
    uint8_t selectRow = 0;
};

} // namespace NicheGraphics::InkHUD

#endif
