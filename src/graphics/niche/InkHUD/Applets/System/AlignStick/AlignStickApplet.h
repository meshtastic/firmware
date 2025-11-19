#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

System Applet for manually aligning the joystick with the screen

should be run on at startup if the joystick is enabled
and not aligned to the screen

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/SystemApplet.h"

namespace NicheGraphics::InkHUD
{

class AlignStickApplet : public SystemApplet
{
  public:
    AlignStickApplet();

    void onRender() override;
    void onForeground() override;
    void onBackground() override;
    void onButtonLongPress() override;
    void onStickCenterLong() override;
    void onStickUp() override;
    void onStickDown() override;
    void onStickLeft() override;
    void onStickRight() override;

  protected:
    enum Direction {
        UP,
        DOWN,
        LEFT,
        RIGHT,
    };

    void drawStick(uint16_t centerX, uint16_t centerY, uint16_t width);
    void drawDirection(uint16_t pointX, uint16_t pointY, Direction direction, uint16_t size, uint16_t chamfer, Color color);
};

} // namespace NicheGraphics::InkHUD

#endif