#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./TipsApplet.h"

#include "graphics/niche/InkHUD/Persistence.h"

#include "main.h"

using namespace NicheGraphics;

InkHUD::TipsApplet::TipsApplet()
{
    bool needsRegion = (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET);

    bool showTutorialTips = (settings->tips.firstBoot || needsRegion);

    // Welcome screen
    if (showTutorialTips)
        tipQueue.push_back(Tip::WELCOME);

    // Finish setup
    if (needsRegion)
        tipQueue.push_back(Tip::FINISH_SETUP);

    // Using the UI
    if (showTutorialTips) {
        tipQueue.push_back(Tip::CUSTOMIZATION);
        tipQueue.push_back(Tip::BUTTONS);
    }

    // Shutdown info
    // Shown until user performs one valid shutdown
    if (!settings->tips.safeShutdownSeen)
        tipQueue.push_back(Tip::SAFE_SHUTDOWN);

    // Catch an incorrect attempt at rotating display
    if (config.display.flip_screen)
        tipQueue.push_back(Tip::ROTATION);

    // Region picker
    if (needsRegion)
        tipQueue.push_back(Tip::PICK_REGION);

    if (!tipQueue.empty())
        bringToForeground();
}

void InkHUD::TipsApplet::onRender(bool full)
{
    switch (tipQueue.front()) {
    case Tip::WELCOME:
        renderWelcome();
        break;

    case Tip::FINISH_SETUP: {
        setFont(fontMedium);
        const char *title = "Tip: Finish Setup";
        uint16_t h = getWrappedTextHeight(0, width(), title);
        printWrapped(0, 0, width(), title);

        setFont(fontSmall);
        int16_t cursorY = h + fontSmall.lineHeight();

        auto drawBullet = [&](const char *text) {
            uint16_t bh = getWrappedTextHeight(0, width(), text);
            printWrapped(0, cursorY, width(), text);
            cursorY += bh + (fontSmall.lineHeight() / 3);
        };

        drawBullet("- connect antenna");
        drawBullet("- connect a client app");

        if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
            drawBullet("- set region");

        if (!(*config.device.tzdef && config.device.tzdef[0] != 0))
            drawBullet("- set timezone");

        cursorY += fontSmall.lineHeight() / 2;
        drawBullet("More info at meshtastic.org");

        printAt(0, Y(1.0), "Press button to continue", LEFT, BOTTOM);
    } break;

    case Tip::PICK_REGION: {
        setFont(fontMedium);
        printAt(0, 0, "Set Region");

        setFont(fontSmall);
        printWrapped(0, fontMedium.lineHeight() * 1.5, width(), "Please select your LoRa region to complete setup.");

        printAt(0, Y(1.0), "Press button to choose", LEFT, BOTTOM);
    } break;

    case Tip::SAFE_SHUTDOWN: {
        setFont(fontMedium);

        const char *title = "Tip: Shutdown";
        uint16_t h = getWrappedTextHeight(0, width(), title);
        printWrapped(0, 0, width(), title);

        setFont(fontSmall);
        int16_t cursorY = h + fontSmall.lineHeight();

        const char *body = "Before removing power, please shut down from InkHUD menu, or a client app.\n\n"
                           "This ensures data is saved.";

        uint16_t bodyH = getWrappedTextHeight(0, width(), body);
        printWrapped(0, cursorY, width(), body);
        cursorY += bodyH + (fontSmall.lineHeight() / 2);

        printAt(0, Y(1.0), "Press button to continue", LEFT, BOTTOM);
    } break;

    case Tip::CUSTOMIZATION: {
        setFont(fontMedium);

        const char *title = "Tip: Customization";
        uint16_t h = getWrappedTextHeight(0, width(), title);
        printWrapped(0, 0, width(), title);

        setFont(fontSmall);
        int16_t cursorY = h + fontSmall.lineHeight();

        const char *body = "Configure & control display with the InkHUD menu. "
                           "Optional features, layout, rotation, and more.";

        uint16_t bodyH = getWrappedTextHeight(0, width(), body);
        printWrapped(0, cursorY, width(), body);
        cursorY += bodyH + (fontSmall.lineHeight() / 2);

        printAt(0, Y(1.0), "Press button to continue", LEFT, BOTTOM);
    } break;

    case Tip::BUTTONS: {
        setFont(fontMedium);

        const char *title = "Tip: Buttons";
        uint16_t h = getWrappedTextHeight(0, width(), title);
        printWrapped(0, 0, width(), title);

        setFont(fontSmall);
        int16_t cursorY = h + fontSmall.lineHeight();

        auto drawBullet = [&](const char *text) {
            uint16_t bh = getWrappedTextHeight(0, width(), text);
            printWrapped(0, cursorY, width(), text);
            cursorY += bh + (fontSmall.lineHeight() / 3);
        };

        if (!settings->joystick.enabled) {
            drawBullet("User Button");
            drawBullet("- short press: next");
            drawBullet("- long press: select or open menu");
        } else {
            drawBullet("Joystick");
            drawBullet("- press: open menu or select");
            drawBullet("Exit Button");
            drawBullet("- press: switch tile or close menu");
        }

        printAt(0, Y(1.0), "Press button to continue", LEFT, BOTTOM);
    } break;

    case Tip::ROTATION: {
        setFont(fontMedium);

        const char *title = "Tip: Rotation";
        uint16_t h = getWrappedTextHeight(0, width(), title);
        printWrapped(0, 0, width(), title);

        setFont(fontSmall);
        if (!settings->joystick.enabled) {
            int16_t cursorY = h + fontSmall.lineHeight();

            const char *body = "To rotate the display, use the InkHUD menu. "
                               "Long-press the user button > Options > Rotate.";

            uint16_t bh = getWrappedTextHeight(0, width(), body);
            printWrapped(0, cursorY, width(), body);
            cursorY += bh + (fontSmall.lineHeight() / 2);
        } else {
            printWrapped(0, fontMedium.lineHeight() * 1.5, width(),
                         "To rotate the display, use the InkHUD menu. Press the user button > Options > Rotate.");
        }

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

    // Detect portrait orientation
    bool portrait = height() > width();

    // Block 1 - logo & title
    // ========================

    // Logo size
    uint16_t logoWLimit = portrait ? X(0.5) : X(0.3);
    uint16_t logoHLimit = portrait ? Y(0.25) : Y(0.3);
    uint16_t logoW = getLogoWidth(logoWLimit, logoHLimit);
    uint16_t logoH = getLogoHeight(logoWLimit, logoHLimit);

    // Title size
    setFont(fontMedium);
    std::string title;
    if (width() >= 200) // Future proofing: hide if *tiny* display
        title = "meshtastic.org";
    uint16_t titleW = getTextWidth(title);

    // Center the block
    // Desired effect: equal margin from display edge for logo left and title right
    int16_t block1Y = portrait ? Y(0.2) : Y(0.3);
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
    printAt(X(0.5), portrait ? Y(0.45) : Y(0.6), subtitle, CENTER, MIDDLE);

    // Block 3 - press to continue
    // ============================
    printAt(X(0.5), Y(1), "Press button to continue", CENTER, BOTTOM);
}

void InkHUD::TipsApplet::onForeground()
{
    // Prevent most other applets from requesting update, and skip their rendering entirely
    // Another system applet with a higher precedence can potentially ignore this
    SystemApplet::lockRendering = true;
    SystemApplet::lockRequests = true;

    SystemApplet::handleInput = true; // Our applet should handle button input (unless another system applet grabs it first)
}

void InkHUD::TipsApplet::onBackground()
{
    // Allow normal update behavior to resume
    SystemApplet::lockRendering = false;
    SystemApplet::lockRequests = false;
    SystemApplet::handleInput = false;

    // Need to force an update, as a polite request wouldn't be honored, seeing how we are now in the background
    // Usually, onBackground is followed by another applet's onForeground (which requests update), but not in this case
    inkhud->forceUpdate(EInk::UpdateTypes::FULL, true);
}

// While our SystemApplet::handleInput flag is true
void InkHUD::TipsApplet::onButtonShortPress()
{
    bool needsRegion = (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET);
    // If we're prompting the user to pick a region, hand off to the menu
    if (!tipQueue.empty() && tipQueue.front() == Tip::PICK_REGION) {
        tipQueue.pop_front();

        // Signal InkHUD to open the menu on Region page
        inkhud->forceRegionMenu = true;

        // Close tips and open menu
        sendToBackground();
        inkhud->openMenu();
        return;
    }
    // Consume current tip
    tipQueue.pop_front();

    // All tips done
    if (tipQueue.empty()) {
        // Record that user has now seen the "tutorial" set of tips
        // Don't show them on subsequent boots
        if (settings->tips.firstBoot && !needsRegion) {
            settings->tips.firstBoot = false;
            inkhud->persistence->saveSettings();
        }

        // Close applet
        sendToBackground();
    } else {
        requestUpdate();
    }
}

// Functions the same as the user button in this instance
void InkHUD::TipsApplet::onExitShort()
{
    onButtonShortPress();
}

#endif
