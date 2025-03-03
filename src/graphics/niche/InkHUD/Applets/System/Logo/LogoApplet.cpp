#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./LogoApplet.h"

#include "mesh/NodeDB.h"

using namespace NicheGraphics;

InkHUD::LogoApplet::LogoApplet() : concurrency::OSThread("LogoApplet")
{
    OSThread::setIntervalFromNow(8 * 1000UL);
    OSThread::enabled = true;

    textLeft = "";
    textRight = "";
    textTitle = xstr(APP_VERSION_SHORT);
    fontTitle = fontSmall;

    bringToForeground();
    // This is then drawn with a FULL refresh by Renderer::begin
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
    SystemApplet::lockRendering = true;
    SystemApplet::lockRequests = true;
    SystemApplet::handleInput = true; // We don't actually use this input. Just blocking other applets from using it.
}

void InkHUD::LogoApplet::onBackground()
{
    SystemApplet::lockRendering = false;
    SystemApplet::lockRequests = false;
    SystemApplet::handleInput = false;

    // Need to force an update, as a polite request wouldn't be honored, seeing how we are now in the background
    // Usually, onBackground is followed by another applet's onForeground (which requests update), but not in this case
    inkhud->forceUpdate(EInk::UpdateTypes::FULL);
}

// Begin displaying the screen which is shown at shutdown
void InkHUD::LogoApplet::onShutdown()
{
    textLeft = "";
    textRight = "";
    textTitle = owner.short_name;
    fontTitle = fontLarge;

    bringToForeground();
    // This is then drawn by InkHUD::Events::onShutdown, with a blocking FULL update
}

int32_t InkHUD::LogoApplet::runOnce()
{
    sendToBackground();
    return OSThread::disable();
}

#endif