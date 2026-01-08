#ifdef MESHTASTIC_INCLUDE_INKHUD
#include "./FreeTextApplet.h"

using namespace NicheGraphics;

InkHUD::FreeTextApplet::FreeTextApplet() 
{
    
}
void InkHUD::FreeTextApplet::onRender()
{
    setFont(fontSmall);
    std::string header = "Free Text Input";
    printAt(0, 0, header);
    // Draw the input box
    uint16_t yStartBox = 5 + fontSmall.lineHeight();
    uint16_t inputBoxH = Y(0.45) - 5;
    uint16_t inputBoxW = X(1.0);

    drawRect(0, yStartBox, inputBoxW, inputBoxH, BLACK);

    // Draw the keyboard
    uint16_t yStartKb = Y(0.55);
    /*uint16_t keyboardH = Y(0.45) ;
    uint16_t keyboardW = X(1.0);

    drawRect(0, yStartKb, keyboardW, keyboardH, BLACK);*/

    // Draw the keys
    float padding = 0.01;
    uint16_t keyW = (width() / 11 );
    uint16_t r = (width() % 10) / 10;
    uint16_t c = 0;
    uint16_t keyH = X(1.0) > Y(1.0) ? fontSmall.lineHeight() + Y(padding) : fontSmall.lineHeight() + (2 * X(padding));
    
    for (uint8_t i=0; i<FreeTextApplet::KEYBOARD_ROWS; i++) {
        for (uint8_t j=0; j<FreeTextApplet::KEYBOARD_COLS; j++) {
            char temp[2] = "\0";
            temp[0] = keyboardLayout[i][j];
            drawRect(c, yStartKb, keyW + r, keyH, BLACK);
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
    sendToBackground();
    inkhud->forceUpdate(EInk::UpdateTypes::FULL);
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
    sendToBackground();
    inkhud->forceUpdate(EInk::UpdateTypes::FULL);
}

void InkHUD::FreeTextApplet::onNavDown()
{
    sendToBackground();
    inkhud->forceUpdate(EInk::UpdateTypes::FULL);
}

void InkHUD::FreeTextApplet::onNavLeft()
{
    sendToBackground();
    inkhud->forceUpdate(EInk::UpdateTypes::FULL);
}

void InkHUD::FreeTextApplet::onNavRight()
{
    sendToBackground();
    inkhud->forceUpdate(EInk::UpdateTypes::FULL);
}
#endif