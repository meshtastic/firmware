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

enum configNames {
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
    lora_usb_pid,
    lora_usb_vid,
    userButtonPin,
    tbUpPin,
    tbDownPin,
    tbLeftPin,
    tbRightPin,
    tbPressPin,
    tbDirection,
    spiSpeed,
    has_gps,
    touchscreenModule,
    touchscreenCS,
    touchscreenIRQ,
    touchscreenI2CAddr,
    touchscreenBusFrequency,
    touchscreenRotate,
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
    webserverport,
    maxtophone,
    maxnodes,
    hostMetrics_interval,
    hostMetrics_channel,
    configDisplayMode,
    has_configDisplayMode
};
enum { no_screen, x11, fb, st7789, st7735, st7735s, st7796, ili9341, ili9342, ili9486, ili9488, hx8357d };
enum { no_touchscreen, xpt2046, stmpe610, gt911, ft5x06 };
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
/*
enum gpio_pins {
    cs_pin,
    irq_pin,
    busy_pin,
    reset_pin,
    sx126x_ant_sw_pin,
    txen_pin,
    rxen_pin,
    displayDC,
    displayCS,
    displayBacklight,
    displayBacklightPWMChannel,
    displayReset,
    touchscreenCS,
    touchscreenIRQ,
    userButtonPin,
    tbUpPin,
    tbDownPin,
    tbLeftPin,
    tbRightPin,
    tbPressPin
};
*/

extern std::map<configNames, int> settingsMap;
extern std::ofstream traceFile;
extern Ch341Hal *ch341Hal;
int initGPIOPin(int pinNum, std::string gpioChipname, int line);
bool loadConfig(const char *configPath);
static bool ends_with(std::string_view str, std::string_view suffix);
void getMacAddr(uint8_t *dmac);
bool MAC_from_string(std::string mac_str, uint8_t *dmac);
std::string exec(const char *cmd);

struct pinMapping {
    int pin;
    int gpiochip;
    int line;
    bool enabled = false;
};

extern struct portduino_config_struct {
    // Lora
    std::map<lora_module_enum, std::string> loraModules = {
        {use_simradio, "sim"},  {use_autoconf, "auto"}, {use_rf95, "RF95"},     {use_sx1262, "sx1262"}, {use_sx1268, "sx1268"},
        {use_sx1280, "sx1280"}, {use_lr1110, "lr1110"}, {use_lr1120, "lr1120"}, {use_lr1121, "lr1121"}, {use_llcc68, "LLCC68"}};

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

    // I2C
    std::string i2cdev = "";

    // Display
    std::string display_spi_dev = "";
    int display_spi_dev_int = 0;

    // Touchscreen
    std::string touchscreen_spi_dev = "";
    int touchscreen_spi_dev_int = 0;

    // Input
    std::string keyboardDevice = "";
    std::string pointerDevice = "";

    // Logging
    portduino_log_level logoutputlevel = level_debug;
    std::string traceFilename;
    bool ascii_logs = !isatty(1);
    bool ascii_logs_explicit = false;

    // Webserver
    std::string webserver_root_path = "";
    std::string webserver_ssl_key_path = "/etc/meshtasticd/ssl/private_key.pem";
    std::string webserver_ssl_cert_path = "/etc/meshtasticd/ssl/certificate.pem";

    // HostMetrics
    std::string hostMetrics_user_command = "";

    // General
    std::string mac_address = "";
    bool mac_address_explicit = false;
    std::string mac_address_source = "";
    std::string config_directory = "";
    std::string available_directory = "/etc/meshtasticd/available.d/";

    std::string emit_yaml()
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Lora" << YAML::Value << YAML::BeginMap;

        out << YAML::Key << "Module" << YAML::Value << loraModules[lora_module];

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
        if (lora_spi_dev != "")
            out << YAML::Key << "spidev" << YAML::Value << lora_spi_dev;
        if (lora_usb_serial_num != "")
            out << YAML::Key << "USB_Serialnum" << YAML::Value << lora_usb_serial_num;
        out << YAML::EndMap; // Lora

        if (i2cdev != "") {
            out << YAML::Key << "I2C" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "I2CDevice" << YAML::Value << i2cdev;
            out << YAML::EndMap; // I2C
        }

        // Display
        if (display_spi_dev != "") {
            out << YAML::Key << "Display" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "spidev" << YAML::Value << display_spi_dev;
            out << YAML::EndMap; // Display
        }

        // Touchscreen
        if (touchscreen_spi_dev != "") {
            out << YAML::Key << "Touchscreen" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "spidev" << YAML::Value << touchscreen_spi_dev;
            out << YAML::EndMap; // Touchscreen
        }

        // Input
        if (keyboardDevice != "" || pointerDevice != "") {
            out << YAML::Key << "Input" << YAML::Value << YAML::BeginMap;
            if (keyboardDevice != "")
                out << YAML::Key << "KeyboardDevice" << YAML::Value << keyboardDevice;
            if (pointerDevice != "")
                out << YAML::Key << "PointerDevice" << YAML::Value << pointerDevice;
            out << YAML::EndMap; // Input
        }

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
            out << YAML::EndMap; // Webserver
        }

        // HostMetrics
        if (hostMetrics_user_command != "") {
            out << YAML::Key << "HostMetrics" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "UserStringCommand" << YAML::Value << hostMetrics_user_command;

            out << YAML::EndMap; // HostMetrics
        }

        out << YAML::Key << "General" << YAML::Value << YAML::BeginMap;
        if (config_directory != "")
            out << YAML::Key << "ConfigDirectory" << YAML::Value << config_directory;
        if (mac_address_explicit)
            out << YAML::Key << "MACAddress" << YAML::Value << mac_address;
        if (mac_address_source != "")
            out << YAML::Key << "MACAddressSource" << YAML::Value << mac_address_source;
        if (available_directory != "")
            out << YAML::Key << "AvailableDirectory" << YAML::Value << available_directory;
        out << YAML::EndMap; // General
        return out.c_str();
    }
} portduino_config;