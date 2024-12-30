#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./BootScreenApplet.h"

using namespace NicheGraphics;

InkHUD::BootScreenApplet::BootScreenApplet() : concurrency::OSThread("BootScreenApplet")
{
    // Don't autostart the runOnce() timer
    OSThread::disable();
}

void InkHUD::BootScreenApplet::render()
{
    // Testing only
    print("Booting");
}

void InkHUD::BootScreenApplet::onForeground()
{
    // Testing only

    getTile()->displayedApplet = this; // Take ownership of the fullscreen tile
    requestUpdate();

    // Timer for 10 seconds
    OSThread::setIntervalFromNow(10 * 1000UL);
    OSThread::enabled = true;
}

void InkHUD::BootScreenApplet::onBackground()
{
    getTile()->displayedApplet = nullptr; // Release ownership of the fullscreen tile
}

int32_t InkHUD::BootScreenApplet::runOnce()
{
    LOG_DEBUG("End of boot screen");
    sendToBackground();
    requestUpdate(Drivers::EInk::UpdateTypes::FULL, true);
    return OSThread::disable();
}

#endif