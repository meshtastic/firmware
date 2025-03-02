#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./TipsApplet.h"

using namespace NicheGraphics;

InkHUD::TipsApplet::TipsApplet()
{
    // Grab the window manager singleton, for convenience
    windowManager = WindowManager::getInstance();
}

void InkHUD::TipsApplet::onRender()
{
    switch (tipQueue.front()) {
    case Tip::WELCOME:
        renderWelcome();
        break;

    case Tip::FINISH_SETUP: {
        setFont(fontLarge);
        printAt(0, 0, "Tip: Finish Setup");

        setFont(fontSmall);
        int16_t cursorY = fontLarge.lineHeight() * 1.5;
        printAt(0, cursorY, "- connect antenna");

        cursorY += fontSmall.lineHeight() * 1.2;
        printAt(0, cursorY, "- connect a client app");

        // Only if region not set
        if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
            cursorY += fontSmall.lineHeight() * 1.2;
            printAt(0, cursorY, "- set region");
        }

        // Only if tz not set
        if (!(*config.device.tzdef && config.device.tzdef[0] != 0)) {
            cursorY += fontSmall.lineHeight() * 1.2;
            printAt(0, cursorY, "- set timezone");
        }

        cursorY += fontSmall.lineHeight() * 1.5;
        printAt(0, cursorY, "More info at meshtastic.org");

        setFont(fontSmall);
        printAt(0, Y(1.0), "Press button to continue", LEFT, BOTTOM);
    } break;

    case Tip::SAFE_SHUTDOWN: {
        setFont(fontLarge);
        printAt(0, 0, "Tip: Shutdown");

        setFont(fontSmall);
        std::string shutdown;
        shutdown += "Before removing power, please shutdown from InkHUD menu, or a client app. \n";
        shutdown += "\n";
        shutdown += "This ensures data is saved.";
        printWrapped(0, fontLarge.lineHeight() * 1.5, width(), shutdown);

        printAt(0, Y(1.0), "Press button to continue", LEFT, BOTTOM);

    } break;

    case Tip::CUSTOMIZATION: {
        setFont(fontLarge);
        printAt(0, 0, "Tip: Customization");

        setFont(fontSmall);
        printWrapped(0, fontLarge.lineHeight() * 1.5, width(),
                     "Configure & control display with the InkHUD menu. Optional features, layout, rotation, and more.");

        printAt(0, Y(1.0), "Press button to continue", LEFT, BOTTOM);
    } break;

    case Tip::BUTTONS: {
        setFont(fontLarge);
        printAt(0, 0, "Tip: Buttons");

        setFont(fontSmall);
        int16_t cursorY = fontLarge.lineHeight() * 1.5;

        printAt(0, cursorY, "User Button");
        cursorY += fontSmall.lineHeight() * 1.2;
        printAt(0, cursorY, "- short press: next");
        cursorY += fontSmall.lineHeight() * 1.2;
        printAt(0, cursorY, "- long press: select / open menu");
        cursorY += fontSmall.lineHeight() * 1.5;

        printAt(0, Y(1.0), "Press button to continue", LEFT, BOTTOM);
    } break;

    case Tip::ROTATION: {
        setFont(fontLarge);
        printAt(0, 0, "Tip: Rotation");

        setFont(fontSmall);
        printWrapped(0, fontLarge.lineHeight() * 1.5, width(),
                     "To rotate the display, use the InkHUD menu. Long-press the user button > Options > Rotate.");

        printAt(0, Y(1.0), "Press button to continue", LEFT, BOTTOM);

        // Revert the "flip screen" setting, preventing this message showing again
        config.display.flip_screen = false;
        nodeDB->saveToDisk(SEGMENT_DEVICESTATE);
    } break;
    }
}

