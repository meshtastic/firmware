#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./BatteryIconApplet.h"

using namespace NicheGraphics;

void InkHUD::BatteryIconApplet::onActivate()
{
    bringToForeground(); // Testing only
}

void InkHUD::BatteryIconApplet::onDeactivate() {}

void InkHUD::BatteryIconApplet::render()
{
    // Todo: use real data. Currently drawing with dummy value of 100%

    // Fill entire tile
    // - size of icon controlled by size of tile
    uint8_t percent = 100;
    int16_t l = 0;
    int16_t t = 0;
    uint16_t w = width();
    int16_t h = height();

    // Vertical centerline
    const int16_t m = t + (h / 2);

    // =====================
    // Draw battery outline
    // =====================

    // Positive terminal "bump"
    const int16_t &bumpL = l;
    const uint16_t bumpH = h / 2;
    const int16_t bumpT = m - (bumpH / 2);
    constexpr uint16_t bumpW = 2;
    fillRect(bumpL, bumpT, bumpW, bumpH, BLACK);

    // Main body of battery
    const int16_t bodyL = bumpL + bumpW;
    const int16_t &bodyT = t;
    const int16_t &bodyH = h;
    const int16_t bodyW = w - bumpW;
    drawRect(bodyL, bodyT, bodyW, bodyH, BLACK);

    // Erase join between bump and body
    drawLine(bodyL, bumpT, bodyL, bumpT + bumpH - 1, WHITE);

    // ===================
    // Draw battery level
    // ===================

    constexpr int16_t slicePad = 2;
    const int16_t sliceL = bodyL + slicePad;
    const int16_t sliceT = bodyT + slicePad;
    const uint16_t sliceH = bodyH - (slicePad * 2);
    uint16_t sliceW = bodyW - (slicePad * 2);

    sliceW = (sliceW * percent) / 100; // Apply percentage

    if (percent > 10) {
        // Testing only
        hatchRegion(sliceL, sliceT, sliceW, sliceH, 2, BLACK);
        drawRect(sliceL, sliceT, sliceW, sliceH, BLACK);
    }
}

#endif