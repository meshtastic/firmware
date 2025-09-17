#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./BatteryIconApplet.h"

using namespace NicheGraphics;

InkHUD::BatteryIconApplet::BatteryIconApplet()
{
    // Show at boot, if user has previously enabled the feature
    if (settings->optionalFeatures.batteryIcon)
        bringToForeground();

    // Register to our have BatteryIconApplet::onPowerStatusUpdate method called when new power info is available
    // This happens whether or not the battery icon feature is enabled
    powerStatusObserver.observe(&powerStatus->onNewStatus);
}

// We handle power status' even when the feature is disabled,
// so that we have up to date data ready if the feature is enabled later.
// Otherwise could be 30s before new status update, with weird battery value displayed
int InkHUD::BatteryIconApplet::onPowerStatusUpdate(const meshtastic::Status *status)
{
    // System applets are always active
    assert(isActive());

    // This method should only receive power statuses
    // If we get a different type of status, something has gone weird elsewhere
    assert(status->getStatusType() == STATUS_TYPE_POWER);

    meshtastic::PowerStatus *powerStatus = (meshtastic::PowerStatus *)status;

    // Get the new state of charge %, and round to the nearest 10%
    uint8_t newSocRounded = ((powerStatus->getBatteryChargePercent() + 5) / 10) * 10;

    // If rounded value has changed, trigger a display update
    // It's okay to requestUpdate before we store the new value, as the update won't run until next loop()
    // Don't trigger an update if the feature is disabled
    if (this->socRounded != newSocRounded && settings->optionalFeatures.batteryIcon)
        requestUpdate();

    // Store the new value
    this->socRounded = newSocRounded;

    return 0; // Tell Observable to continue informing other observers
}

void InkHUD::BatteryIconApplet::onRender()
{
    // Fill entire tile
    // - size of icon controlled by size of tile
    int16_t l = 0;
    int16_t t = 0;
    uint16_t w = width();
    int16_t h = height();

    // Clear the region beneath the tile
    // Most applets are drawing onto an empty frame buffer and don't need to do this
    // We do need to do this with the battery though, as it is an "overlay"
    fillRect(l, t, w, h, WHITE);

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

    sliceW = (sliceW * socRounded) / 100; // Apply percentage

    hatchRegion(sliceL, sliceT, sliceW, sliceH, 2, BLACK);
    drawRect(sliceL, sliceT, sliceW, sliceH, BLACK);
}

#endif