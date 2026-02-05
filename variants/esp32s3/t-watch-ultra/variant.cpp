#include "variant.h"
#include "Arduino.h"
#include "TouchDrvCSTXXX.hpp"
#include "input/TouchScreenImpl1.h"
#include <ExtensionIOXL9555.hpp>
#include <Wire.h>

static ExtensionIOXL9555 io;
static TouchDrvCST92xx touchDrv;

void earlyInitVariant()
{
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(DISP_CS, OUTPUT);
    digitalWrite(DISP_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);

    if (io.begin(Wire, XL9555_SLAVE_ADDRESS0)) {
        io.pinMode(EXPANDS_DRV_EN, OUTPUT);
        io.digitalWrite(EXPANDS_DRV_EN, HIGH);
        delay(1);
        io.pinMode(EXPANDS_DISP_EN, OUTPUT);
        io.digitalWrite(EXPANDS_DISP_EN, HIGH);
        delay(1);
        io.pinMode(EXPANDS_TOUCH_RST, OUTPUT);
        io.digitalWrite(EXPANDS_TOUCH_RST, LOW);
        delay(20);
        io.digitalWrite(EXPANDS_TOUCH_RST, HIGH);
        delay(60);
        io.pinMode(EXPANDS_LORA_RF_SW, OUTPUT);
        io.digitalWrite(EXPANDS_LORA_RF_SW, HIGH); // set RF switch to built-in LoRa antenna
        // io.pinMode(EXPANDS_SD_DET, INPUT);
    } else {
        LOG_ERROR("io expander initialisation failed!");
    }
}

static bool readTouch(int16_t *x, int16_t *y)
{
    return touchDrv.isPressed() && touchDrv.getPoint(x, y, 1);
}

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
