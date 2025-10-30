#include "configuration.h"

#ifdef T5_S3_EPAPER_PRO

#include "input/TouchScreenImpl1.h"
#include <TAMC_GT911.h>
#include <Wire.h>

TAMC_GT911 tp = TAMC_GT911(GT911_PIN_SDA, GT911_PIN_SCL, GT911_PIN_INT, GT911_PIN_RST, EPD_WIDTH, EPD_HEIGHT);

bool readTouch(int16_t *x, int16_t *y)
{
    if (!digitalRead(GT911_PIN_INT)) {
        tp.read();
        if (tp.isTouched) {
            *x = tp.points[0].x;
            *y = tp.points[0].y;
            LOG_DEBUG("touched(%d/%d)", *x, *y);
            return true;
        }
    }
    return false;
}

// T5-S3-ePaper Pro specific (late-) init
void lateInitVariant()
{
    tp.setRotation(ROTATION_INVERTED); // portrait
    tp.begin();
    touchScreenImpl1 = new TouchScreenImpl1(EPD_WIDTH, EPD_HEIGHT, readTouch);
    touchScreenImpl1->init();
}
#endif