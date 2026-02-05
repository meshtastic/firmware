#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./AlignStickApplet.h"

using namespace NicheGraphics;

InkHUD::AlignStickApplet::AlignStickApplet()
{
    if (!settings->joystick.aligned)
        bringToForeground();
}

void InkHUD::AlignStickApplet::onRender(bool full)
{
    setFont(fontMedium);
    printAt(0, 0, "Align Joystick:");
    setFont(fontSmall);
    std::string instructions = "Move joystick in the direction indicated";
    printWrapped(0, fontMedium.lineHeight() * 1.5, width(), instructions);

    // Size of the region in which the joystick graphic should fit
    uint16_t joyXLimit = X(0.8);
    uint16_t contentH = fontMedium.lineHeight() * 1.5 + fontSmall.lineHeight() * 1;
    if (getTextWidth(instructions) > width())
        contentH += fontSmall.lineHeight();
    uint16_t freeY = height() - contentH - fontSmall.lineHeight() * 1.2;
    uint16_t joyYLimit = freeY * 0.8;

    // Use the shorter of the two
    uint16_t joyWidth = joyXLimit < joyYLimit ? joyXLimit : joyYLimit;

    // Center the joystick graphic
    uint16_t centerX = X(0.5);
    uint16_t centerY = contentH + freeY * 0.5;

    // Draw joystick graphic
    drawStick(centerX, centerY, joyWidth);

    setFont(fontSmall);
    printAt(X(0.5), Y(1.0) - fontSmall.lineHeight() * 0.2, "Long press to skip", CENTER, BOTTOM);
}

// Draw a scalable joystick graphic
void InkHUD::AlignStickApplet::drawStick(uint16_t centerX, uint16_t centerY, uint16_t width)
{
    if (width < 9) // too small to draw
        return;

    else if (width < 40) { // only draw up arrow
        uint16_t chamfer = width < 20 ? 1 : 2;

        // Draw filled up arrow
        drawDirection(centerX, centerY - width / 4, Direction::UP, width, chamfer, BLACK);

    } else { // large enough to draw the full thing
        uint16_t chamfer = width < 80 ? 1 : 2;
        uint16_t stroke = 3; // pixels
        uint16_t arrowW = width * 0.22;
        uint16_t hollowW = arrowW - stroke * 2;

        // Draw center circle
        fillCircle((int16_t)centerX, (int16_t)centerY, (int16_t)(width * 0.2), BLACK);
        fillCircle((int16_t)centerX, (int16_t)centerY, (int16_t)(width * 0.2) - stroke, WHITE);

        // Draw filled up arrow
        drawDirection(centerX, centerY - width / 2, Direction::UP, arrowW, chamfer, BLACK);

        // Draw down arrow
        drawDirection(centerX, centerY + width / 2, Direction::DOWN, arrowW, chamfer, BLACK);
        drawDirection(centerX, centerY + width / 2 - stroke, Direction::DOWN, hollowW, 0, WHITE);

        // Draw left arrow
        drawDirection(centerX - width / 2, centerY, Direction::LEFT, arrowW, chamfer, BLACK);
        drawDirection(centerX - width / 2 + stroke, centerY, Direction::LEFT, hollowW, 0, WHITE);

        // Draw right arrow
        drawDirection(centerX + width / 2, centerY, Direction::RIGHT, arrowW, chamfer, BLACK);
        drawDirection(centerX + width / 2 - stroke, centerY, Direction::RIGHT, hollowW, 0, WHITE);
    }
}

