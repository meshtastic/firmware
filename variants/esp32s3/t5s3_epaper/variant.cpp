#include "configuration.h"

#ifdef T5_S3_EPAPER_PRO

#include "Observer.h"
#include "TouchDrvGT911.hpp"
#include "Wire.h"
#include "input/InputBroker.h"
#include "input/TouchScreenImpl1.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/SystemApplet.h"

// Bridges touch events from TouchScreenImpl1 directly into InkHUD,
// bypassing the InputBroker (which is excluded in InkHUD builds).
// Routing mirrors the mini-epaper-s3 two-way rocker pattern:
//   - Nav left/right: prevApplet/nextApplet when idle, navUp/Down when a system applet has focus (e.g. menu)
//   - Nav up/down:    navUp/navDown always (menu scroll)
//   - Tap:            shortpress (cycle applets / confirm in menu)
//   - Long press:     longpress (open menu / back)
class TouchInkHUDBridge : public Observer<const InputEvent *>
{
    int onNotify(const InputEvent *e) override
    {
        auto *inkhud = NicheGraphics::InkHUD::InkHUD::getInstance();

        // Check whether a system applet (e.g. menu) is currently handling input
        bool systemHandlingInput = false;
        for (NicheGraphics::InkHUD::SystemApplet *sa : inkhud->systemApplets) {
            if (sa->handleInput) {
                systemHandlingInput = true;
                break;
            }
        }

        switch (e->inputEvent) {
        case INPUT_BROKER_USER_PRESS:
            inkhud->shortpress();
            break;
        case INPUT_BROKER_SELECT:
            inkhud->longpress();
            break;
        case INPUT_BROKER_LEFT:
            if (systemHandlingInput)
                inkhud->navUp();
            else
                inkhud->prevApplet();
            break;
        case INPUT_BROKER_RIGHT:
            if (systemHandlingInput)
                inkhud->navDown();
            else
                inkhud->nextApplet();
            break;
        case INPUT_BROKER_UP:
            inkhud->navUp();
            break;
        case INPUT_BROKER_DOWN:
            inkhud->navDown();
            break;
        default:
            break;
        }
        return 0;
    }
};

static TouchInkHUDBridge touchBridge;
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS

TouchDrvGT911 touch;

bool readTouch(int16_t *x, int16_t *y)
{
    if (!digitalRead(GT911_PIN_INT)) {
        int16_t raw_x;
        int16_t raw_y;
        if (touch.getPoint(&raw_x, &raw_y)) {
            // rotate 90° for landscape
            *x = raw_y;
            *y = EPD_WIDTH - 1 - raw_x;
            LOG_DEBUG("touched(%d/%d)", *x, *y);
            return true;
        }
    }
    return false;
}

void earlyInitVariant()
{
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(BOARD_BL_EN, OUTPUT);
}

// T5-S3-ePaper Pro specific (late-) init
void lateInitVariant(void)
{
    touch.setPins(GT911_PIN_RST, GT911_PIN_INT);
    if (touch.begin(Wire, GT911_SLAVE_ADDRESS_L, GT911_PIN_SDA, GT911_PIN_SCL)) {
        touchScreenImpl1 = new TouchScreenImpl1(EPD_WIDTH, EPD_HEIGHT, readTouch);
        touchScreenImpl1->init();
#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
        touchBridge.observe(touchScreenImpl1);
#endif
    } else {
        LOG_ERROR("Failed to find touch controller!");
    }
}
#endif