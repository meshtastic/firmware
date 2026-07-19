#include "configuration.h"

#if HAS_SCREEN && defined(GAT562)
#include "graphics/GAT562Arcade.h"
#include "graphics/Screen.h"
#include "main.h"
#include <Arduino.h>
#include <CastleBoyApp.h>
#include <cstring>

namespace graphics
{

#if defined(TB_UP) && defined(TB_DOWN) && defined(TB_LEFT) && defined(TB_RIGHT) && defined(TB_PRESS)
#define GAT562_ARCADE_DIRECT_POLL 1
#endif

static bool isActiveLowPressed(uint8_t pin)
{
    return pin != 0xff && digitalRead(pin) == LOW;
}

GAT562Arcade &GAT562Arcade::instance()
{
    static GAT562Arcade inst;
    return inst;
}

GAT562Arcade::GAT562Arcade() : concurrency::OSThread("GAT562Arcade")
{
}

void GAT562Arcade::start()
{
#if GAT562_ARCADE_DIRECT_POLL
    pinMode(TB_UP, INPUT_PULLUP);
    pinMode(TB_DOWN, INPUT_PULLUP);
    pinMode(TB_LEFT, INPUT_PULLUP);
    pinMode(TB_RIGHT, INPUT_PULLUP);
    pinMode(TB_PRESS, INPUT_PULLUP);
#endif
#if defined(PIN_BUTTON1)
    pinMode(PIN_BUTTON1, INPUT_PULLUP);
#endif
#if defined(GAT562_DEL_PIN)
    pinMode(GAT562_DEL_PIN, INPUT_PULLUP);
#endif
    CastleBoyApp::begin();
    CastleBoyApp::step(0);
    buttons = 0;
    heldButtons = 0;
    holdFrames = 0;
    active = true;
    if (screen) {
        screen->startAlert([](OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
            GAT562Arcade::instance().draw(display, state, x, y);
        });
        screen->runNow();
    }
    enabled = true;
    setIntervalFromNow(0);
}

void GAT562Arcade::stop()
{
    if (!active)
        return;

    active = false;
    buttons = 0;
    heldButtons = 0;
    holdFrames = 0;
    if (screen) {
        screen->endAlert();
        screen->runNow();
    }
}

int GAT562Arcade::handleInputEvent(const InputEvent *event)
{
    if (!active || !event)
        return 0;

#if GAT562_ARCADE_DIRECT_POLL
    switch (event->inputEvent) {
    case INPUT_BROKER_SELECT_LONG:
    case INPUT_BROKER_CANCEL:
    case INPUT_BROKER_ALT_LONG:
        stop();
        return 1;
    default:
        return 1;
    }
#else
    uint8_t mappedButton = 0;
    bool keepHeld = false;
    switch (event->inputEvent) {
    case INPUT_BROKER_LEFT:
        mappedButton = CastleBoyApp::Left;
        keepHeld = true;
        break;
    case INPUT_BROKER_RIGHT:
        mappedButton = CastleBoyApp::Right;
        keepHeld = true;
        break;
    case INPUT_BROKER_UP:
        mappedButton = CastleBoyApp::Up;
        keepHeld = true;
        break;
    case INPUT_BROKER_DOWN:
        mappedButton = CastleBoyApp::Down;
        keepHeld = true;
        break;
    case INPUT_BROKER_USER_PRESS:
        mappedButton = CastleBoyApp::A;
        break;
    case INPUT_BROKER_SELECT:
    case INPUT_BROKER_ALT_PRESS:
    case INPUT_BROKER_BACK:
        mappedButton = CastleBoyApp::B;
        break;
    case INPUT_BROKER_SELECT_LONG:
    case INPUT_BROKER_CANCEL:
    case INPUT_BROKER_ALT_LONG:
        stop();
        return 1;
    default:
        break;
    }

    if (mappedButton) {
        buttons = mappedButton;
        CastleBoyApp::step(buttons);
        if (keepHeld) {
            heldButtons = mappedButton;
            holdFrames = 3;
        } else {
            heldButtons = 0;
            holdFrames = 0;
        }
        buttons = 0;
    }

    if (screen)
        screen->runNow();
    return 1;
#endif
}

void GAT562Arcade::draw(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    (void)state;
    if (!display)
        return;

    const int16_t drawX = x + (display->getWidth() - 128) / 2;
    const int16_t drawY = y + (display->getHeight() - 64) / 2;
    const uint8_t *frame = CastleBoyApp::buffer();

    // Arduboy and OLEDDisplay use the same vertical page-buffer layout. The
    // normal GAT562 path can therefore copy one 1024-byte frame directly,
    // avoiding an XBM conversion plus two full 8192-pixel scans per refresh.
    if (drawX == 0 && drawY == 0 && display->getWidth() == 128 && display->getHeight() == 64 && display->buffer) {
        memcpy(display->buffer, frame, 128 * 64 / 8);
        return;
    }

    // Retain a geometry/transition-safe path for non-native offsets.
    display->clear();
    display->setColor(WHITE);
    display->drawFastImage(drawX, drawY, 128, 64, frame);
}

int32_t GAT562Arcade::runOnce()
{
    if (!active)
        return disable();

#if GAT562_ARCADE_DIRECT_POLL
    uint8_t polledButtons = 0;
    if (isActiveLowPressed(TB_LEFT))
        polledButtons |= CastleBoyApp::Left;
    if (isActiveLowPressed(TB_RIGHT))
        polledButtons |= CastleBoyApp::Right;
    if (isActiveLowPressed(TB_UP))
        polledButtons |= CastleBoyApp::Up;
    if (isActiveLowPressed(TB_DOWN))
        polledButtons |= CastleBoyApp::Down;
    if (isActiveLowPressed(TB_PRESS))
        polledButtons |= CastleBoyApp::B;
#if defined(PIN_BUTTON1)
    if (isActiveLowPressed(PIN_BUTTON1))
        polledButtons |= CastleBoyApp::A;
#endif
#if defined(GAT562_DEL_PIN)
    if (isActiveLowPressed(GAT562_DEL_PIN))
        polledButtons |= CastleBoyApp::B;
#endif
    CastleBoyApp::step(polledButtons);
    return 16;
#else
    if (holdFrames > 0) {
        buttons |= heldButtons;
        --holdFrames;
    } else {
        heldButtons = 0;
    }

    CastleBoyApp::step(buttons);
    buttons = 0;

    return 33;
#endif
}

} // namespace graphics
#endif
