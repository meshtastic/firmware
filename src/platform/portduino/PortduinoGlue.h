#pragma once
#include <fstream>
#include <map>
#include <unordered_map>

#include "LR11x0Interface.h"
#include "Module.h"
#include "platform/portduino/USBHal.h"
#include "yaml-cpp/yaml.h"

// Product strings for auto-configuration
// {"PRODUCT_STRING", "CONFIG.YAML"}
// YAML paths are relative to `meshtastic/available.d`
inline const std::unordered_map<std::string, std::string> configProducts = {
    {"MESHTOAD", "lora-usb-meshtoad-e22.yaml"},
    {"MESHSTICK", "lora-meshstick-1262.yaml"},
    {"MESHADV-PI", "lora-MeshAdv-900M30S.yaml"},
    {"MeshAdv Mini", "lora-MeshAdv-Mini-900M22S.yaml"},
    {"POWERPI", "lora-MeshAdv-900M30S.yaml"},
    {"RAK6421-13300-S1", "lora-RAK6421-13300-slot1.yaml"},
    {"RAK6421-13300-S2", "lora-RAK6421-13300-slot2.yaml"}};

enum screen_modules { no_screen, x11, fb, st7789, st7735, st7735s, st7796, ili9341, ili9342, ili9486, ili9488, hx8357d };
enum touchscreen_modules { no_touchscreen, xpt2046, stmpe610, gt911, ft5x06 };
enum portduino_log_level { level_error, level_warn, level_info, level_debug, level_trace };
enum lora_module_enum {
    use_simradio,
    use_autoconf,
    use_rf95,
    use_sx1262,
    use_sx1268,
    use_sx1280,
    use_lr1110,
    use_lr1120,
    use_lr1121,
    use_llcc68
};

struct pinMapping {
    std::string config_section;
    std::string config_name;
    int pin = RADIOLIB_NC;
    int gpiochip;
    int line;
    bool enabled = false;
};

extern std::ofstream traceFile;
extern Ch341Hal *ch341Hal;
int initGPIOPin(int pinNum, std::string gpioChipname, int line);
bool loadConfig(const char *configPath);
static bool ends_with(std::string_view str, std::string_view suffix);
void getMacAddr(uint8_t *dmac);
bool MAC_from_string(std::string mac_str, uint8_t *dmac);
void readGPIOFromYaml(YAML::Node sourceNode, pinMapping &destPin, int pinDefault = RADIOLIB_NC);
std::string exec(const char *cmd);

