#include "configuration.h"
#include "input/TouchScreenImpl1.h"
#include <Wire.h>
#include <touch/TouchDrvCST3530.hpp>

namespace
{
constexpr uint8_t touchAddress = 0x58;
TouchDrvCST3530 touchPanel;

bool readTouch(int16_t *x, int16_t *y)
{
    const auto &points = touchPanel.getTouchPoints();
    if (!points.hasPoints())
        return false;

    const auto &point = points[0];
    *x = point.x;
    *y = point.y;
    return point.x < EINK_WIDTH && point.y < EINK_HEIGHT;
}
} // namespace

void lateInitVariant()
{
    touchPanel.setPins(PIN_TOUCH_RESET, SCREEN_TOUCH_INT);
    if (!touchPanel.begin(Wire, touchAddress, I2C_SDA, I2C_SCL)) {
        LOG_WARN("W20 CST3530 touch initialization failed");
        return;
    }

    touchScreenImpl1 = new TouchScreenImpl1(EINK_WIDTH, EINK_HEIGHT, readTouch);
    touchScreenImpl1->init();
}
