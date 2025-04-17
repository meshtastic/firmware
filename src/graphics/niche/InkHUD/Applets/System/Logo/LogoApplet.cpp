#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./LogoApplet.h"

#include "mesh/NodeDB.h"

using namespace NicheGraphics;

InkHUD::LogoApplet::LogoApplet() : concurrency::OSThread("LogoApplet")
{
    OSThread::setIntervalFromNow(8 * 1000UL);
    OSThread::enabled = true;

    // During onboarding, show the default short name as well as the version string
    // This behavior assists manufacturers during mass production, and should not be modified without good reason
    if (!settings->tips.safeShutdownSeen) {
        fontTitle = fontLarge;
        textLeft = xstr(APP_VERSION_SHORT);
        textRight = owner.short_name;
        textTitle = "Meshtastic";
    } else {
        fontTitle = fontSmall;
        textLeft = "";
        textRight = "";
        textTitle = xstr(APP_VERSION_SHORT);
    }

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

    // Invert colors if black-on-white
    // Used during shutdown, to resport display health
    // Todo: handle this in InkHUD::Renderer instead
    if (inverted) {
        fillScreen(BLACK);
        setTextColor(WHITE);
    }

    drawLogo(logoCX, logoCY, logoW, logoH, inverted ? WHITE : BLACK);

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
    bringToForeground();

    textLeft = "";
    textRight = "";
    textTitle = "Shutting Down...";
    fontTitle = fontSmall;

    // Draw a shutting down screen, twice.
    // Once white on black, once black on white.
    // Intention is to restore display health.

    inverted = true;
    inkhud->forceUpdate(Drivers::EInk::FULL, false);
    delay(1000); // Cooldown. Back to back updates aren't great for health.
    inverted = false;
    inkhud->forceUpdate(Drivers::EInk::FULL, false);
    delay(1000); // Cooldown

    // Prepare for the powered-off screen now
    // We can change these values because the initial "shutting down" screen has already rendered at this point
    textLeft = "";
    textRight = "";
    textTitle = owner.short_name;
    fontTitle = fontLarge;

    // This is then drawn by InkHUD::Events::onShutdown, with a blocking FULL update, after InkHUD's flash write is complete
}

void InkHUD::LogoApplet::onReboot()
{
    bringToForeground();

    textLeft = "";
    textRight = "";
    textTitle = "Rebooting...";
    fontTitle = fontSmall;

    inkhud->forceUpdate(Drivers::EInk::FULL, false);
    // Perform the update right now, waiting here until complete
}

int32_t InkHUD::LogoApplet::runOnce()
{
    sendToBackground();
    return OSThread::disable();
}

#endif