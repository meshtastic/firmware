#ifdef MESHTASTIC_INCLUDE_INKHUD
#include "./FreeTextApplet.h"

using namespace NicheGraphics;

InkHUD::FreeTextApplet::FreeTextApplet()
{
    for (int i = 0; i < KEYBOARD_ROWS; i++) {
        for (int j = 0; j < KEYBOARD_COLS; j++) {
            if (keyboardLayout[i][j] == '\b') {
                keyboard[i][j] = {'<', BACKSPACE};
            } else if (keyboardLayout[i][j] == '\n') {
                keyboard[i][j] = {'>', ENTER};
            } else if (keyboardLayout[i][j] == '\x1b') {
                keyboard[i][j] = {'~', ESCAPE};
            } else {
                keyboard[i][j] = {keyboardLayout[i][j], NONE};
            }
        }
    }
}

void InkHUD::FreeTextApplet::onRender()
{
    setFont(fontSmall);
    std::string header = "Free Text";
    printAt(0, 0, header);
    // Draw the input box
    uint16_t yStartBox = 5 + fontSmall.lineHeight();
    uint16_t inputBoxH = Y(0.45) - 5;
    uint16_t inputBoxW = X(1.0);

    drawRect(0, yStartBox, inputBoxW, inputBoxH, BLACK);
    if (!inkhud->freetext.empty())
        // For very long words with narrow characters like 'l' at the right edge,
        // The text will jump down one line before going back to normal on the
        // next character typed.
        // Either it must be fixed here in in printWrapped() itself.
        printWrapped(2, yStartBox + 1, inputBoxW - 5, inkhud->freetext);
    // Draw the keyboard
    uint16_t yStartKb = Y(0.55);
    float padding = 0.01;
    uint16_t keyW = (width() / 11);
    uint16_t r = (width() % 10) / 10;
    uint16_t c = 0;
    uint16_t keyH = X(1.0) > Y(1.0) ? fontSmall.lineHeight() + Y(padding) : fontSmall.lineHeight() + (2 * X(padding));

    for (uint8_t i = 0; i < FreeTextApplet::KEYBOARD_ROWS; i++) {
        for (uint8_t j = 0; j < FreeTextApplet::KEYBOARD_COLS; j++) {
            char temp[2] = "\0";
            temp[0] = keyboardLayout[i][j];
            // drawRect(c, yStartKb, keyW + r, keyH, BLACK);
            if (keyCursorX == j && keyCursorY == i) {
                fillRect(c, yStartKb, keyW + r, keyH, BLACK);
                setTextColor(WHITE);
            } else {
                setTextColor(BLACK);
            }
            if (keyboardLayout[i][j] == '\b') {
                printAt(c + 2, yStartKb + 1, "<");
            } else if (keyboardLayout[i][j] == '\n') {
                printAt(c + 2, yStartKb + 1, ">");
            } else if (keyboardLayout[i][j] == ' ') {
                printAt(c + 2, yStartKb + 1, "_");
            } else if (keyboardLayout[i][j] == '\x1b') {
                printAt(c + 2, yStartKb + 1, "~");
            } else {
                printAt(c + 2, yStartKb + 1, temp);
            }
            c += (keyW + r);
        }
        c = 0;
        yStartKb += X(1.0) > Y(1.0) ? keyH : keyH + Y(padding);
    }
}

void InkHUD::FreeTextApplet::onForeground()
{
    // Prevent most other applets from requesting update, and skip their rendering entirely
    // Another system applet with a higher precedence can potentially ignore this
    SystemApplet::lockRendering = true;
    SystemApplet::lockRequests = true;

    handleInput = true; // Intercept the button input for our applet
}

void InkHUD::FreeTextApplet::onBackground()
{
    // Allow normal update behavior to resume
    SystemApplet::lockRendering = false;
    SystemApplet::lockRequests = false;
    SystemApplet::handleInput = false;
    // Need to force an update, as a polite request wouldn't be honored, seeing how we are now in the background
    // Usually, onBackground is followed by another applet's onForeground (which requests update), but not in this case
    inkhud->forceUpdate(EInk::UpdateTypes::FULL);
}

void InkHUD::FreeTextApplet::onButtonShortPress()
{

    char ch = keyboard[keyCursorY][keyCursorX].c;
    enum KEY_ACTIONS a = keyboard[keyCursorY][keyCursorX].action;
    if (a == BACKSPACE && !inkhud->freetext.empty()) {
        inkhud->freetext.pop_back();
        requestUpdate(EInk::UpdateTypes::FAST);
    } else if (a == ENTER) {
        // This will eventually lead to a save/send page;
    } else if (a == ESCAPE) {
        sendToBackground();
        requestUpdate(EInk::UpdateTypes::FAST);
    } else if (a == NONE) {
        inkhud->freetext += ch;
        requestUpdate(EInk::UpdateTypes::FAST);
    }
}

void InkHUD::FreeTextApplet::onButtonLongPress()
{
    sendToBackground();
    inkhud->forceUpdate(EInk::UpdateTypes::FULL);
}

void InkHUD::FreeTextApplet::onExitShort()
{
    sendToBackground();
    inkhud->forceUpdate(EInk::UpdateTypes::FULL);
}

void InkHUD::FreeTextApplet::onExitLong()
{
    sendToBackground();
    inkhud->forceUpdate(EInk::UpdateTypes::FULL);
}

void InkHUD::FreeTextApplet::onNavUp()
{
    if (keyCursorY == 0)
        keyCursorY = KEYBOARD_ROWS - 1;
    else
        keyCursorY--;
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

void InkHUD::FreeTextApplet::onNavDown()
{
    keyCursorY = (keyCursorY + 1) % KEYBOARD_ROWS;
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

void InkHUD::FreeTextApplet::onNavLeft()
{
    if (keyCursorX == 0)
        keyCursorX = KEYBOARD_COLS - 1;
    else
        keyCursorX--;
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

void InkHUD::FreeTextApplet::onNavRight()
{
    keyCursorX = (keyCursorX + 1) % KEYBOARD_COLS;
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}
#endif