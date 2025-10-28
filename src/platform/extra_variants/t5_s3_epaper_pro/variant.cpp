#include "configuration.h"

#ifdef T5_S3_EPAPER_PRO

#include "input/TouchScreenImpl1.h"
#include <Wire.h>
#include <bb_captouch.h>

BBCapTouch bbct;

bool readTouch(int16_t *x, int16_t *y)
{
    TOUCHINFO ti;
    if (!digitalRead(GT911_PIN_INT)) {
        if (bbct.getSamples(&ti)) {
            *x = ti.x[0];
            *y = ti.y[0];
            return true;
        }
    }
    return false;
}

// T5-S3-ePaper Pro specific (late-) init
void lateInitVariant()
{
    bbct.init(GT911_PIN_SDA, GT911_PIN_SCL, GT911_PIN_RST, GT911_PIN_INT);
    bbct.setOrientation(180, EPD_WIDTH, EPD_HEIGHT);
    touchScreenImpl1 = new TouchScreenImpl1(EPD_WIDTH, EPD_HEIGHT, readTouch);
    touchScreenImpl1->init();
}
#endif