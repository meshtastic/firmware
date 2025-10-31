#include "configuration.h"

#ifdef T5_S3_EPAPER_PRO

#include "SPILock.h"
#include "TouchDrvGT911.hpp"
#include "Wire.h"
#include "input/TouchScreenImpl1.h"

TouchDrvGT911 touch;

bool readTouch(int16_t *x, int16_t *y)
{
    if (!digitalRead(GT911_PIN_INT)) {
        concurrency::LockGuard g(spiLock);
        if (touch.getPoint(x, y) && (*x >= 0) && (*y >= 0) && (*x < EPD_WIDTH) && (*y < EPD_HEIGHT)) {
            LOG_DEBUG("touched(%d/%d)", *x, *y);
            return true;
        }
    }
    return false;
}

// T5-S3-ePaper Pro specific (late-) init
void lateInitVariant_T5S3Pro(void)
{
    concurrency::LockGuard g(spiLock);
    touch.setPins(GT911_PIN_RST, GT911_PIN_INT);
    if (touch.begin(Wire, GT911_SLAVE_ADDRESS_L, GT911_PIN_SDA, GT911_PIN_SCL)) {
        touchScreenImpl1 = new TouchScreenImpl1(EPD_WIDTH, EPD_HEIGHT, readTouch);
        touchScreenImpl1->init();
    } else {
        LOG_ERROR("Failed to find touch controller!");
    }
}
#endif