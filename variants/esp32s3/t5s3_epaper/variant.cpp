#include "configuration.h"

#ifdef T5_S3_EPAPER_PRO

#include "Observer.h"
#include "TouchDrvGT911.hpp"
#include "Wire.h"
#include "input/InputBroker.h"
#include "input/TouchScreenImpl1.h"
#include "sleep.h"

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

        // Keep alignment in sync with the current rotation so that visual-frame gestures
        // always pass through nav functions without remapping: (rotation + alignment) % 4 == 0.
        inkhud->persistence->settings.joystick.alignment = (4 - inkhud->persistence->settings.rotation) % 4;

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

// Commands the GT911 into standby before the Wire bus is torn down.
// notifyDeepSleep fires before Wire.end() in doDeepSleep(), so I2C is still available here.
struct TouchDeepSleepObserver {
    int onDeepSleep(void *)
    {
        touch.sleep();
        return 0;
    }
    CallbackObserver<TouchDeepSleepObserver, void *> observer{this, &TouchDeepSleepObserver::onDeepSleep};
} static touchDeepSleepObserver;

bool readTouch(int16_t *x, int16_t *y)
{
    if (!digitalRead(GT911_PIN_INT)) {
        int16_t raw_x;
        int16_t raw_y;
        if (touch.getPoint(&raw_x, &raw_y)) {
#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
            // Transform raw GT911 axes to visual-frame coordinates for the current display rotation.
            // rotation=3 is the physical identity (device's default orientation).
            switch (NicheGraphics::InkHUD::InkHUD::getInstance()->persistence->settings.rotation) {
            default:
            case 3:
                *x = raw_x;
                *y = raw_y;
                break; // identity
            case 2:
                *x = (EPD_WIDTH - 1) - raw_y;
                *y = raw_x;
                break; // 90° CW tilt
            case 1:
                *x = (EPD_HEIGHT - 1) - raw_x;
                *y = (EPD_WIDTH - 1) - raw_y;
                break; // 180° flip
            case 0:
                *x = raw_y;
                *y = (EPD_HEIGHT - 1) - raw_x;
                break; // 90° CCW tilt
            }
#else
            *x = raw_x;
            *y = raw_y;
#endif
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

    // Program GT911 touch controller to I2C address 0x14 (GT911_SLAVE_ADDRESS_H) before
    // the I2C bus scan runs.  GPIO3 (INT) defaults LOW on ESP32-S3 cold boot, which would
    // leave the GT911 at 0x5D (GT911_SLAVE_ADDRESS_L) — the same address as the SFA30
    // air quality sensor — causing a false-positive SFA30 detection during the I2C scan.
    //
    // GT911 datasheet §4.3 "Address Selection":
    //   Pull INT HIGH before releasing RST → device latches address 0x14 (SLAVE_ADDRESS_H)
    //   Pull INT LOW  before releasing RST → device latches address 0x5D (SLAVE_ADDRESS_L)
    //   Minimum RST assert time: 100 µs; minimum startup time after RST deassert: 5 ms.
    //
    // lateInitVariant() calls touch.begin() which repeats this sequence internally while
    // also performing full I2C initialisation; the double-reset is harmless.
    pinMode(GT911_PIN_RST, OUTPUT);
    digitalWrite(GT911_PIN_RST, LOW);
    pinMode(GT911_PIN_INT, OUTPUT);
    digitalWrite(GT911_PIN_INT, HIGH); // HIGH → latch address 0x14
    delay(1);                          // > 100 µs
    digitalWrite(GT911_PIN_RST, HIGH);
    delay(10);                     // > 5 ms startup
    pinMode(GT911_PIN_INT, INPUT); // release INT for interrupt use
}

void variant_shutdown()
{
    // Ensure frontlight is off during deep sleep
    digitalWrite(BOARD_BL_EN, LOW);
}

void lateInitVariant()
{
    touch.setPins(GT911_PIN_RST, GT911_PIN_INT);
    if (touch.begin(Wire, GT911_SLAVE_ADDRESS_H, GT911_PIN_SDA, GT911_PIN_SCL)) {
        touchDeepSleepObserver.observer.observe(&notifyDeepSleep);
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
