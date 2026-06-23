#include "configuration.h"

#ifdef T_WATCH_ULTRA

// Board-specific init lives here (rather than in variants/esp32s3/t-watch-ultra/variant.cpp)
// so that PlatformIO's library dependency finder can resolve headers such as
// input/TouchScreenImpl1.h (which transitively pulls in the ArduinoThread "Thread.h"),
// ExtensionIOXL9555.hpp and TouchDrvCSTXXX.hpp. Files compiled from outside src/ only get
// include paths for libraries they reference directly, so the transitive Thread.h include
// is not found there. See src/platform/extra_variants/README.md.

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
    pinMode(NFC_CS, OUTPUT);
    digitalWrite(NFC_CS, HIGH);
    pinMode(I2C_SDA, INPUT_PULLUP);
    pinMode(I2C_SCL, INPUT_PULLUP);

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
    }
    // NOTE: deliberately no LOG_* on the io.begin() failure path. earlyInitVariant() runs
    // before consoleInit(), where calling a LOG_* macro crashes the device (see
    // extra_variants/README.md). On failure the EXPANDS_* pins stay on their defaults.
}

static bool readTouch(int16_t *x, int16_t *y)
{
    return touchDrv.getPoint(x, y, 1);
}

void lateInitVariant()
{
    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
        pinMode(SCREEN_TOUCH_INT, INPUT_PULLUP);
        touchDrv.setPins(-1, SCREEN_TOUCH_INT);
        if (touchDrv.begin(Wire, TOUCH_SLAVE_ADDRESS, -1, -1)) {
            touchScreenImpl1 = new TouchScreenImpl1(TFT_WIDTH, TFT_HEIGHT, readTouch);
            touchScreenImpl1->init();
        } else {
            LOG_ERROR("failed to initialize CST92xx");
        }
    }
}
#endif
