#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./TouchStatusApplet.h"

using namespace NicheGraphics;

InkHUD::TouchStatusApplet::TouchStatusApplet()
{
    alwaysRender = true;
}

void InkHUD::TouchStatusApplet::onRender(bool full)
{
    (void)full;

    if (inkhud->isTouchEnabled()) {
        return;
    }

    setFont(fontSmall);

    const uint16_t barH = fontSmall.lineHeight() + 4;
    const int16_t top = height() - barH;

    fillRect(0, top, width(), barH, WHITE);
    drawLine(0, top, width() - 1, top, BLACK);
    printAt(width() / 2, top + (barH / 2), "TOUCH OFF", CENTER, MIDDLE);
}

#endif
