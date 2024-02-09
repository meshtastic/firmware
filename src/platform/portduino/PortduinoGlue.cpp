#include "CryptoEngine.h"
#include "PortduinoGPIO.h"
#include "SPIChip.h"
#include "mesh/RF95Interface.h"
#include "sleep.h"
#include "target_specific.h"

#include <Utility.h>
#include <assert.h>

#include "PortduinoGlue.h"
#include "linux/gpio/LinuxGPIOPin.h"
#include "yaml-cpp/yaml.h"
#include <iostream>
#include <map>
#include <unistd.h>

std::map<configNames, int> settingsMap;
std::map<configNames, std::string> settingsStrings;
char *configPath = nullptr;

// FIXME - move setBluetoothEnable into a HALPlatform class
void setBluetoothEnable(bool on)
{
    // not needed
}

void cpuDeepSleep(uint32_t msecs)
{
    notImplemented("cpuDeepSleep");
}

void updateBatteryLevel(uint8_t level) NOT_IMPLEMENTED("updateBatteryLevel");

int TCPPort = 4403;

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
                                           {0}};
    static void *childArguments;
    static char doc[] = "Meshtastic native build.";
    static char args_doc[] = "...";
    static struct argp argp = {options, parse_opt, args_doc, doc, 0, 0, 0};
    const struct argp_child child = {&argp, OPTION_ARG_OPTIONAL, 0, 0};
    portduinoAddArguments(child, childArguments);
}

/** apps run under portduino can optionally define a portduinoSetup() to
 * use portduino specific init code (such as gpioBind) to setup portduino on their host machine,
 * before running 'arduino' code.
 */
