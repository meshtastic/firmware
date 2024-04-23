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
    touchscreenI2CAddr,
    touchscreenBusFrequency,
    touchscreenRotate,
    touchscreenspidev,
    displayspidev,
    displayBusFrequency,
    displayPanel,
    displayWidth,
    displayHeight,
    displayCS,
    displayDC,
    displayRGBOrder,
    displayBacklight,
    displayBacklightPWMChannel,
    displayBacklightInvert,
    displayReset,
    displayRotate,
    displayOffsetRotate,
    displayOffsetX,
    displayOffsetY,
    displayInvert,
    keyboardDevice,
    logoutputlevel,
    webserver,
    webserverport,
    webserverrootpath,
    maxnodes
};
enum { no_screen, x11, st7789, st7735, st7735s, st7796, ili9341, ili9488, hx8357d };
enum { no_touchscreen, xpt2046, stmpe610, gt911, ft5x06 };
enum { level_error, level_warn, level_info, level_debug };

extern std::map<configNames, int> settingsMap;
extern std::map<configNames, std::string> settingsStrings;
int initGPIOPin(int pinNum, std::string gpioChipname);
extern HardwareSPI *DisplaySPI;
extern HardwareSPI *LoraSPI;