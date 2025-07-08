#include "configuration.h"

#ifdef HAS_CST226SE

#include "TouchDrvCSTXXX.hpp"
#include "input/TouchScreenImpl1.h"
#include <Wire.h>

TouchDrvCSTXXX tsPanel;
static constexpr uint8_t PossibleAddresses[2] = {CST226SE_ADDR, CST226SE_ADDR_ALT};
uint8_t i2cAddress = 0;

bool readTouch(int16_t *x, int16_t *y)
{
    int16_t x_array[1], y_array[1];
    uint8_t touched = tsPanel.getPoint(x_array, y_array, 1);
    if (touched > 0) {
        *y = x_array[0];
        *x = (TFT_WIDTH - y_array[0]);
        // Check bounds
        if (*x < 0 || *x >= TFT_WIDTH || *y < 0 || *y >= TFT_HEIGHT) {
            return false;
        }
        return true; // Valid touch detected
    }
    return false; // No valid touch data
}

void lateInitVariant()
{
    tsPanel.setTouchDrvModel(TouchDrv_CST226);
    for (uint8_t addr : PossibleAddresses) {
        if (tsPanel.begin(Wire, addr, I2C_SDA, I2C_SCL)) {
            i2cAddress = addr;
            LOG_DEBUG("CST226SE init OK at address 0x%02X", addr);
            touchScreenImpl1 = new TouchScreenImpl1(TFT_WIDTH, TFT_HEIGHT, readTouch);
            touchScreenImpl1->init();
            return;
        }
    }
    LOG_ERROR("CST226SE init failed at all known addresses");
}
#endif
