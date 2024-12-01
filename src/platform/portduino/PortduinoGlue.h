#pragma once
#include <fstream>
#include <map>

enum configNames {
    use_sx1262,
    cs,
    irq,
    busy,
    reset,
    sx126x_ant_sw,
    txen,
    rxen,
    dio2_as_rf_switch,
    dio3_tcxo_voltage,
    ch341Quirk,
    use_rf95,
    use_sx1280,
    use_sx1268,
    user,
    gpiochip,
    spidev,
    spiSpeed,
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
    traceFilename,
    webserver,
    webserverport,
    webserverrootpath,
    maxtophone,
    maxnodes,
    ascii_logs,
    config_directory
};
enum { no_screen, x11, st7789, st7735, st7735s, st7796, ili9341, ili9342, ili9488, hx8357d };
enum { no_touchscreen, xpt2046, stmpe610, gt911, ft5x06 };
enum { level_error, level_warn, level_info, level_debug, level_trace };

extern std::map<configNames, int> settingsMap;
extern std::map<configNames, std::string> settingsStrings;
extern std::ofstream traceFile;
int initGPIOPin(int pinNum, std::string gpioChipname);
bool loadConfig(const char *configPath);
static bool ends_with(std::string_view str, std::string_view suffix);