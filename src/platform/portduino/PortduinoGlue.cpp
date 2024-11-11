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
#include <filesystem>
#include <iostream>
#include <map>
#include <unistd.h>

std::map<configNames, int> settingsMap;
std::map<configNames, std::string> settingsStrings;
std::ofstream traceFile;
char *configPath = nullptr;

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
    printf("Set up Meshtastic on Portduino...\n");
    int max_GPIO = 0;
    const configNames GPIO_lines[] = {cs,
                                      irq,
                                      busy,
                                      reset,
                                      sx126x_ant_sw,
                                      txen,
                                      rxen,
                                      displayDC,
                                      displayCS,
                                      displayBacklight,
                                      displayBacklightPWMChannel,
                                      displayReset,
                                      touchscreenCS,
                                      touchscreenIRQ,
                                      user};

    std::string gpioChipName = "gpiochip";
    settingsStrings[i2cdev] = "";
    settingsStrings[keyboardDevice] = "";
    settingsStrings[webserverrootpath] = "";
    settingsStrings[spidev] = "";
    settingsStrings[displayspidev] = "";
    settingsMap[spiSpeed] = 2000000;
    settingsMap[ascii_logs] = !isatty(1);
    settingsMap[displayPanel] = no_screen;
    settingsMap[touchscreenModule] = no_touchscreen;

    YAML::Node yamlConfig;

    if (configPath != nullptr) {
        if (loadConfig(configPath)) {
            std::cout << "Using " << configPath << " as config file" << std::endl;
        } else {
            std::cout << "Unable to use " << configPath << " as config file" << std::endl;
            exit(EXIT_FAILURE);
        }
    } else if (access("config.yaml", R_OK) == 0 && loadConfig("config.yaml")) {
        std::cout << "Using local config.yaml as config file" << std::endl;
    } else if (access("/etc/meshtasticd/config.yaml", R_OK) == 0 && loadConfig("/etc/meshtasticd/config.yaml")) {
        std::cout << "Using /etc/meshtasticd/config.yaml as config file" << std::endl;
    } else {
        std::cout << "No 'config.yaml' found, running simulated." << std::endl;
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

    // Rather important to set this, if not running simulated.
    randomSeed(time(NULL));

    gpioChipName += std::to_string(settingsMap[gpiochip]);

    for (configNames i : GPIO_lines) {
        if (settingsMap.count(i) && settingsMap[i] > max_GPIO)
            max_GPIO = settingsMap[i];
    }

    gpioInit(max_GPIO + 1); // Done here so we can inform Portduino how many GPIOs we need.

    // Need to bind all the configured GPIO pins so they're not simulated
    // TODO: Can we do this in the for loop above?
    // TODO: If one of these fails, we should log and terminate
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
    if (settingsMap.count(sx126x_ant_sw) > 0 && settingsMap[sx126x_ant_sw] != RADIOLIB_NC) {
        if (initGPIOPin(settingsMap[sx126x_ant_sw], gpioChipName) != ERRNO_OK) {
            settingsMap[sx126x_ant_sw] = RADIOLIB_NC;
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

    if (settingsStrings[spidev] != "") {
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

    return;
}

int initGPIOPin(int pinNum, const std::string gpioChipName)
{
#ifdef PORTDUINO_LINUX_HARDWARE
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
            settingsMap[use_sx1262] = false;
            settingsMap[use_rf95] = false;
            settingsMap[use_sx1280] = false;
            settingsMap[use_sx1268] = false;

            if (yamlConfig["Lora"]["Module"] && yamlConfig["Lora"]["Module"].as<std::string>("") == "sx1262") {
                settingsMap[use_sx1262] = true;
            } else if (yamlConfig["Lora"]["Module"] && yamlConfig["Lora"]["Module"].as<std::string>("") == "RF95") {
                settingsMap[use_rf95] = true;
            } else if (yamlConfig["Lora"]["Module"] && yamlConfig["Lora"]["Module"].as<std::string>("") == "sx1280") {
                settingsMap[use_sx1280] = true;
            } else if (yamlConfig["Lora"]["Module"] && yamlConfig["Lora"]["Module"].as<std::string>("") == "sx1268") {
                settingsMap[use_sx1268] = true;
            }
            settingsMap[dio2_as_rf_switch] = yamlConfig["Lora"]["DIO2_AS_RF_SWITCH"].as<bool>(false);
            settingsMap[dio3_tcxo_voltage] = yamlConfig["Lora"]["DIO3_TCXO_VOLTAGE"].as<bool>(false);
            settingsMap[cs] = yamlConfig["Lora"]["CS"].as<int>(RADIOLIB_NC);
            settingsMap[irq] = yamlConfig["Lora"]["IRQ"].as<int>(RADIOLIB_NC);
            settingsMap[busy] = yamlConfig["Lora"]["Busy"].as<int>(RADIOLIB_NC);
            settingsMap[reset] = yamlConfig["Lora"]["Reset"].as<int>(RADIOLIB_NC);
            settingsMap[txen] = yamlConfig["Lora"]["TXen"].as<int>(RADIOLIB_NC);
            settingsMap[rxen] = yamlConfig["Lora"]["RXen"].as<int>(RADIOLIB_NC);
            settingsMap[sx126x_ant_sw] = yamlConfig["Lora"]["SX126X_ANT_SW"].as<int>(RADIOLIB_NC);
            settingsMap[gpiochip] = yamlConfig["Lora"]["gpiochip"].as<int>(0);
            settingsMap[ch341Quirk] = yamlConfig["Lora"]["ch341_quirk"].as<bool>(false);
            settingsMap[spiSpeed] = yamlConfig["Lora"]["spiSpeed"].as<int>(2000000);

            settingsStrings[spidev] = "/dev/" + yamlConfig["Lora"]["spidev"].as<std::string>("spidev0.0");
            if (settingsStrings[spidev].length() == 14) {
                int x = settingsStrings[spidev].at(11) - '0';
                int y = settingsStrings[spidev].at(13) - '0';
                if (x >= 0 && x < 10 && y >= 0 && y < 10) {
                    settingsMap[spidev] = x + y << 4;
                    settingsMap[displayspidev] = settingsMap[spidev];
                    settingsMap[touchscreenspidev] = settingsMap[spidev];
                }
            }
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
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "ILI9488")
                settingsMap[displayPanel] = ili9488;
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "HX8357D")
                settingsMap[displayPanel] = hx8357d;
            else if (yamlConfig["Display"]["Panel"].as<std::string>("") == "X11")
                settingsMap[displayPanel] = x11;
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
        }

        if (yamlConfig["Webserver"]) {
            settingsMap[webserverport] = (yamlConfig["Webserver"]["Port"]).as<int>(-1);
            settingsStrings[webserverrootpath] = (yamlConfig["Webserver"]["RootPath"]).as<std::string>("");
        }

        if (yamlConfig["General"]) {
            settingsMap[maxnodes] = (yamlConfig["General"]["MaxNodes"]).as<int>(200);
            settingsMap[maxtophone] = (yamlConfig["General"]["MaxMessageQueue"]).as<int>(100);
            settingsStrings[config_directory] = (yamlConfig["General"]["ConfigDirectory"]).as<std::string>("");
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