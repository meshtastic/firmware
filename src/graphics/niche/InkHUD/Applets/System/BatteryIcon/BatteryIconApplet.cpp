#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./BatteryIconApplet.h"

using namespace NicheGraphics;

InkHUD::BatteryIconApplet::BatteryIconApplet()
{
    alwaysRender = true; // render everytime the screen is updated

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

void InkHUD::BatteryIconApplet::onRender(bool full)
{
    // Clear the region beneath the tile, including the border
    // Most applets are drawing onto an empty frame buffer and don't need to do this
    // We do need to do this with the battery though, as it is an "overlay"
    fillRect(0, 0, width(), height(), WHITE);

    // =====================
    // Draw battery outline
    // =====================

    // Positive terminal "bump"
    constexpr uint16_t bumpW = 2;
    const int16_t &bumpL = 1;
    const uint16_t bumpH = (height() - 2) / 2;
    const int16_t bumpT = (1 + ((height() - 2) / 2)) - (bumpH / 2);
    fillRect(bumpL, bumpT, bumpW, bumpH, BLACK);

    // Main body of battery
    const int16_t bodyL = 1 + bumpW;
    const int16_t &bodyT = 1;
    const int16_t &bodyH = height() - 2;         // Handle top/bottom padding
    const int16_t bodyW = (width() - 1) - bumpW; // Handle 1px left pad
    drawRect(bodyL, bodyT, bodyW, bodyH, BLACK);

    // Erase join between bump and body
    drawLine(bodyL, bumpT, bodyL, bumpT + bumpH - 1, WHITE);

    // ===================
    // Draw battery level
    // ===================

    constexpr int16_t slicePad = 2;
    int16_t sliceL = bodyL + slicePad;
    const int16_t sliceT = bodyT + slicePad;
    const uint16_t sliceH = bodyH - (slicePad * 2);
    uint16_t sliceW = bodyW - (slicePad * 2);

    sliceW = (sliceW * socRounded) / 100;          // Apply percentage
    sliceL += ((bodyW - (slicePad * 2)) - sliceW); // Shift slice to the battery's negative terminal, correcting drain direction

    hatchRegion(sliceL, sliceT, sliceW, sliceH, 2, BLACK);
    drawRect(sliceL, sliceT, sliceW, sliceH, BLACK);
}

#endif