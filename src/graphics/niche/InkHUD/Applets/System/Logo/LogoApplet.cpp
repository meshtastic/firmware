#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./LogoApplet.h"

using namespace NicheGraphics;

InkHUD::LogoApplet::LogoApplet() : concurrency::OSThread("LogoApplet")
{
    // Don't autostart the runOnce() timer
    OSThread::disable();
}

void InkHUD::LogoApplet::onRender()
{
    // Size  of the region which the logo should "scale to fit"
    uint16_t logoWLimit = X(0.8);
    uint16_t logoHLimit = Y(0.5);

    // Get the max width and height we can manage within the region, while still maintaining aspect ratio
    uint16_t logoW = getLogoWidth(logoWLimit, logoHLimit);
    uint16_t logoH = getLogoHeight(logoWLimit, logoHLimit);

    // Where to place the center of the logo
    int16_t logoCX = X(0.5);
    int16_t logoCY = Y(0.5 - 0.05);

    drawLogo(logoCX, logoCY, logoW, logoH);

    if (!textLeft.empty()) {
        setFont(fontSmall);
        printAt(0, 0, textLeft, LEFT, TOP);
    }

    if (!textRight.empty()) {
        setFont(fontSmall);
        printAt(X(1), 0, textRight, RIGHT, TOP);
    }

    if (!textTitle.empty()) {
        int16_t logoB = logoCY + (logoH / 2); // Bottom of the logo
        setFont(fontTitle);
        printAt(X(0.5), logoB + Y(0.1), textTitle, CENTER, TOP);
    }
}

void InkHUD::LogoApplet::onForeground()
{
    lockRendering();                                 // Prevent other applets from rendering while logo shows
    requestUpdate(Drivers::EInk::UpdateTypes::FULL); // Upgrades from the normal UNSPECIFIED refresh
}

void InkHUD::LogoApplet::onBackground()
{
    unlockRendering();                                     // Allow other applets to render, now that we're done
    requestUpdate(Drivers::EInk::UpdateTypes::FULL, true); // Use FULL instead of UNSPECIFIED, and redraw all applets
}

int32_t InkHUD::LogoApplet::runOnce()
{
    LOG_DEBUG("Sent to background by timer");
    sendToBackground();
    return OSThread::disable();
}

// Begin displaying the screen which is shown at startup
// Suggest EInk::await after calling this method
void InkHUD::LogoApplet::showBootScreen()
{
    OSThread::setIntervalFromNow(8 * 1000UL);
    OSThread::enabled = true;

    textLeft = "";
    textRight = "";
    textTitle = xstr(APP_VERSION_SHORT);
    fontTitle = fontSmall;

    bringToForeground();
    requestUpdate(Drivers::EInk::UpdateTypes::FULL, true);
}

// Begin displaying the screen which is shown at shutdown
// Needs EInk::await after calling this method, to ensure display updates before shutdown
void InkHUD::LogoApplet::showShutdownScreen()
{
    textLeft = "";
    textRight = "";
    textTitle = owner.short_name;
    fontTitle = fontLarge;

    bringToForeground();
    requestUpdate(Drivers::EInk::UpdateTypes::FULL, true);
}

#endif