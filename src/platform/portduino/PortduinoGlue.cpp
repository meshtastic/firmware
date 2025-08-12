#include "CryptoEngine.h"
#include "PortduinoGPIO.h"
#include "SPIChip.h"
#include "mesh/RF95Interface.h"
#include "sleep.h"
#include "target_specific.h"

#include "PortduinoGlue.h"
#include "api/ServerAPI.h"
#include "linux/gpio/LinuxGPIOPin.h"
#include "meshUtils.h"
#include "yaml-cpp/yaml.h"
#include <Utility.h>
#include <assert.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sys/ioctl.h>
#include <unistd.h>

#ifdef PORTDUINO_LINUX_HARDWARE
#include <cxxabi.h>
#endif

#include "platform/portduino/USBHal.h"

std::map<configNames, int> settingsMap;
std::map<configNames, std::string> settingsStrings;
std::ofstream traceFile;
Ch341Hal *ch341Hal = nullptr;
char *configPath = nullptr;
char *optionMac = nullptr;
bool forceSimulated = false;
bool verboseEnabled = false;

const char *argp_program_version = optstr(APP_VERSION);

// FIXME - move setBluetoothEnable into a HALPlatform class
void setBluetoothEnable(bool enable)
{
    // not needed
}

void cpuDeepSleep(uint32_t msecs)
{
    notImplemented("cpuDeepSleep");
}

void updateBatteryLevel(uint8_t level) NOT_IMPLEMENTED("updateBatteryLevel");