extern struct portduino_config_struct {
    // Lora
    std::map<lora_module_enum, std::string> loraModules = {
        {use_simradio, "sim"},  {use_autoconf, "auto"}, {use_rf95, "RF95"},     {use_sx1262, "sx1262"}, {use_sx1268, "sx1268"},
        {use_sx1280, "sx1280"}, {use_lr1110, "lr1110"}, {use_lr1120, "lr1120"}, {use_lr1121, "lr1121"}, {use_llcc68, "LLCC68"}};

    std::map<screen_modules, std::string> screen_names = {{x11, "X11"},         {fb, "FB"},           {st7789, "ST7789"},
                                                          {st7735, "ST7735"},   {st7735s, "ST7735S"}, {st7796, "ST7796"},
                                                          {ili9341, "ILI9341"}, {ili9342, "ILI9342"}, {ili9486, "ILI9486"},
                                                          {ili9488, "ILI9488"}, {hx8357d, "HX8357D"}};

    lora_module_enum lora_module;
    bool has_rfswitch_table = false;
    uint32_t rfswitch_dio_pins[5] = {RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};
    Module::RfSwitchMode_t rfswitch_table[8];
    bool force_simradio = false;
    bool has_device_id = false;
    uint8_t device_id[16] = {0};
    std::string lora_spi_dev = "";
    std::string lora_usb_serial_num = "";
    int lora_spi_dev_int = 0;
    int lora_default_gpiochip = 0;
    int sx126x_max_power = 22;
    int sx128x_max_power = 13;
    int lr1110_max_power = 22;
    int lr1120_max_power = 13;
    int rf95_max_power = 20;
    bool dio2_as_rf_switch = false;
    int dio3_tcxo_voltage = 0;
    int lora_usb_pid = 0x5512;
    int lora_usb_vid = 0x1A86;
    int spiSpeed = 2000000;
    pinMapping lora_cs_pin = {"Lora", "CS"};
    pinMapping lora_irq_pin = {"Lora", "IRQ"};
    pinMapping lora_busy_pin = {"Lora", "Busy"};
    pinMapping lora_reset_pin = {"Lora", "Reset"};
    pinMapping lora_txen_pin = {"Lora", "TXen"};
    pinMapping lora_rxen_pin = {"Lora", "RXen"};
    pinMapping lora_sx126x_ant_sw_pin = {"Lora", "SX126X_ANT_SW"};

    // GPS
    bool has_gps = false;

    // I2C
    std::string i2cdev = "";

    // Display
    std::string display_spi_dev = "";
    int display_spi_dev_int = 0;
    int displayBusFrequency = 40000000;
    screen_modules displayPanel = no_screen;
    int displayWidth = 0;
    int displayHeight = 0;
    bool displayRGBOrder = false;
    bool displayBacklightInvert = false;
    bool displayRotate = false;
    int displayOffsetRotate = 1;
    bool displayInvert = false;
    int displayOffsetX = 0;
    int displayOffsetY = 0;
    pinMapping displayDC = {"Display", "DC"};
    pinMapping displayCS = {"Display", "CS"};
    pinMapping displayBacklight = {"Display", "Backlight"};
    pinMapping displayBacklightPWMChannel = {"Display", "BacklightPWMChannel"};
    pinMapping displayReset = {"Display", "Reset"};

    // Touchscreen
    std::string touchscreen_spi_dev = "";
    int touchscreen_spi_dev_int = 0;
    touchscreen_modules touchscreenModule = no_touchscreen;
    int touchscreenI2CAddr = -1;
    int touchscreenBusFrequency = 1000000;
    int touchscreenRotate = -1;
    pinMapping touchscreenCS = {"Touchscreen", "CS"};
    pinMapping touchscreenIRQ = {"Touchscreen", "IRQ"};

    // Input
    std::string keyboardDevice = "";
    std::string pointerDevice = "";
    int tbDirection;
    pinMapping userButtonPin = {"Input", "User"};
    pinMapping tbUpPin = {"Input", "TrackballUp"};
    pinMapping tbDownPin = {"Input", "TrackballDown"};
    pinMapping tbLeftPin = {"Input", "TrackballLwft"};
    pinMapping tbRightPin = {"Input", "TrackballRight"};
    pinMapping tbPressPin = {"Input", "TrackballPress"};

    // Logging
    portduino_log_level logoutputlevel = level_debug;
    std::string traceFilename;
    bool ascii_logs = !isatty(1);
    bool ascii_logs_explicit = false;

    // Webserver
    std::string webserver_root_path = "";
    std::string webserver_ssl_key_path = "/etc/meshtasticd/ssl/private_key.pem";
    std::string webserver_ssl_cert_path = "/etc/meshtasticd/ssl/certificate.pem";
    int webserverport = -1;

    // HostMetrics
    std::string hostMetrics_user_command = "";
    int hostMetrics_interval = 0;
    int hostMetrics_channel = 0;

    // config
    int configDisplayMode = 0;
    bool has_configDisplayMode = false;

    // General
    std::string mac_address = "";
    bool mac_address_explicit = false;
    std::string mac_address_source = "";
    std::string config_directory = "";
    std::string available_directory = "/etc/meshtasticd/available.d/";
    int maxtophone = 100;
    int MaxNodes = 200;

    pinMapping *all_pins[20] = {&lora_cs_pin,
                                &lora_irq_pin,
                                &lora_busy_pin,
                                &lora_reset_pin,
                                &lora_txen_pin,
                                &lora_rxen_pin,
                                &lora_sx126x_ant_sw_pin,
                                &displayDC,
                                &displayCS,
                                &displayBacklight,
                                &displayBacklightPWMChannel,
                                &displayReset,
                                &touchscreenCS,
                                &touchscreenIRQ,
                                &userButtonPin,
                                &tbUpPin,
                                &tbDownPin,
                                &tbLeftPin,
                                &tbRightPin,
                                &tbPressPin};

    std::string emit_yaml()
    {
        YAML::Emitter out;
        out << YAML::BeginMap;

        // Lora
        out << YAML::Key << "Lora" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Module" << YAML::Value << loraModules[lora_module];

        for (auto lora_pin : all_pins) {
            if (lora_pin->config_section == "Lora" && lora_pin->enabled) {
                out << YAML::Key << lora_pin->config_name << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "pin" << YAML::Value << lora_pin->pin;
                out << YAML::Key << "line" << YAML::Value << lora_pin->line;
                out << YAML::Key << "gpiochip" << YAML::Value << lora_pin->gpiochip;
                out << YAML::EndMap; // User
            }
        }

        if (sx126x_max_power != 22)
            out << YAML::Key << "SX126X_MAX_POWER" << YAML::Value << sx126x_max_power;
        if (sx128x_max_power != 13)
            out << YAML::Key << "SX128X_MAX_POWER" << YAML::Value << sx128x_max_power;
        if (lr1110_max_power != 22)
            out << YAML::Key << "LR1110_MAX_POWER" << YAML::Value << lr1110_max_power;
        if (lr1120_max_power != 13)
            out << YAML::Key << "LR1120_MAX_POWER" << YAML::Value << lr1120_max_power;
        if (rf95_max_power != 20)
            out << YAML::Key << "RF95_MAX_POWER" << YAML::Value << rf95_max_power;
        out << YAML::Key << "DIO2_AS_RF_SWITCH" << YAML::Value << dio2_as_rf_switch;
        if (dio3_tcxo_voltage != 0)
            out << YAML::Key << "DIO3_TCXO_VOLTAGE" << YAML::Value << dio3_tcxo_voltage;
        if (lora_usb_pid != 0x5512)
            out << YAML::Key << "USB_PID" << YAML::Value << YAML::Hex << lora_usb_pid;
        if (lora_usb_vid != 0x1A86)
            out << YAML::Key << "USB_VID" << YAML::Value << YAML::Hex << lora_usb_vid;
        if (lora_spi_dev != "")
            out << YAML::Key << "spidev" << YAML::Value << lora_spi_dev;
        if (lora_usb_serial_num != "")
            out << YAML::Key << "USB_Serialnum" << YAML::Value << lora_usb_serial_num;
        out << YAML::Key << "spiSpeed" << YAML::Value << spiSpeed;
        if (rfswitch_dio_pins[0] != RADIOLIB_NC) {
            out << YAML::Key << "rfswitch_table" << YAML::Value << YAML::BeginMap;

            out << YAML::Key << "pins";
            out << YAML::Value << YAML::Flow << YAML::BeginSeq;

            for (int i = 0; i < 5; i++) {
                // set up the pin array first
                if (rfswitch_dio_pins[i] == RADIOLIB_LR11X0_DIO5)
                    out << "DIO5";
                if (rfswitch_dio_pins[i] == RADIOLIB_LR11X0_DIO6)
                    out << "DIO6";
                if (rfswitch_dio_pins[i] == RADIOLIB_LR11X0_DIO7)
                    out << "DIO7";
                if (rfswitch_dio_pins[i] == RADIOLIB_LR11X0_DIO8)
                    out << "DIO8";
                if (rfswitch_dio_pins[i] == RADIOLIB_LR11X0_DIO10)
                    out << "DIO10";
            }
            out << YAML::EndSeq;

            for (int i = 0; i < 7; i++) {
                switch (i) {
                case 0:
                    out << YAML::Key << "MODE_STBY";
                    break;
                case 1:
                    out << YAML::Key << "MODE_RX";
                    break;
                case 2:
                    out << YAML::Key << "MODE_TX";
                    break;
                case 3:
                    out << YAML::Key << "MODE_TX_HP";
                    break;
                case 4:
                    out << YAML::Key << "MODE_TX_HF";
                    break;
                case 5:
                    out << YAML::Key << "MODE_GNSS";
                    break;
                case 6:
                    out << YAML::Key << "MODE_WIFI";
                    break;
                }

                out << YAML::Value << YAML::Flow << YAML::BeginSeq;
                for (int j = 0; j < 5; j++) {
                    if (rfswitch_table[i].values[j] == HIGH) {
                        out << "HIGH";
                    } else {
                        out << "LOW";
                    }
                }
                out << YAML::EndSeq;
            }
            out << YAML::EndMap; // rfswitch_table
        }
        out << YAML::EndMap; // Lora

        if (i2cdev != "") {
            out << YAML::Key << "I2C" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "I2CDevice" << YAML::Value << i2cdev;
            out << YAML::EndMap; // I2C
        }

        // Display
        if (displayPanel != no_screen) {
            out << YAML::Key << "Display" << YAML::Value << YAML::BeginMap;
            for (auto &screen_name : screen_names) {
                if (displayPanel == screen_name.first)
                    out << YAML::Key << "Module" << YAML::Value << screen_name.second;
            }
            for (auto display_pin : all_pins) {
                if (display_pin->config_section == "Display" && display_pin->enabled) {
                    out << YAML::Key << display_pin->config_name << YAML::Value << YAML::BeginMap;
                    out << YAML::Key << "pin" << YAML::Value << display_pin->pin;
                    out << YAML::Key << "line" << YAML::Value << display_pin->line;
                    out << YAML::Key << "gpiochip" << YAML::Value << display_pin->gpiochip;
                    out << YAML::EndMap;
                }
            }
            out << YAML::Key << "spidev" << YAML::Value << display_spi_dev;
            out << YAML::Key << "BusFrequency" << YAML::Value << displayBusFrequency;
            if (displayWidth)
                out << YAML::Key << "Width" << YAML::Value << displayWidth;
            if (displayHeight)
                out << YAML::Key << "Height" << YAML::Value << displayHeight;
            if (displayRGBOrder)
                out << YAML::Key << "RGBOrder" << YAML::Value << true;
            if (displayBacklightInvert)
                out << YAML::Key << "BacklightInvert" << YAML::Value << true;
            if (displayRotate)
                out << YAML::Key << "Rotate" << YAML::Value << true;
            if (displayInvert)
                out << YAML::Key << "Invert" << YAML::Value << true;
            if (displayOffsetX)
                out << YAML::Key << "OffsetX" << YAML::Value << displayOffsetX;
            if (displayOffsetY)
                out << YAML::Key << "OffsetY" << YAML::Value << displayOffsetY;

            out << YAML::Key << "OffsetRotate" << YAML::Value << displayOffsetRotate;

            out << YAML::EndMap; // Display
        }

        // Touchscreen
        if (touchscreen_spi_dev != "") {
            out << YAML::Key << "Touchscreen" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "spidev" << YAML::Value << touchscreen_spi_dev;
            out << YAML::Key << "BusFrequency" << YAML::Value << touchscreenBusFrequency;
            switch (touchscreenModule) {
            case xpt2046:
                out << YAML::Key << "Module" << YAML::Value << "XPT2046";
            case stmpe610:
                out << YAML::Key << "Module" << YAML::Value << "STMPE610";
            case gt911:
                out << YAML::Key << "Module" << YAML::Value << "GT911";
            case ft5x06:
                out << YAML::Key << "Module" << YAML::Value << "FT5x06";
            }
            for (auto touchscreen_pin : all_pins) {
                if (touchscreen_pin->config_section == "Touchscreen" && touchscreen_pin->enabled) {
                    out << YAML::Key << touchscreen_pin->config_name << YAML::Value << YAML::BeginMap;
                    out << YAML::Key << "pin" << YAML::Value << touchscreen_pin->pin;
                    out << YAML::Key << "line" << YAML::Value << touchscreen_pin->line;
                    out << YAML::Key << "gpiochip" << YAML::Value << touchscreen_pin->gpiochip;
                    out << YAML::EndMap;
                }
            }
            if (touchscreenRotate != -1)
                out << YAML::Key << "Rotate" << YAML::Value << touchscreenRotate;
            if (touchscreenI2CAddr != -1)
                out << YAML::Key << "I2CAddr" << YAML::Value << touchscreenI2CAddr;
            out << YAML::EndMap; // Touchscreen
        }

        // Input
        out << YAML::Key << "Input" << YAML::Value << YAML::BeginMap;
        if (keyboardDevice != "")
            out << YAML::Key << "KeyboardDevice" << YAML::Value << keyboardDevice;
        if (pointerDevice != "")
            out << YAML::Key << "PointerDevice" << YAML::Value << pointerDevice;

        for (auto input_pin : all_pins) {
            if (input_pin->config_section == "Input" && input_pin->enabled) {
                out << YAML::Key << input_pin->config_name << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "pin" << YAML::Value << input_pin->pin;
                out << YAML::Key << "line" << YAML::Value << input_pin->line;
                out << YAML::Key << "gpiochip" << YAML::Value << input_pin->gpiochip;
                out << YAML::EndMap;
            }
        }
        if (tbDirection == 3)
            out << YAML::Key << "TrackballDirection" << YAML::Value << "FALLING";

        out << YAML::EndMap; // Input

        out << YAML::Key << "Logging" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "LogLevel" << YAML::Value;
        switch (logoutputlevel) {
        case level_error:
            out << "error";
            break;
        case level_warn:
            out << "warn";
            break;
        case level_info:
            out << "info";
            break;
        case level_debug:
            out << "debug";
            break;
        case level_trace:
            out << "trace";
            break;
        }
        if (traceFilename != "")
            out << YAML::Key << "TraceFile" << YAML::Value << traceFilename;
        if (ascii_logs_explicit) {
            out << YAML::Key << "AsciiLogs" << YAML::Value << ascii_logs;
        }
        out << YAML::EndMap; // Logging

        // Webserver
        if (webserver_root_path != "") {
            out << YAML::Key << "Webserver" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "RootPath" << YAML::Value << webserver_root_path;
            out << YAML::Key << "SSLKey" << YAML::Value << webserver_ssl_key_path;
            out << YAML::Key << "SSLCert" << YAML::Value << webserver_ssl_cert_path;
            out << YAML::Key << "Port" << YAML::Value << webserverport;
            out << YAML::EndMap; // Webserver
        }

        // HostMetrics
        if (hostMetrics_user_command != "") {
            out << YAML::Key << "HostMetrics" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "UserStringCommand" << YAML::Value << hostMetrics_user_command;
            out << YAML::Key << "ReportInterval" << YAML::Value << hostMetrics_interval;
            out << YAML::Key << "Channel" << YAML::Value << hostMetrics_channel;

            out << YAML::EndMap; // HostMetrics
        }

        // config
        if (has_configDisplayMode) {
            out << YAML::Key << "Config" << YAML::Value << YAML::BeginMap;
            switch (configDisplayMode) {
            case meshtastic_Config_DisplayConfig_DisplayMode_TWOCOLOR:
                out << YAML::Key << "DisplayMode" << YAML::Value << "TWOCOLOR";
            case meshtastic_Config_DisplayConfig_DisplayMode_INVERTED:
                out << YAML::Key << "DisplayMode" << YAML::Value << "INVERTED";
            case meshtastic_Config_DisplayConfig_DisplayMode_COLOR:
                out << YAML::Key << "DisplayMode" << YAML::Value << "COLOR";
            case meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT:
                out << YAML::Key << "DisplayMode" << YAML::Value << "DEFAULT";
            }

            out << YAML::EndMap; // Config
        }

        // General
        out << YAML::Key << "General" << YAML::Value << YAML::BeginMap;
        if (config_directory != "")
            out << YAML::Key << "ConfigDirectory" << YAML::Value << config_directory;
        if (mac_address_explicit)
            out << YAML::Key << "MACAddress" << YAML::Value << mac_address;
        if (mac_address_source != "")
            out << YAML::Key << "MACAddressSource" << YAML::Value << mac_address_source;
        if (available_directory != "")
            out << YAML::Key << "AvailableDirectory" << YAML::Value << available_directory;
        out << YAML::Key << "MaxMessageQueue" << YAML::Value << maxtophone;
        out << YAML::Key << "MaxNodes" << YAML::Value << MaxNodes;
        out << YAML::EndMap; // General
        return out.c_str();
    }
} portduino_config;