void portduinoSetup()
{
    printf("Setting up Meshtastic on Portduino...\n");
    gpioInit();

    std::string gpioChipName = "gpiochip";

    YAML::Node yamlConfig;

    if (configPath != nullptr) {
        std::cout << "Using " << configPath << " as config file" << std::endl;
        try {
            yamlConfig = YAML::LoadFile(configPath);
        } catch (YAML::Exception e) {
            std::cout << "Could not open " << configPath << " because of error: " << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
    } else if (access("config.yaml", R_OK) == 0) {
        std::cout << "Using local config.yaml as config file" << std::endl;
        try {
            yamlConfig = YAML::LoadFile("config.yaml");
        } catch (YAML::Exception e) {
            std::cout << "*** Exception " << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
    } else if (access("/etc/meshtasticd/config.yaml", R_OK) == 0) {
        std::cout << "Using /etc/meshtasticd/config.yaml as config file" << std::endl;
        try {
            yamlConfig = YAML::LoadFile("/etc/meshtasticd/config.yaml");
        } catch (YAML::Exception e) {
            std::cout << "*** Exception " << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
    } else {
        std::cout << "No 'config.yaml' found, running simulated." << std::endl;
        // Set the random seed equal to TCPPort to have a different seed per instance
        randomSeed(TCPPort);
        return;
    }

    try {
        if (yamlConfig["Logging"]) {
            if (yamlConfig["Logging"]["LogLevel"].as<std::string>("info") == "debug") {
                settingsMap[logoutputlevel] = level_debug;
            } else if (yamlConfig["Logging"]["LogLevel"].as<std::string>("info") == "info") {
                settingsMap[logoutputlevel] = level_info;
            } else if (yamlConfig["Logging"]["LogLevel"].as<std::string>("info") == "warn") {
                settingsMap[logoutputlevel] = level_warn;
            } else if (yamlConfig["Logging"]["LogLevel"].as<std::string>("info") == "error") {
                settingsMap[logoutputlevel] = level_error;
            }
        }
        if (yamlConfig["Lora"]) {
            settingsMap[use_sx1262] = false;
            settingsMap[use_rf95] = false;
            settingsMap[use_sx1280] = false;

            if (yamlConfig["Lora"]["Module"] && yamlConfig["Lora"]["Module"].as<std::string>("") == "sx1262") {
                settingsMap[use_sx1262] = true;
            } else if (yamlConfig["Lora"]["Module"] && yamlConfig["Lora"]["Module"].as<std::string>("") == "RF95") {
                settingsMap[use_rf95] = true;
            } else if (yamlConfig["Lora"]["Module"] && yamlConfig["Lora"]["Module"].as<std::string>("") == "sx1280") {
                settingsMap[use_sx1280] = true;
            }
            settingsMap[dio2_as_rf_switch] = yamlConfig["Lora"]["DIO2_AS_RF_SWITCH"].as<bool>(false);
            settingsMap[dio3_tcxo_voltage] = yamlConfig["Lora"]["DIO3_TCXO_VOLTAGE"].as<bool>(false);
            settingsMap[cs] = yamlConfig["Lora"]["CS"].as<int>(RADIOLIB_NC);
            settingsMap[irq] = yamlConfig["Lora"]["IRQ"].as<int>(RADIOLIB_NC);
            settingsMap[busy] = yamlConfig["Lora"]["Busy"].as<int>(RADIOLIB_NC);
            settingsMap[reset] = yamlConfig["Lora"]["Reset"].as<int>(RADIOLIB_NC);
            settingsMap[txen] = yamlConfig["Lora"]["TXen"].as<int>(RADIOLIB_NC);
            settingsMap[rxen] = yamlConfig["Lora"]["RXen"].as<int>(RADIOLIB_NC);
            settingsMap[gpiochip] = yamlConfig["Lora"]["gpiochip"].as<int>(0);
            gpioChipName += std::to_string(settingsMap[gpiochip]);

            settingsStrings[spidev] = "/dev/" + yamlConfig["Lora"]["spidev"].as<std::string>("spidev0.0");
        }
        if (yamlConfig["GPIO"]) {
            settingsMap[user] = yamlConfig["GPIO"]["User"].as<int>(RADIOLIB_NC);
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
        settingsMap[displayPanel] = no_screen;
        if (yamlConfig["Display"]) {
            if (yamlConfig["Display"]["Panel"].as<std::string>("") == "ST7789")
                settingsMap[displayPanel] = st7789;
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "ST7735")
                settingsMap[displayPanel] = st7735;
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "ST7735S")
                settingsMap[displayPanel] = st7735s;
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "ILI9341")
                settingsMap[displayPanel] = ili9341;
            settingsMap[displayHeight] = yamlConfig["Display"]["Height"].as<int>(0);
            settingsMap[displayWidth] = yamlConfig["Display"]["Width"].as<int>(0);
            settingsMap[displayDC] = yamlConfig["Display"]["DC"].as<int>(-1);
            settingsMap[displayCS] = yamlConfig["Display"]["CS"].as<int>(-1);
            settingsMap[displayBacklight] = yamlConfig["Display"]["Backlight"].as<int>(-1);
            settingsMap[displayReset] = yamlConfig["Display"]["Reset"].as<int>(-1);
            settingsMap[displayOffsetX] = yamlConfig["Display"]["OffsetX"].as<int>(0);
            settingsMap[displayOffsetY] = yamlConfig["Display"]["OffsetY"].as<int>(0);
            settingsMap[displayRotate] = yamlConfig["Display"]["Rotate"].as<bool>(false);
            settingsMap[displayInvert] = yamlConfig["Display"]["Invert"].as<bool>(false);
        }
        settingsMap[touchscreenModule] = no_touchscreen;
        if (yamlConfig["Touchscreen"]) {
            if (yamlConfig["Touchscreen"]["Module"].as<std::string>("") == "XPT2046")
                settingsMap[touchscreenModule] = xpt2046;
            else if (yamlConfig["Touchscreen"]["Module"].as<std::string>("") == "STMPE610")
                settingsMap[touchscreenModule] = stmpe610;
            settingsMap[touchscreenCS] = yamlConfig["Touchscreen"]["CS"].as<int>(-1);
            settingsMap[touchscreenIRQ] = yamlConfig["Touchscreen"]["IRQ"].as<int>(-1);
        }
        if (yamlConfig["Input"]) {
            settingsStrings[keyboardDevice] = (yamlConfig["Input"]["KeyboardDevice"]).as<std::string>("");
        }

    } catch (YAML::Exception e) {
        std::cout << "*** Exception " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    // Need to bind all the configured GPIO pins so they're not simulated
    if (settingsMap.count(cs) > 0 && settingsMap[cs] != RADIOLIB_NC) {
        if (initGPIOPin(settingsMap[cs], gpioChipName) != ERRNO_OK) {
            settingsMap[cs] = RADIOLIB_NC;
        }
    }
    if (settingsMap.count(irq) > 0 && settingsMap[irq] != RADIOLIB_NC) {
        if (initGPIOPin(settingsMap[irq], gpioChipName) != ERRNO_OK) {
            settingsMap[irq] = RADIOLIB_NC;
        }
    }
    if (settingsMap.count(busy) > 0 && settingsMap[busy] != RADIOLIB_NC) {
        if (initGPIOPin(settingsMap[busy], gpioChipName) != ERRNO_OK) {
            settingsMap[busy] = RADIOLIB_NC;
        }
    }
    if (settingsMap.count(reset) > 0 && settingsMap[reset] != RADIOLIB_NC) {
        if (initGPIOPin(settingsMap[reset], gpioChipName) != ERRNO_OK) {
            settingsMap[reset] = RADIOLIB_NC;
        }
    }
    if (settingsMap.count(user) > 0 && settingsMap[user] != RADIOLIB_NC) {
        if (initGPIOPin(settingsMap[user], gpioChipName) != ERRNO_OK) {
            settingsMap[user] = RADIOLIB_NC;
        }
    }
    if (settingsMap.count(rxen) > 0 && settingsMap[rxen] != RADIOLIB_NC) {
        if (initGPIOPin(settingsMap[rxen], gpioChipName) != ERRNO_OK) {
            settingsMap[rxen] = RADIOLIB_NC;
        }
    }
    if (settingsMap.count(txen) > 0 && settingsMap[txen] != RADIOLIB_NC) {
        if (initGPIOPin(settingsMap[txen], gpioChipName) != ERRNO_OK) {
            settingsMap[txen] = RADIOLIB_NC;
        }
    }

    if (settingsMap[displayPanel] != no_screen) {
        if (settingsMap[displayCS] > 0)
            initGPIOPin(settingsMap[displayCS], gpioChipName);
        if (settingsMap[displayDC] > 0)
            initGPIOPin(settingsMap[displayDC], gpioChipName);
        if (settingsMap[displayBacklight] > 0)
            initGPIOPin(settingsMap[displayBacklight], gpioChipName);
        if (settingsMap[displayReset] > 0)
            initGPIOPin(settingsMap[displayReset], gpioChipName);
    }
    if (settingsMap[touchscreenModule] != no_touchscreen) {
        if (settingsMap[touchscreenCS] > 0)
            initGPIOPin(settingsMap[touchscreenCS], gpioChipName);
        if (settingsMap[touchscreenIRQ] > 0)
            initGPIOPin(settingsMap[touchscreenIRQ], gpioChipName);
    }

    return;
}

int initGPIOPin(int pinNum, std::string gpioChipName)
{
    std::string gpio_name = "GPIO" + std::to_string(pinNum);
    try {
        GPIOPin *csPin;
        csPin = new LinuxGPIOPin(pinNum, gpioChipName.c_str(), pinNum, gpio_name.c_str());
        csPin->setSilent();
        gpioBind(csPin);
        return ERRNO_OK;
    } catch (...) {
        std::exception_ptr p = std::current_exception();
        std::cout << "Warning, cannot claim pin " << gpio_name << (p ? p.__cxa_exception_type()->name() : "null") << std::endl;
        return ERRNO_DISABLED;
    }
}