int TCPPort = SERVER_API_DEFAULT_PORT;

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    switch (key) {
    case 'p':
        if (sscanf(arg, "%d", &TCPPort) < 1)
            return ARGP_ERR_UNKNOWN;
        else
            printf("Using config file %d\n", TCPPort);
        break;
    case 'c':
        configPath = arg;
        break;
    case 's':
        forceSimulated = true;
        break;
    case 'h':
        optionMac = arg;
        break;
    case 'v':
        verboseEnabled = true;
        break;
    case ARGP_KEY_ARG:
        return 0;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

void portduinoCustomInit()
{
    static struct argp_option options[] = {{"port", 'p', "PORT", 0, "The TCP port to use."},
                                           {"config", 'c', "CONFIG_PATH", 0, "Full path of the .yaml config file to use."},
                                           {"hwid", 'h', "HWID", 0, "The mac address to assign to this virtual machine"},
                                           {"sim", 's', 0, 0, "Run in Simulated radio mode"},
                                           {"verbose", 'v', 0, 0, "Set log level to full debug"},
                                           {0}};
    static void *childArguments;
    static char doc[] = "Meshtastic native build.";
    static char args_doc[] = "...";
    static struct argp argp = {options, parse_opt, args_doc, doc, 0, 0, 0};
    const struct argp_child child = {&argp, OPTION_ARG_OPTIONAL, 0, 0};
    portduinoAddArguments(child, childArguments);
}

void getMacAddr(uint8_t *dmac)
{
    // We should store this value, and short-circuit all this if it's already been set.
    if (optionMac != nullptr && strlen(optionMac) > 0) {
        if (strlen(optionMac) >= 12) {
            MAC_from_string(optionMac, dmac);
        } else {
            uint32_t hwId = {0};
            sscanf(optionMac, "%u", &hwId);
            dmac[0] = 0x80;
            dmac[1] = 0;
            dmac[2] = hwId >> 24;
            dmac[3] = hwId >> 16;
            dmac[4] = hwId >> 8;
            dmac[5] = hwId & 0xff;
        }
    } else if (settingsStrings[mac_address].length() > 11) {
        MAC_from_string(settingsStrings[mac_address], dmac);
        exit;
    } else {

        struct hci_dev_info di = {0};
        di.dev_id = 0;
        bdaddr_t bdaddr;
        int btsock;
        btsock = socket(AF_BLUETOOTH, SOCK_RAW, 1);
        if (btsock < 0) { // If anything fails, just return with the default value
            return;
        }

        if (ioctl(btsock, HCIGETDEVINFO, (void *)&di)) {
            return;
        }

        dmac[0] = di.bdaddr.b[5];
        dmac[1] = di.bdaddr.b[4];
        dmac[2] = di.bdaddr.b[3];
        dmac[3] = di.bdaddr.b[2];
        dmac[4] = di.bdaddr.b[1];
        dmac[5] = di.bdaddr.b[0];
    }
}

/** apps run under portduino can optionally define a portduinoSetup() to
 * use portduino specific init code (such as gpioBind) to setup portduino on their host machine,
 * before running 'arduino' code.
 */
void portduinoSetup()
{
    printf("Set up Meshtastic on Portduino...\n");
    int max_GPIO = 0;
    const configNames GPIO_lines[] = {cs_pin,
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
                                      tbPressPin};

    std::string gpioChipName = "gpiochip";
    settingsStrings[i2cdev] = "";
    settingsStrings[keyboardDevice] = "";
    settingsStrings[pointerDevice] = "";
    settingsStrings[webserverrootpath] = "";
    settingsStrings[spidev] = "";
    settingsStrings[displayspidev] = "";
    settingsMap[spiSpeed] = 2000000;
    settingsMap[ascii_logs] = !isatty(1);
    settingsMap[displayPanel] = no_screen;
    settingsMap[touchscreenModule] = no_touchscreen;
    settingsMap[tbUpPin] = RADIOLIB_NC;
    settingsMap[tbDownPin] = RADIOLIB_NC;
    settingsMap[tbLeftPin] = RADIOLIB_NC;
    settingsMap[tbRightPin] = RADIOLIB_NC;
    settingsMap[tbPressPin] = RADIOLIB_NC;

    YAML::Node yamlConfig;

    if (forceSimulated == true) {
        settingsMap[use_simradio] = true;
    } else if (configPath != nullptr) {
        if (loadConfig(configPath)) {
            std::cout << "Using " << configPath << " as config file" << std::endl;
        } else {
            std::cout << "Unable to use " << configPath << " as config file" << std::endl;
            exit(EXIT_FAILURE);
        }
    } else if (access("config.yaml", R_OK) == 0) {
        if (loadConfig("config.yaml")) {
            std::cout << "Using local config.yaml as config file" << std::endl;
        } else {
            std::cout << "Unable to use local config.yaml as config file" << std::endl;
            exit(EXIT_FAILURE);
        }
    } else if (access("/etc/meshtasticd/config.yaml", R_OK) == 0) {
        if (loadConfig("/etc/meshtasticd/config.yaml")) {
            std::cout << "Using /etc/meshtasticd/config.yaml as config file" << std::endl;
        } else {
            std::cout << "Unable to use /etc/meshtasticd/config.yaml as config file" << std::endl;
            exit(EXIT_FAILURE);
        }
    } else {
        std::cout << "No 'config.yaml' found..." << std::endl;
        settingsMap[use_simradio] = true;
    }

    if (settingsMap[use_simradio] == true) {
        std::cout << "Running in simulated mode." << std::endl;
        settingsMap[maxnodes] = 200;               // Default to 200 nodes
        settingsMap[logoutputlevel] = level_debug; // Default to debug
        // Set the random seed equal to TCPPort to have a different seed per instance
        randomSeed(TCPPort);
        return;
    }

    if (settingsStrings[config_directory] != "") {
        std::string filetype = ".yaml";
        for (const std::filesystem::directory_entry &entry :
             std::filesystem::directory_iterator{settingsStrings[config_directory]}) {
            if (ends_with(entry.path().string(), ".yaml")) {
                std::cout << "Also using " << entry << " as additional config file" << std::endl;
                loadConfig(entry.path().c_str());
            }
        }
    }

    // If LoRa `Module: auto` (default in config.yaml),
    // attempt to auto config based on Product Strings
    if (settingsMap[use_autoconf] == true) {
        char autoconf_product[96] = {0};
        // Try CH341
        try {
            std::cout << "autoconf: Looking for CH341 device..." << std::endl;
            ch341Hal =
                new Ch341Hal(0, settingsStrings[lora_usb_serial_num], settingsMap[lora_usb_vid], settingsMap[lora_usb_pid]);
            ch341Hal->getProductString(autoconf_product, 95);
            delete ch341Hal;
            std::cout << "autoconf: Found CH341 device " << autoconf_product << std::endl;
        } catch (...) {
            std::cout << "autoconf: Could not locate CH341 device" << std::endl;
        }
        // Try Pi HAT+
        std::cout << "autoconf: Looking for Pi HAT+..." << std::endl;
        if (access("/proc/device-tree/hat/product", R_OK) == 0) {
            std::ifstream hatProductFile("/proc/device-tree/hat/product");
            if (hatProductFile.is_open()) {
                hatProductFile.read(autoconf_product, 95);
                hatProductFile.close();
            }
            std::cout << "autoconf: Found Pi HAT+ " << autoconf_product << " at /proc/device-tree/hat/product" << std::endl;
        } else {
            std::cout << "autoconf: Could not locate Pi HAT+ at /proc/device-tree/hat/product" << std::endl;
        }
        // Load the config file based on the product string
        if (strlen(autoconf_product) > 0) {
            // From configProducts map in PortduinoGlue.h
            std::string product_config = "";
            try {
                product_config = configProducts.at(autoconf_product);
            } catch (std::out_of_range &e) {
                std::cerr << "autoconf: Unable to find config for " << autoconf_product << std::endl;
                exit(EXIT_FAILURE);
            }
            if (loadConfig((settingsStrings[available_directory] + product_config).c_str())) {
                std::cout << "autoconf: Using " << product_config << " as config file for " << autoconf_product << std::endl;
            } else {
                std::cerr << "autoconf: Unable to use " << product_config << " as config file for " << autoconf_product
                          << std::endl;
                exit(EXIT_FAILURE);
            }
        } else {
            std::cerr << "autoconf: Could not locate any devices" << std::endl;
        }
    }

    // if we're using a usermode driver, we need to initialize it here, to get a serial number back for mac address
    uint8_t dmac[6] = {0};
    if (settingsStrings[spidev] == "ch341") {
        try {
            ch341Hal =
                new Ch341Hal(0, settingsStrings[lora_usb_serial_num], settingsMap[lora_usb_vid], settingsMap[lora_usb_pid]);
        } catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
            std::cerr << "Could not initialize CH341 device!" << std::endl;
            exit(EXIT_FAILURE);
        }
        char serial[9] = {0};
        ch341Hal->getSerialString(serial, 8);
        std::cout << "CH341 Serial " << serial << std::endl;
        char product_string[96] = {0};
        ch341Hal->getProductString(product_string, 95);
        std::cout << "CH341 Product " << product_string << std::endl;
        if (strlen(serial) == 8 && settingsStrings[mac_address].length() < 12) {
            uint8_t hash[32] = {0};
            memcpy(hash, serial, 8);
            crypto->hash(hash, 8);
            dmac[0] = (hash[0] << 4) | 2;
            dmac[1] = hash[1];
            dmac[2] = hash[2];
            dmac[3] = hash[3];
            dmac[4] = hash[4];
            dmac[5] = hash[5];
            char macBuf[13] = {0};
            sprintf(macBuf, "%02X%02X%02X%02X%02X%02X", dmac[0], dmac[1], dmac[2], dmac[3], dmac[4], dmac[5]);
            settingsStrings[mac_address] = macBuf;
        }
    }

    getMacAddr(dmac);
    if (dmac[0] == 0 && dmac[1] == 0 && dmac[2] == 0 && dmac[3] == 0 && dmac[4] == 0 && dmac[5] == 0) {
        std::cout << "*** Blank MAC Address not allowed!" << std::endl;
        std::cout << "Please set a MAC Address in config.yaml using either MACAddress or MACAddressSource." << std::endl;
        exit(EXIT_FAILURE);
    }
    printf("MAC ADDRESS: %02X:%02X:%02X:%02X:%02X:%02X\n", dmac[0], dmac[1], dmac[2], dmac[3], dmac[4], dmac[5]);
    // Rather important to set this, if not running simulated.
    randomSeed(time(NULL));

    std::string defaultGpioChipName = gpioChipName + std::to_string(settingsMap[default_gpiochip]);

    for (configNames i : GPIO_lines) {
        if (settingsMap.count(i) && settingsMap[i] > max_GPIO)
            max_GPIO = settingsMap[i];
    }

    gpioInit(max_GPIO + 1); // Done here so we can inform Portduino how many GPIOs we need.

    // Need to bind all the configured GPIO pins so they're not simulated
    // TODO: If one of these fails, we should log and terminate
    if (settingsMap.count(userButtonPin) > 0 && settingsMap[userButtonPin] != RADIOLIB_NC) {
        if (initGPIOPin(settingsMap[userButtonPin], defaultGpioChipName, settingsMap[userButtonPin]) != ERRNO_OK) {
            settingsMap[userButtonPin] = RADIOLIB_NC;
        }
    }
    if (settingsMap.count(tbUpPin) > 0 && settingsMap[tbUpPin] != RADIOLIB_NC) {
        if (initGPIOPin(settingsMap[tbUpPin], defaultGpioChipName, settingsMap[tbUpPin]) != ERRNO_OK) {
            settingsMap[tbUpPin] = RADIOLIB_NC;
        }
    }
    if (settingsMap.count(tbDownPin) > 0 && settingsMap[tbDownPin] != RADIOLIB_NC) {
        if (initGPIOPin(settingsMap[tbDownPin], defaultGpioChipName, settingsMap[tbDownPin]) != ERRNO_OK) {
            settingsMap[tbDownPin] = RADIOLIB_NC;
        }
    }
    if (settingsMap.count(tbLeftPin) > 0 && settingsMap[tbLeftPin] != RADIOLIB_NC) {
        if (initGPIOPin(settingsMap[tbLeftPin], defaultGpioChipName, settingsMap[tbLeftPin]) != ERRNO_OK) {
            settingsMap[tbLeftPin] = RADIOLIB_NC;
        }
    }
    if (settingsMap.count(tbRightPin) > 0 && settingsMap[tbRightPin] != RADIOLIB_NC) {
        if (initGPIOPin(settingsMap[tbRightPin], defaultGpioChipName, settingsMap[tbRightPin]) != ERRNO_OK) {
            settingsMap[tbRightPin] = RADIOLIB_NC;
        }
    }
    if (settingsMap.count(tbPressPin) > 0 && settingsMap[tbPressPin] != RADIOLIB_NC) {
        if (initGPIOPin(settingsMap[tbPressPin], defaultGpioChipName, settingsMap[tbPressPin]) != ERRNO_OK) {
            settingsMap[tbPressPin] = RADIOLIB_NC;
        }
    }
    if (settingsMap[displayPanel] != no_screen) {
        if (settingsMap[displayCS] > 0)
            initGPIOPin(settingsMap[displayCS], defaultGpioChipName, settingsMap[displayCS]);
        if (settingsMap[displayDC] > 0)
            initGPIOPin(settingsMap[displayDC], defaultGpioChipName, settingsMap[displayDC]);
        if (settingsMap[displayBacklight] > 0)
            initGPIOPin(settingsMap[displayBacklight], defaultGpioChipName, settingsMap[displayBacklight]);
        if (settingsMap[displayReset] > 0)
            initGPIOPin(settingsMap[displayReset], defaultGpioChipName, settingsMap[displayReset]);
    }
    if (settingsMap[touchscreenModule] != no_touchscreen) {
        if (settingsMap[touchscreenCS] > 0)
            initGPIOPin(settingsMap[touchscreenCS], defaultGpioChipName, settingsMap[touchscreenCS]);
        if (settingsMap[touchscreenIRQ] > 0)
            initGPIOPin(settingsMap[touchscreenIRQ], defaultGpioChipName, settingsMap[touchscreenIRQ]);
    }

    // Only initialize the radio pins when dealing with real, kernel controlled SPI hardware
    if (settingsStrings[spidev] != "" && settingsStrings[spidev] != "ch341") {
        const struct {
            configNames pin;
            configNames gpiochip;
            configNames line;
        } pinMappings[] = {{cs_pin, cs_gpiochip, cs_line},
                           {irq_pin, irq_gpiochip, irq_line},
                           {busy_pin, busy_gpiochip, busy_line},
                           {reset_pin, reset_gpiochip, reset_line},
                           {rxen_pin, rxen_gpiochip, rxen_line},
                           {txen_pin, txen_gpiochip, txen_line},
                           {sx126x_ant_sw_pin, sx126x_ant_sw_gpiochip, sx126x_ant_sw_line}};
        for (auto &pinMap : pinMappings) {
            auto setMapIter = settingsMap.find(pinMap.pin);
            if (setMapIter != settingsMap.end() && setMapIter->second != RADIOLIB_NC) {
                if (initGPIOPin(setMapIter->second, gpioChipName + std::to_string(settingsMap[pinMap.gpiochip]),
                                settingsMap[pinMap.line]) != ERRNO_OK) {
                    printf("Error setting pin number %d. It may not exist, or may already be in use.\n",
                           settingsMap[pinMap.line]);
                    exit(EXIT_FAILURE);
                }
            }
        }
        SPI.begin(settingsStrings[spidev].c_str());
    }
    if (settingsStrings[traceFilename] != "") {
        try {
            traceFile.open(settingsStrings[traceFilename], std::ios::out | std::ios::app);
        } catch (std::ofstream::failure &e) {
            std::cout << "*** traceFile Exception " << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    if (verboseEnabled && settingsMap[logoutputlevel] != level_trace) {
        settingsMap[logoutputlevel] = level_debug;
    }

    return;
}

int initGPIOPin(int pinNum, const std::string gpioChipName, int line)
{
#ifdef PORTDUINO_LINUX_HARDWARE
    std::string gpio_name = "GPIO" + std::to_string(pinNum);
    std::cout << gpio_name;
    printf("\n");
    try {
        GPIOPin *csPin;
        csPin = new LinuxGPIOPin(pinNum, gpioChipName.c_str(), line, gpio_name.c_str());
        csPin->setSilent();
        gpioBind(csPin);
        return ERRNO_OK;
    } catch (...) {
        const std::type_info *t = abi::__cxa_current_exception_type();
        std::cout << "Warning, cannot claim pin " << gpio_name << (t ? t->name() : "null") << std::endl;
        return ERRNO_DISABLED;
    }
#else
    return ERRNO_OK;
#endif
}

bool loadConfig(const char *configPath)
{
    YAML::Node yamlConfig;
    try {
        yamlConfig = YAML::LoadFile(configPath);
        if (yamlConfig["Logging"]) {
            if (yamlConfig["Logging"]["LogLevel"].as<std::string>("info") == "trace") {
                settingsMap[logoutputlevel] = level_trace;
            } else if (yamlConfig["Logging"]["LogLevel"].as<std::string>("info") == "debug") {
                settingsMap[logoutputlevel] = level_debug;
            } else if (yamlConfig["Logging"]["LogLevel"].as<std::string>("info") == "info") {
                settingsMap[logoutputlevel] = level_info;
            } else if (yamlConfig["Logging"]["LogLevel"].as<std::string>("info") == "warn") {
                settingsMap[logoutputlevel] = level_warn;
            } else if (yamlConfig["Logging"]["LogLevel"].as<std::string>("info") == "error") {
                settingsMap[logoutputlevel] = level_error;
            }
            settingsStrings[traceFilename] = yamlConfig["Logging"]["TraceFile"].as<std::string>("");
            if (yamlConfig["Logging"]["AsciiLogs"]) {
                // Default is !isatty(1) but can be set explicitly in config.yaml
                settingsMap[ascii_logs] = yamlConfig["Logging"]["AsciiLogs"].as<bool>();
            }
        }
        if (yamlConfig["Lora"]) {
            const struct {
                configNames cfgName;
                std::string strName;
            } loraModules[] = {{use_simradio, "sim"},  {use_autoconf, "auto"}, {use_rf95, "RF95"},     {use_sx1262, "sx1262"},
                               {use_sx1268, "sx1268"}, {use_sx1280, "sx1280"}, {use_lr1110, "lr1110"}, {use_lr1120, "lr1120"},
                               {use_lr1121, "lr1121"}, {use_llcc68, "LLCC68"}};
            for (auto &loraModule : loraModules) {
                settingsMap[loraModule.cfgName] = false;
            }
            if (yamlConfig["Lora"]["Module"]) {
                for (auto &loraModule : loraModules) {
                    if (yamlConfig["Lora"]["Module"].as<std::string>("") == loraModule.strName) {
                        settingsMap[loraModule.cfgName] = true;
                        break;
                    }
                }
            }

            settingsMap[sx126x_max_power] = yamlConfig["Lora"]["SX126X_MAX_POWER"].as<int>(22);
            settingsMap[sx128x_max_power] = yamlConfig["Lora"]["SX128X_MAX_POWER"].as<int>(13);
            settingsMap[lr1110_max_power] = yamlConfig["Lora"]["LR1110_MAX_POWER"].as<int>(22);
            settingsMap[lr1120_max_power] = yamlConfig["Lora"]["LR1120_MAX_POWER"].as<int>(13);
            settingsMap[rf95_max_power] = yamlConfig["Lora"]["RF95_MAX_POWER"].as<int>(20);

            settingsMap[dio2_as_rf_switch] = yamlConfig["Lora"]["DIO2_AS_RF_SWITCH"].as<bool>(false);
            settingsMap[dio3_tcxo_voltage] = yamlConfig["Lora"]["DIO3_TCXO_VOLTAGE"].as<float>(0) * 1000;
            if (settingsMap[dio3_tcxo_voltage] == 0 && yamlConfig["Lora"]["DIO3_TCXO_VOLTAGE"].as<bool>(false)) {
                settingsMap[dio3_tcxo_voltage] = 1800; // default millivolts for "true"
            }

            // backwards API compatibility and to globally set gpiochip once
            int defaultGpioChip = settingsMap[default_gpiochip] = yamlConfig["Lora"]["gpiochip"].as<int>(0);

            const struct {
                configNames pin;
                configNames gpiochip;
                configNames line;
                std::string strName;
            } pinMappings[] = {
                {cs_pin, cs_gpiochip, cs_line, "CS"},
                {irq_pin, irq_gpiochip, irq_line, "IRQ"},
                {busy_pin, busy_gpiochip, busy_line, "Busy"},
                {reset_pin, reset_gpiochip, reset_line, "Reset"},
                {txen_pin, txen_gpiochip, txen_line, "TXen"},
                {rxen_pin, rxen_gpiochip, rxen_line, "RXen"},
                {sx126x_ant_sw_pin, sx126x_ant_sw_gpiochip, sx126x_ant_sw_line, "SX126X_ANT_SW"},
            };
            for (auto &pinMap : pinMappings) {
                if (yamlConfig["Lora"][pinMap.strName].IsMap()) {
                    settingsMap[pinMap.pin] = yamlConfig["Lora"][pinMap.strName]["pin"].as<int>(RADIOLIB_NC);
                    settingsMap[pinMap.line] = yamlConfig["Lora"][pinMap.strName]["line"].as<int>(settingsMap[pinMap.pin]);
                    settingsMap[pinMap.gpiochip] = yamlConfig["Lora"][pinMap.strName]["gpiochip"].as<int>(defaultGpioChip);
                } else { // backwards API compatibility
                    settingsMap[pinMap.pin] = yamlConfig["Lora"][pinMap.strName].as<int>(RADIOLIB_NC);
                    settingsMap[pinMap.line] = settingsMap[pinMap.pin];
                    settingsMap[pinMap.gpiochip] = defaultGpioChip;
                }
            }

            settingsMap[spiSpeed] = yamlConfig["Lora"]["spiSpeed"].as<int>(2000000);
            settingsStrings[lora_usb_serial_num] = yamlConfig["Lora"]["USB_Serialnum"].as<std::string>("");
            settingsMap[lora_usb_pid] = yamlConfig["Lora"]["USB_PID"].as<int>(0x5512);
            settingsMap[lora_usb_vid] = yamlConfig["Lora"]["USB_VID"].as<int>(0x1A86);

            settingsStrings[spidev] = yamlConfig["Lora"]["spidev"].as<std::string>("spidev0.0");
            if (settingsStrings[spidev] != "ch341") {
                settingsStrings[spidev] = "/dev/" + settingsStrings[spidev];
                if (settingsStrings[spidev].length() == 14) {
                    int x = settingsStrings[spidev].at(11) - '0';
                    int y = settingsStrings[spidev].at(13) - '0';
                    // Pretty sure this is always true
                    if (x >= 0 && x < 10 && y >= 0 && y < 10) {
                        // I believe this bit of weirdness is specifically for the new GUI
                        settingsMap[spidev] = x + y << 4;
                        settingsMap[displayspidev] = settingsMap[spidev];
                        settingsMap[touchscreenspidev] = settingsMap[spidev];
                    }
                }
            }
        }
        if (yamlConfig["GPIO"]) {
            settingsMap[userButtonPin] = yamlConfig["GPIO"]["User"].as<int>(RADIOLIB_NC);
        }
        if (yamlConfig["GPS"]) {
            std::string serialPath = yamlConfig["GPS"]["SerialPath"].as<std::string>("");
            if (serialPath != "") {
                Serial1.setPath(serialPath);
                settingsMap[has_gps] = 1;
            }
        }
        if (yamlConfig["I2C"]) {
            settingsStrings[i2cdev] = yamlConfig["I2C"]["I2CDevice"].as<std::string>("");
        }
        if (yamlConfig["Display"]) {
            if (yamlConfig["Display"]["Panel"].as<std::string>("") == "ST7789")
                settingsMap[displayPanel] = st7789;
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "ST7735")
                settingsMap[displayPanel] = st7735;
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "ST7735S")
                settingsMap[displayPanel] = st7735s;
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "ST7796")
                settingsMap[displayPanel] = st7796;
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "ILI9341")
                settingsMap[displayPanel] = ili9341;
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "ILI9342")
                settingsMap[displayPanel] = ili9342;
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "ILI9486")
                settingsMap[displayPanel] = ili9486;
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "ILI9488")
                settingsMap[displayPanel] = ili9488;
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "HX8357D")
                settingsMap[displayPanel] = hx8357d;
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "X11")
                settingsMap[displayPanel] = x11;
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "FB")
                settingsMap[displayPanel] = fb;
            settingsMap[displayHeight] = yamlConfig["Display"]["Height"].as<int>(0);
            settingsMap[displayWidth] = yamlConfig["Display"]["Width"].as<int>(0);
            settingsMap[displayDC] = yamlConfig["Display"]["DC"].as<int>(-1);
            settingsMap[displayCS] = yamlConfig["Display"]["CS"].as<int>(-1);
            settingsMap[displayRGBOrder] = yamlConfig["Display"]["RGBOrder"].as<bool>(false);
            settingsMap[displayBacklight] = yamlConfig["Display"]["Backlight"].as<int>(-1);
            settingsMap[displayBacklightInvert] = yamlConfig["Display"]["BacklightInvert"].as<bool>(false);
            settingsMap[displayBacklightPWMChannel] = yamlConfig["Display"]["BacklightPWMChannel"].as<int>(-1);
            settingsMap[displayReset] = yamlConfig["Display"]["Reset"].as<int>(-1);
            settingsMap[displayOffsetX] = yamlConfig["Display"]["OffsetX"].as<int>(0);
            settingsMap[displayOffsetY] = yamlConfig["Display"]["OffsetY"].as<int>(0);
            settingsMap[displayRotate] = yamlConfig["Display"]["Rotate"].as<bool>(false);
            settingsMap[displayOffsetRotate] = yamlConfig["Display"]["OffsetRotate"].as<int>(1);
            settingsMap[displayInvert] = yamlConfig["Display"]["Invert"].as<bool>(false);
            settingsMap[displayBusFrequency] = yamlConfig["Display"]["BusFrequency"].as<int>(40000000);
            if (yamlConfig["Display"]["spidev"]) {
                settingsStrings[displayspidev] = "/dev/" + yamlConfig["Display"]["spidev"].as<std::string>("spidev0.1");
                if (settingsStrings[displayspidev].length() == 14) {
                    int x = settingsStrings[displayspidev].at(11) - '0';
                    int y = settingsStrings[displayspidev].at(13) - '0';
                    if (x >= 0 && x < 10 && y >= 0 && y < 10) {
                        settingsMap[displayspidev] = x + y << 4;
                        settingsMap[touchscreenspidev] = settingsMap[displayspidev];
                    }
                }
            }
        }
        if (yamlConfig["Touchscreen"]) {
            if (yamlConfig["Touchscreen"]["Module"].as<std::string>("") == "XPT2046")
                settingsMap[touchscreenModule] = xpt2046;
            else if (yamlConfig["Touchscreen"]["Module"].as<std::string>("") == "STMPE610")
                settingsMap[touchscreenModule] = stmpe610;
            else if (yamlConfig["Touchscreen"]["Module"].as<std::string>("") == "GT911")
                settingsMap[touchscreenModule] = gt911;
            else if (yamlConfig["Touchscreen"]["Module"].as<std::string>("") == "FT5x06")
                settingsMap[touchscreenModule] = ft5x06;
            settingsMap[touchscreenCS] = yamlConfig["Touchscreen"]["CS"].as<int>(-1);
            settingsMap[touchscreenIRQ] = yamlConfig["Touchscreen"]["IRQ"].as<int>(-1);
            settingsMap[touchscreenBusFrequency] = yamlConfig["Touchscreen"]["BusFrequency"].as<int>(1000000);
            settingsMap[touchscreenRotate] = yamlConfig["Touchscreen"]["Rotate"].as<int>(-1);
            settingsMap[touchscreenI2CAddr] = yamlConfig["Touchscreen"]["I2CAddr"].as<int>(-1);
            if (yamlConfig["Touchscreen"]["spidev"]) {
                settingsStrings[touchscreenspidev] = "/dev/" + yamlConfig["Touchscreen"]["spidev"].as<std::string>("");
                if (settingsStrings[touchscreenspidev].length() == 14) {
                    int x = settingsStrings[touchscreenspidev].at(11) - '0';
                    int y = settingsStrings[touchscreenspidev].at(13) - '0';
                    if (x >= 0 && x < 10 && y >= 0 && y < 10) {
                        settingsMap[touchscreenspidev] = x + y << 4;
                    }
                }
            }
        }
        if (yamlConfig["Input"]) {
            settingsStrings[keyboardDevice] = (yamlConfig["Input"]["KeyboardDevice"]).as<std::string>("");
            settingsStrings[pointerDevice] = (yamlConfig["Input"]["PointerDevice"]).as<std::string>("");
            settingsMap[userButtonPin] = yamlConfig["Input"]["User"].as<int>(RADIOLIB_NC);
            settingsMap[tbUpPin] = yamlConfig["Input"]["TrackballUp"].as<int>(RADIOLIB_NC);
            settingsMap[tbDownPin] = yamlConfig["Input"]["TrackballDown"].as<int>(RADIOLIB_NC);
            settingsMap[tbLeftPin] = yamlConfig["Input"]["TrackballLeft"].as<int>(RADIOLIB_NC);
            settingsMap[tbRightPin] = yamlConfig["Input"]["TrackballRight"].as<int>(RADIOLIB_NC);
            settingsMap[tbPressPin] = yamlConfig["Input"]["TrackballPress"].as<int>(RADIOLIB_NC);
            if (yamlConfig["Input"]["TrackballDirection"].as<std::string>("RISING") == "RISING") {
                settingsMap[tbDirection] = 4;
            } else if (yamlConfig["Input"]["TrackballDirection"].as<std::string>("RISING") == "FALLING") {
                settingsMap[tbDirection] = 3;
            }
        }

        if (yamlConfig["Webserver"]) {
            settingsMap[webserverport] = (yamlConfig["Webserver"]["Port"]).as<int>(-1);
            settingsStrings[webserverrootpath] =
                (yamlConfig["Webserver"]["RootPath"]).as<std::string>("/usr/share/meshtasticd/web");
            settingsStrings[websslkeypath] =
                (yamlConfig["Webserver"]["SSLKey"]).as<std::string>("/etc/meshtasticd/ssl/private_key.pem");
            settingsStrings[websslcertpath] =
                (yamlConfig["Webserver"]["SSLCert"]).as<std::string>("/etc/meshtasticd/ssl/certificate.pem");
        }

        if (yamlConfig["HostMetrics"]) {
            settingsMap[hostMetrics_channel] = (yamlConfig["HostMetrics"]["Channel"]).as<int>(0);
            settingsMap[hostMetrics_interval] = (yamlConfig["HostMetrics"]["ReportInterval"]).as<int>(0);
            settingsStrings[hostMetrics_user_command] = (yamlConfig["HostMetrics"]["UserStringCommand"]).as<std::string>("");
        }

        if (yamlConfig["Config"]) {
            if (yamlConfig["Config"]["DisplayMode"]) {
                settingsMap[has_configDisplayMode] = true;
                if ((yamlConfig["Config"]["DisplayMode"]).as<std::string>("") == "TWOCOLOR") {
                    settingsMap[configDisplayMode] = meshtastic_Config_DisplayConfig_DisplayMode_TWOCOLOR;
                } else if ((yamlConfig["Config"]["DisplayMode"]).as<std::string>("") == "INVERTED") {
                    settingsMap[configDisplayMode] = meshtastic_Config_DisplayConfig_DisplayMode_INVERTED;
                } else if ((yamlConfig["Config"]["DisplayMode"]).as<std::string>("") == "COLOR") {
                    settingsMap[configDisplayMode] = meshtastic_Config_DisplayConfig_DisplayMode_COLOR;
                } else {
                    settingsMap[configDisplayMode] = meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT;
                }
            }
        }

        if (yamlConfig["General"]) {
            settingsMap[maxnodes] = (yamlConfig["General"]["MaxNodes"]).as<int>(200);
            settingsMap[maxtophone] = (yamlConfig["General"]["MaxMessageQueue"]).as<int>(100);
            settingsStrings[config_directory] = (yamlConfig["General"]["ConfigDirectory"]).as<std::string>("");
            settingsStrings[available_directory] =
                (yamlConfig["General"]["AvailableDirectory"]).as<std::string>("/etc/meshtasticd/available.d/");
            if ((yamlConfig["General"]["MACAddress"]).as<std::string>("") != "" &&
                (yamlConfig["General"]["MACAddressSource"]).as<std::string>("") != "") {
                std::cout << "Cannot set both MACAddress and MACAddressSource!" << std::endl;
                exit(EXIT_FAILURE);
            }
            settingsStrings[mac_address] = (yamlConfig["General"]["MACAddress"]).as<std::string>("");
            if ((yamlConfig["General"]["MACAddressSource"]).as<std::string>("") != "") {
                std::ifstream infile("/sys/class/net/" + (yamlConfig["General"]["MACAddressSource"]).as<std::string>("") +
                                     "/address");
                std::getline(infile, settingsStrings[mac_address]);
            }

            // https://stackoverflow.com/a/20326454
            settingsStrings[mac_address].erase(
                std::remove(settingsStrings[mac_address].begin(), settingsStrings[mac_address].end(), ':'),
                settingsStrings[mac_address].end());
        }
    } catch (YAML::Exception &e) {
        std::cout << "*** Exception " << e.what() << std::endl;
        return false;
    }
    return true;
}

// https://stackoverflow.com/questions/874134/find-out-if-string-ends-with-another-string-in-c
static bool ends_with(std::string_view str, std::string_view suffix)
{
    return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool MAC_from_string(std::string mac_str, uint8_t *dmac)
{
    mac_str.erase(std::remove(mac_str.begin(), mac_str.end(), ':'), mac_str.end());
    if (mac_str.length() == 12) {
        dmac[0] = std::stoi(settingsStrings[mac_address].substr(0, 2), nullptr, 16);
        dmac[1] = std::stoi(settingsStrings[mac_address].substr(2, 2), nullptr, 16);
        dmac[2] = std::stoi(settingsStrings[mac_address].substr(4, 2), nullptr, 16);
        dmac[3] = std::stoi(settingsStrings[mac_address].substr(6, 2), nullptr, 16);
        dmac[4] = std::stoi(settingsStrings[mac_address].substr(8, 2), nullptr, 16);
        dmac[5] = std::stoi(settingsStrings[mac_address].substr(10, 2), nullptr, 16);
        return true;
    } else {
        return false;
    }
}

std::string exec(const char *cmd)
{ // https://stackoverflow.com/a/478960
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}