// This tip has its own render method, only because it's a big block of code
// Didn't want to clutter up the switch in onRender too much
void InkHUD::TipsApplet::renderWelcome()
{
    uint16_t padW = X(0.05);

    // Block 1 - logo & title
    // ========================

    // Logo size
    uint16_t logoWLimit = X(0.3);
    uint16_t logoHLimit = Y(0.3);
    uint16_t logoW = getLogoWidth(logoWLimit, logoHLimit);
    uint16_t logoH = getLogoHeight(logoWLimit, logoHLimit);

    // Title size
    setFont(fontLarge);
    std::string title;
    if (width() >= 200) // Future proofing: hide if *tiny* display
        title = "meshtastic.org";
    uint16_t titleW = getTextWidth(title);

    // Center the block
    // Desired effect: equal margin from display edge for logo left and title right
    int16_t block1Y = Y(0.3);
    int16_t block1CX = X(0.5) + (logoW / 2) - (titleW / 2);
    int16_t logoCX = block1CX - (logoW / 2) - (padW / 2);
    int16_t titleCX = block1CX + (titleW / 2) + (padW / 2);

    // Draw block
    drawLogo(logoCX, block1Y, logoW, logoH);
    printAt(titleCX, block1Y, title, CENTER, MIDDLE);

    // Block 2 - subtitle
    // =======================
    setFont(fontSmall);
    std::string subtitle = "InkHUD";
    if (width() >= 200)
        subtitle += "  -  A Heads-Up Display"; // Future proofing: narrower for tiny display
    printAt(X(0.5), Y(0.6), subtitle, CENTER, MIDDLE);

    // Block 3 - press to continue
    // ============================
    printAt(X(0.5), Y(1), "Press button to continue", CENTER, BOTTOM);
}

// Grab fullscreen tile, and lock the window manager, when applet is shown
void InkHUD::TipsApplet::onForeground()
{
    windowManager->lock(this);
    windowManager->claimFullscreen(this);
}

void InkHUD::TipsApplet::onBackground()
{
    windowManager->releaseFullscreen();
    windowManager->unlock(this);
}

void InkHUD::TipsApplet::onActivate()
{
    // Decide which tips (if any) should be shown to user after the boot screen

    // Welcome screen
    if (settings.tips.firstBoot)
        tipQueue.push_back(Tip::WELCOME);

    // Antenna, region, timezone
    // Shown at boot if region not yet set
    if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        tipQueue.push_back(Tip::FINISH_SETUP);

    // Shutdown info
    // Shown until user performs one valid shutdown
    if (!settings.tips.safeShutdownSeen)
        tipQueue.push_back(Tip::SAFE_SHUTDOWN);

    // Using the UI
    if (settings.tips.firstBoot) {
        tipQueue.push_back(Tip::CUSTOMIZATION);
        tipQueue.push_back(Tip::BUTTONS);
    }

    // Catch an incorrect attempt at rotating display
    if (config.display.flip_screen)
        tipQueue.push_back(Tip::ROTATION);

    // Applet will be brought to foreground when boot screen closes, via TipsApplet::onLockAvailable
}

// While our applet has the window manager locked, we will receive the button input
void InkHUD::TipsApplet::onButtonShortPress()
{
    tipQueue.pop_front();

    // All tips done
    if (tipQueue.empty()) {
        // Record that user has now seen the "tutorial" set of tips
        // Don't show them on subsequent boots
        if (settings.tips.firstBoot) {
            settings.tips.firstBoot = false;
            saveDataToFlash();
        }

        // Close applet, and full refresh to clean the screen
        // Need to force update, because our request would be ignored otherwise, as we are now background
        sendToBackground();
        windowManager->forceUpdate(EInk::UpdateTypes::FULL);
    }

    // More tips left
    else
        requestUpdate();
}

// If the wm lock has just become availale (rendering, input), and we've still got tips, grab it!
// This situation would arise if bluetooth pairing occurs while TipsApplet was already shown (after pairing)
// Note: this event is only raised when *other* applets unlock the window manager
void InkHUD::TipsApplet::onLockAvailable()
{
    if (!tipQueue.empty())
        bringToForeground();
}

#endif