#pragma once
#include <map>

enum configNames {
    use_sx1262,
    cs,
    irq,
    busy,
    reset,
    txen,
    rxen,
    dio2_as_rf_switch,
    dio3_tcxo_voltage,
    use_rf95,
    use_sx1280,
    user,
    gpiochip,
    spidev,
    i2cdev,
    has_gps,
    touchscreenModule,
    touchscreenCS,
    touchscreenIRQ,
    displayPanel,
    displayWidth,
    displayHeight,
    displayCS,
    displayDC,
    displayBacklight,
    displayReset,
    displayRotate,
    displayOffsetX,
    displayOffsetY,
    displayInvert,
    keyboardDevice,
    logoutputlevel
};
enum { no_screen, st7789, st7735, st7735s, ili9341 };
enum { no_touchscreen, xpt2046, stmpe610 };
enum { level_error, level_warn, level_info, level_debug };

extern std::map<configNames, int> settingsMap;
extern std::map<configNames, std::string> settingsStrings;
int initGPIOPin(int pinNum, std::string gpioChipname);