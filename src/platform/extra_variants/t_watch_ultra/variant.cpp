#include "configuration.h"

#ifdef T_WATCH_ULTRA

#include "Arduino.h"
#include "TouchDrvCSTXXX.hpp"
#include "input/TouchScreenImpl1.h"
#include <Wire.h>

static TouchDrvCST92xx touchDrv;

static bool readTouch(int16_t *x, int16_t *y)
{
    return touchDrv.isPressed() && touchDrv.getPoint(x, y, 1);
}

// T-Watch Ultra specific init
void lateInitVariant()
{
    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
        pinMode(SCREEN_TOUCH_INT, INPUT_PULLUP);
        touchDrv.setPins(-1, SCREEN_TOUCH_INT);
        if (touchDrv.begin(Wire, TOUCH_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
            touchScreenImpl1 = new TouchScreenImpl1(TFT_WIDTH, TFT_HEIGHT, readTouch);
            touchScreenImpl1->init();
        } else {
            LOG_ERROR("failed to initialize CST92xx");
        }
    }
}
#endif