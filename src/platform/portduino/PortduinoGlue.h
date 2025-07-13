#pragma once
#include <fstream>
#include <map>
#include <unordered_map>

#include "platform/portduino/USBHal.h"

// Product strings for auto-configuration
// {"PRODUCT_STRING", "CONFIG.YAML"}
// YAML paths are relative to `meshtastic/available.d`
inline const std::unordered_map<std::string, std::string> configProducts = {{"MESHTOAD", "lora-usb-meshtoad-e22.yaml"},
                                                                            {"MESHSTICK", "lora-meshstick-1262.yaml"},
                                                                            {"MESHADV-PI", "lora-MeshAdv-900M30S.yaml"},
                                                                            {"MeshAdv Mini", "lora-MeshAdv-Mini-900M22S.yaml"},
                                                                            {"POWERPI", "lora-MeshAdv-900M30S.yaml"}};

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
    sx126x_max_power,
    sx128x_max_power,
    lr1110_max_power,
    lr1120_max_power,
    rf95_max_power,
    dio2_as_rf_switch,
    dio3_tcxo_voltage,
    use_simradio,
    use_autoconf,
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
    userButtonPin,
    tbUpPin,
    tbDownPin,
    tbLeftPin,
    tbRightPin,
    tbPressPin,
    tbDirection,
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
    websslkeypath,
    websslcertpath,
    maxtophone,
    maxnodes,
    ascii_logs,
    config_directory,
    available_directory,
    mac_address,
    hostMetrics_interval,
    hostMetrics_channel,
    hostMetrics_user_command,
    configDisplayMode,
    has_configDisplayMode
};
enum { no_screen, x11, fb, st7789, st7735, st7735s, st7796, ili9341, ili9342, ili9486, ili9488, hx8357d };
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
std::string exec(const char *cmd);