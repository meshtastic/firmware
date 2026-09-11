#include "configuration.h"

#ifdef HAS_CST226SE

#include "input/TouchScreenImpl1.h"
#include "touch/TouchDrvCST226.h"
#include <Wire.h>

#ifndef TOUCH_RST
#define TOUCH_RST -1
#endif
#ifndef SCREEN_TOUCH_INT
#define SCREEN_TOUCH_INT -1
#endif

// The panel reports raw coordinates in its portrait frame. Rotated boards run the UI landscape,
// so mirror the swap TFTDisplay does for them (setGeometry(TFT_HEIGHT, TFT_WIDTH)).
#ifdef SCREEN_ROTATE
static constexpr int16_t screenWidth = TFT_HEIGHT;
static constexpr int16_t screenHeight = TFT_WIDTH;
#else
static constexpr int16_t screenWidth = TFT_WIDTH;
static constexpr int16_t screenHeight = TFT_HEIGHT;
#endif

// Concrete CST226 driver, not the TouchDrvCSTXXX wrapper: the wrapper walks CST816 and CST92xx
// too, and its two 1s retries cost ~2.4s of boot probing chips this panel never is.
TouchDrvCST226 tsPanel;
static constexpr uint8_t PossibleAddresses[2] = {CST328_ADDR, CST226SE_ADDR_ALT};
uint8_t i2cAddress = 0;

bool readTouch(int16_t *x, int16_t *y)
{
    int16_t x_array[1], y_array[1];
    uint8_t touched = tsPanel.getPoint(x_array, y_array, 1);
    if (touched > 0) {
        *y = x_array[0];
        *x = (screenWidth - y_array[0]);
        // Check bounds
        if (*x < 0 || *x >= screenWidth || *y < 0 || *y >= screenHeight) {
            return false;
        }
        return true; // Valid touch detected
    }
    return false; // No valid touch data
}

void lateInitVariant()
{
    tsPanel.setPins(TOUCH_RST, SCREEN_TOUCH_INT);
    for (uint8_t addr : PossibleAddresses) {
        // -1 pins: Wire is already begun by the I2C scan, re-initializing it only logs warnings
        if (tsPanel.begin(Wire, addr, -1, -1)) {
            i2cAddress = addr;
            LOG_DEBUG("CST226SE init OK at address 0x%02X", addr);
            touchScreenImpl1 = new TouchScreenImpl1(screenWidth, screenHeight, readTouch);
            touchScreenImpl1->init();
            return;
        }
    }
    LOG_ERROR("CST226SE init failed at all known addresses");
}
#endif
