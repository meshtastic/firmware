#pragma once
#include <fstream>
#include <map>

#include "platform/portduino/USBHal.h"

enum configNames {
    default_gpiochip,
    cs_pin,
    cs_line,
    cs_gpiochip,
    irq_pin,
    irq_line,
    irq_gpiochip,
    busy_pin,
    busy_line,
    busy_gpiochip,
    reset_pin,
    reset_line,
    reset_gpiochip,
    txen_pin,
    txen_line,
    txen_gpiochip,
    rxen_pin,
    rxen_line,
    rxen_gpiochip,
    sx126x_ant_sw_pin,
    sx126x_ant_sw_line,
    sx126x_ant_sw_gpiochip,
    dio2_as_rf_switch,
    dio3_tcxo_voltage,
    use_rf95,
    use_sx1262,
    use_sx1268,
    use_sx1280,
    use_lr1110,
    use_lr1120,
    use_lr1121,
    use_llcc68,
    lora_usb_serial_num,
    lora_usb_pid,
    lora_usb_vid,
    user,
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
    pointerDevice,
    logoutputlevel,
    traceFilename,
    webserver,
    webserverport,
    webserverrootpath,
    maxtophone,
    maxnodes,
    ascii_logs,
    config_directory,
    mac_address
};
enum { no_screen, x11, st7789, st7735, st7735s, st7796, ili9341, ili9342, ili9486, ili9488, hx8357d };
enum { no_touchscreen, xpt2046, stmpe610, gt911, ft5x06 };
enum { level_error, level_warn, level_info, level_debug, level_trace };

extern std::map<configNames, int> settingsMap;
extern std::map<configNames, std::string> settingsStrings;
extern std::ofstream traceFile;
extern Ch341Hal *ch341Hal;
int initGPIOPin(int pinNum, std::string gpioChipname, int line);
bool loadConfig(const char *configPath);
static bool ends_with(std::string_view str, std::string_view suffix);
void getMacAddr(uint8_t *dmac);
bool MAC_from_string(std::string mac_str, uint8_t *dmac);
