#pragma once
#ifdef ARCH_RASPBERRY_PI
#include <map>

enum configNames {
    use_sx1262,
    cs,
    irq,
    busy,
    reset,
    dio2_as_rf_switch,
    use_rf95,
    user,
    gpiochip,
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
    keyboardDevice
};
enum { no_screen, st7789 };
enum { no_touchscreen, xpt2046 };

extern std::map<configNames, int> settingsMap;
extern std::map<configNames, std::string> settingsStrings;
int initGPIOPin(int pinNum, std::string gpioChipname);

#endif