// Draw a scalable joystick direction arrow
// a right-triangle with blunted tips
/*
            _ <--point
    ^      / \
    |     /   \
   size  /     \
    |   /       \
    v  |_________|

*/
void InkHUD::AlignStickApplet::drawDirection(uint16_t pointX, uint16_t pointY, Direction direction, uint16_t size,
                                             uint16_t chamfer, Color color)
{
    uint16_t chamferW = chamfer * 2 + 1;
    uint16_t triangleW = size - chamferW;

    // Draw arrow
    switch (direction) {
    case Direction::UP:
        fillRect(pointX - chamfer, pointY, chamferW, triangleW, color);
        fillRect(pointX - chamfer - triangleW, pointY + triangleW, chamferW + triangleW * 2, chamferW, color);
        fillTriangle(pointX - chamfer, pointY, pointX - chamfer - triangleW, pointY + triangleW, pointX - chamfer,
                     pointY + triangleW, color);
        fillTriangle(pointX + chamfer, pointY, pointX + chamfer + triangleW, pointY + triangleW, pointX + chamfer,
                     pointY + triangleW, color);
        break;
    case Direction::DOWN:
        fillRect(pointX - chamfer, pointY - triangleW + 1, chamferW, triangleW, color);
        fillRect(pointX - chamfer - triangleW, pointY - size + 1, chamferW + triangleW * 2, chamferW, color);
        fillTriangle(pointX - chamfer, pointY, pointX - chamfer - triangleW, pointY - triangleW, pointX - chamfer,
                     pointY - triangleW, color);
        fillTriangle(pointX + chamfer, pointY, pointX + chamfer + triangleW, pointY - triangleW, pointX + chamfer,
                     pointY - triangleW, color);
        break;
    case Direction::LEFT:
        fillRect(pointX, pointY - chamfer, triangleW, chamferW, color);
        fillRect(pointX + triangleW, pointY - chamfer - triangleW, chamferW, chamferW + triangleW * 2, color);
        fillTriangle(pointX, pointY - chamfer, pointX + triangleW, pointY - chamfer - triangleW, pointX + triangleW,
                     pointY - chamfer, color);
        fillTriangle(pointX, pointY + chamfer, pointX + triangleW, pointY + chamfer + triangleW, pointX + triangleW,
                     pointY + chamfer, color);
        break;
    case Direction::RIGHT:
        fillRect(pointX - triangleW + 1, pointY - chamfer, triangleW, chamferW, color);
        fillRect(pointX - size + 1, pointY - chamfer - triangleW, chamferW, chamferW + triangleW * 2, color);
        fillTriangle(pointX, pointY - chamfer, pointX - triangleW, pointY - chamfer - triangleW, pointX - triangleW,
                     pointY - chamfer, color);
        fillTriangle(pointX, pointY + chamfer, pointX - triangleW, pointY + chamfer + triangleW, pointX - triangleW,
                     pointY + chamfer, color);
        break;
    }
}

void InkHUD::AlignStickApplet::onForeground()
{
    // Prevent most other applets from requesting update, and skip their rendering entirely
    // Another system applet with a higher precedence can potentially ignore this
    SystemApplet::lockRendering = true;
    SystemApplet::lockRequests = true;

    handleInput = true; // Intercept the button input for our applet
}

void InkHUD::AlignStickApplet::onBackground()
{
    // Allow normal update behavior to resume
    SystemApplet::lockRendering = false;
    SystemApplet::lockRequests = false;
    SystemApplet::handleInput = false;

    // Need to force an update, as a polite request wouldn't be honored, seeing how we are now in the background
    // Usually, onBackground is followed by another applet's onForeground (which requests update), but not in this case
    inkhud->forceUpdate(EInk::UpdateTypes::FULL, true);
}

void InkHUD::AlignStickApplet::onButtonLongPress()
{
    sendToBackground();
}

void InkHUD::AlignStickApplet::onExitLong()
{
    sendToBackground();
}

void InkHUD::AlignStickApplet::onNavUp()
{
    settings->joystick.aligned = true;

    sendToBackground();
}

void InkHUD::AlignStickApplet::onNavDown()
{
    inkhud->rotateJoystick(2); // 180 deg
    settings->joystick.aligned = true;

    sendToBackground();
}

void InkHUD::AlignStickApplet::onNavLeft()
{
    inkhud->rotateJoystick(3); // 270 deg
    settings->joystick.aligned = true;

    sendToBackground();
}

void InkHUD::AlignStickApplet::onNavRight()
{
    inkhud->rotateJoystick(1); // 90 deg
    settings->joystick.aligned = true;

    sendToBackground();
}

#endif