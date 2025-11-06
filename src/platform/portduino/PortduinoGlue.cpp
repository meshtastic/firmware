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
#include <ErriezCRC32.h>
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

portduino_config_struct portduino_config;
std::ofstream traceFile;
Ch341Hal *ch341Hal = nullptr;
char *configPath = nullptr;
char *optionMac = nullptr;
bool verboseEnabled = false;
bool yamlOnly = false;

const char *argp_program_version = optstr(APP_VERSION);

char stdoutBuffer[512];

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
        portduino_config.force_simradio = true;
        break;
    case 'h':
        optionMac = arg;
        break;
    case 'v':
        verboseEnabled = true;
        break;
    case 'y':
        yamlOnly = true;
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
                                           {"output-yaml", 'y', 0, 0, "Output config yaml and exit"},
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
    } else if (portduino_config.mac_address.length() > 11) {
        MAC_from_string(portduino_config.mac_address, dmac);
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
    int max_GPIO = 0;
    std::string gpioChipName = "gpiochip";
    portduino_config.displayPanel = no_screen;

    // Force stdout to be line buffered
    setvbuf(stdout, stdoutBuffer, _IOLBF, sizeof(stdoutBuffer));

    if (portduino_config.force_simradio == true) {
        portduino_config.lora_module = use_simradio;
    } else if (configPath != nullptr) {
        if (loadConfig(configPath)) {
            if (!yamlOnly)
                std::cout << "Using " << configPath << " as config file" << std::endl;
        } else {
            std::cout << "Unable to use " << configPath << " as config file" << std::endl;
            exit(EXIT_FAILURE);
        }
    } else if (access("config.yaml", R_OK) == 0) {
        if (loadConfig("config.yaml")) {
            if (!yamlOnly)
                std::cout << "Using local config.yaml as config file" << std::endl;
        } else {
            std::cout << "Unable to use local config.yaml as config file" << std::endl;
            exit(EXIT_FAILURE);
        }
    } else if (access("/etc/meshtasticd/config.yaml", R_OK) == 0) {
        if (loadConfig("/etc/meshtasticd/config.yaml")) {
            if (!yamlOnly)
                std::cout << "Using /etc/meshtasticd/config.yaml as config file" << std::endl;
        } else {
            std::cout << "Unable to use /etc/meshtasticd/config.yaml as config file" << std::endl;
            exit(EXIT_FAILURE);
        }
    } else {
        if (!yamlOnly)
            std::cout << "No 'config.yaml' found..." << std::endl;
        portduino_config.lora_module = use_simradio;
    }

    if (portduino_config.config_directory != "") {
        std::string filetype = ".yaml";
        for (const std::filesystem::directory_entry &entry :
             std::filesystem::directory_iterator{portduino_config.config_directory}) {
            if (ends_with(entry.path().string(), ".yaml")) {
                std::cout << "Also using " << entry << " as additional config file" << std::endl;
                loadConfig(entry.path().c_str());
            }
        }
    }

    if (yamlOnly) {
        std::cout << portduino_config.emit_yaml() << std::endl;
        exit(EXIT_SUCCESS);
    }

    if (portduino_config.force_simradio) {
        std::cout << "Running in simulated mode." << std::endl;
        portduino_config.MaxNodes = 200; // Default to 200 nodes
        // Set the random seed equal to TCPPort to have a different seed per instance
        randomSeed(TCPPort);
        return;
    }

    // If LoRa `Module: auto` (default in config.yaml),
    // attempt to auto config based on Product Strings
    if (portduino_config.lora_module == use_autoconf) {
        char autoconf_product[96] = {0};
        // Try CH341
        try {
            std::cout << "autoconf: Looking for CH341 device..." << std::endl;
            ch341Hal = new Ch341Hal(0, portduino_config.lora_usb_serial_num, portduino_config.lora_usb_vid,
                                    portduino_config.lora_usb_pid);
            ch341Hal->getProductString(autoconf_product, 95);
            delete ch341Hal;
            std::cout << "autoconf: Found CH341 device " << autoconf_product << std::endl;
        } catch (...) {
            std::cout << "autoconf: Could not locate CH341 device" << std::endl;
        }
        // Try Pi HAT+
        if (strlen(autoconf_product) < 6) {
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
        }
        // attempt to load autoconf data from an EEPROM on 0x50
        //      RAK6421-13300-S1:aabbcc123456:5ba85807d92138b7519cfb60460573af:3061e8d8
        // <model string>:mac address :<16 random unique bytes in hexidecimal> : crc32
        // crc32 is calculated on the eeprom string up to but not including the final colon
        if (strlen(autoconf_product) < 6) {
            try {
                char *mac_start = nullptr;
                char *devID_start = nullptr;
                char *crc32_start = nullptr;
                Wire.begin();
                Wire.beginTransmission(0x50);
                Wire.write(0x0);
                Wire.write(0x0);
                Wire.endTransmission();
                Wire.requestFrom((uint8_t)0x50, (uint8_t)75);
                uint8_t i = 0;
                delay(100);
                std::string autoconf_raw;
                while (Wire.available() && i < sizeof(autoconf_product)) {
                    autoconf_product[i] = Wire.read();
                    if (autoconf_product[i] == 0xff) {
                        autoconf_product[i] = 0x0;
                        break;
                    }
                    autoconf_raw += autoconf_product[i];
                    if (autoconf_product[i] == ':') {
                        autoconf_product[i] = 0x0;
                        if (mac_start == nullptr) {
                            mac_start = autoconf_product + i + 1;
                        } else if (devID_start == nullptr) {
                            devID_start = autoconf_product + i + 1;
                        } else if (crc32_start == nullptr) {
                            crc32_start = autoconf_product + i + 1;
                        }
                    }
                    i++;
                }
                if (crc32_start != nullptr && strlen(crc32_start) == 8) {
                    std::string crc32_str(crc32_start);
                    uint32_t crc32_value = 0;

                    // convert crc32 ascii to raw uint32
                    for (int j = 0; j < 4; j++) {
                        crc32_value += std::stoi(crc32_str.substr(j * 2, 2), nullptr, 16) << (3 - j) * 8;
                    }
                    std::cout << "autoconf: Found eeprom crc " << crc32_start << std::endl;

                    // set the autoconf string to blank and short circuit
                    if (crc32_value != crc32Buffer(autoconf_raw.c_str(), i - 9)) {
                        std::cout << "autoconf: crc32 mismatch, dropping " << std::endl;
                        autoconf_product[0] = 0x0;
                    } else {
                        std::cout << "autoconf: Found eeprom data " << autoconf_raw << std::endl;
                        if (mac_start != nullptr) {
                            std::cout << "autoconf: Found mac data " << mac_start << std::endl;
                            if (strlen(mac_start) == 12)
                                portduino_config.mac_address = std::string(mac_start);
                        }
                        if (devID_start != nullptr) {
                            std::cout << "autoconf: Found deviceid data " << devID_start << std::endl;
                            if (strlen(devID_start) == 32) {
                                std::string devID_str(devID_start);
                                for (int j = 0; j < 16; j++) {
                                    portduino_config.device_id[j] = std::stoi(devID_str.substr(j * 2, 2), nullptr, 16);
                                }
                                portduino_config.has_device_id = true;
                            }
                        }
                    }
                } else {
                    std::cout << "autoconf: crc32 missing " << std::endl;
                    autoconf_product[0] = 0x0;
                }
            } catch (...) {
                std::cout << "autoconf: Could not locate EEPROM" << std::endl;
            }
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
            if (loadConfig((portduino_config.available_directory + product_config).c_str())) {
                std::cout << "autoconf: Using " << product_config << " as config file for " << autoconf_product << std::endl;
            } else {
                std::cerr << "autoconf: Unable to use " << product_config << " as config file for " << autoconf_product
                          << std::endl;
                exit(EXIT_FAILURE);
            }
        } else {
            std::cerr << "autoconf: Could not locate any devices" << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    // if we're using a usermode driver, we need to initialize it here, to get a serial number back for mac address
    uint8_t dmac[6] = {0};
    if (portduino_config.lora_spi_dev == "ch341") {
        try {
            ch341Hal = new Ch341Hal(0, portduino_config.lora_usb_serial_num, portduino_config.lora_usb_vid,
                                    portduino_config.lora_usb_pid);
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
        if (strlen(serial) == 8 && portduino_config.mac_address.length() < 12) {
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
            portduino_config.mac_address = macBuf;
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

    std::string defaultGpioChipName = gpioChipName + std::to_string(portduino_config.lora_default_gpiochip);
    for (auto i : portduino_config.all_pins) {
        if (i->enabled && i->pin > max_GPIO)
            max_GPIO = i->pin;
    }

    gpioInit(max_GPIO + 1); // Done here so we can inform Portduino how many GPIOs we need.

    // Need to bind all the configured GPIO pins so they're not simulated
    // TODO: If one of these fails, we should log and terminate
    for (auto i : portduino_config.all_pins) {
        // In the case of a ch341 Lora device, we don't want to touch the system GPIO lines for Lora
        // Those GPIO are handled in our usermode driver instead.
        if (i->config_section == "Lora" && portduino_config.lora_spi_dev == "ch341") {
            continue;
        }
        if (i->enabled) {
            if (initGPIOPin(i->pin, gpioChipName + std::to_string(i->gpiochip), i->line) != ERRNO_OK) {
                printf("Error setting pin number %d. It may not exist, or may already be in use.\n", i->line);
                exit(EXIT_FAILURE);
            }
        }
    }

    // Only initialize the radio pins when dealing with real, kernel controlled SPI hardware
    if (portduino_config.lora_spi_dev != "" && portduino_config.lora_spi_dev != "ch341") {
        SPI.begin(portduino_config.lora_spi_dev.c_str());
    }
    if (portduino_config.traceFilename != "") {
        try {
            traceFile.open(portduino_config.traceFilename, std::ios::out | std::ios::app);
        } catch (std::ofstream::failure &e) {
            std::cout << "*** traceFile Exception " << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    if (verboseEnabled && portduino_config.logoutputlevel != level_trace) {
        portduino_config.logoutputlevel = level_debug;
    }

    return;
}

int initGPIOPin(int pinNum, const std::string gpioChipName, int line)
{
#ifdef PORTDUINO_LINUX_HARDWARE
    std::string gpio_name = "GPIO" + std::to_string(pinNum);
    std::cout << "Initializing " << gpio_name << " on chip " << gpioChipName << std::endl;
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
                portduino_config.logoutputlevel = level_trace;
            } else if (yamlConfig["Logging"]["LogLevel"].as<std::string>("info") == "debug") {
                portduino_config.logoutputlevel = level_debug;
            } else if (yamlConfig["Logging"]["LogLevel"].as<std::string>("info") == "info") {
                portduino_config.logoutputlevel = level_info;
            } else if (yamlConfig["Logging"]["LogLevel"].as<std::string>("info") == "warn") {
                portduino_config.logoutputlevel = level_warn;
            } else if (yamlConfig["Logging"]["LogLevel"].as<std::string>("info") == "error") {
                portduino_config.logoutputlevel = level_error;
            }
            portduino_config.traceFilename = yamlConfig["Logging"]["TraceFile"].as<std::string>("");
            if (yamlConfig["Logging"]["AsciiLogs"]) {
                // Default is !isatty(1) but can be set explicitly in config.yaml
                portduino_config.ascii_logs = yamlConfig["Logging"]["AsciiLogs"].as<bool>();
                portduino_config.ascii_logs_explicit = true;
            }
        }
        if (yamlConfig["Lora"]) {

            if (yamlConfig["Lora"]["Module"]) {
                for (auto &loraModule : portduino_config.loraModules) {
                    if (yamlConfig["Lora"]["Module"].as<std::string>("") == loraModule.second) {
                        portduino_config.lora_module = loraModule.first;
                        break;
                    }
                }
            }
            if (yamlConfig["Lora"]["SX126X_MAX_POWER"])
                portduino_config.sx126x_max_power = yamlConfig["Lora"]["SX126X_MAX_POWER"].as<int>(22);
            if (yamlConfig["Lora"]["SX128X_MAX_POWER"])
                portduino_config.sx128x_max_power = yamlConfig["Lora"]["SX128X_MAX_POWER"].as<int>(13);
            if (yamlConfig["Lora"]["LR1110_MAX_POWER"])
                portduino_config.lr1110_max_power = yamlConfig["Lora"]["LR1110_MAX_POWER"].as<int>(22);
            if (yamlConfig["Lora"]["LR1120_MAX_POWER"])
                portduino_config.lr1120_max_power = yamlConfig["Lora"]["LR1120_MAX_POWER"].as<int>(13);
            if (yamlConfig["Lora"]["RF95_MAX_POWER"])
                portduino_config.rf95_max_power = yamlConfig["Lora"]["RF95_MAX_POWER"].as<int>(20);

            if (portduino_config.lora_module != use_autoconf && portduino_config.lora_module != use_simradio &&
                !portduino_config.force_simradio) {
                portduino_config.dio2_as_rf_switch = yamlConfig["Lora"]["DIO2_AS_RF_SWITCH"].as<bool>(false);
                portduino_config.dio3_tcxo_voltage = yamlConfig["Lora"]["DIO3_TCXO_VOLTAGE"].as<float>(0) * 1000;
                if (portduino_config.dio3_tcxo_voltage == 0 && yamlConfig["Lora"]["DIO3_TCXO_VOLTAGE"].as<bool>(false)) {
                    portduino_config.dio3_tcxo_voltage = 1800; // default millivolts for "true"
                }

                // backwards API compatibility and to globally set gpiochip once
                portduino_config.lora_default_gpiochip = yamlConfig["Lora"]["gpiochip"].as<int>(0);
                for (auto this_pin : portduino_config.all_pins) {
                    if (this_pin->config_section == "Lora") {
                        readGPIOFromYaml(yamlConfig["Lora"][this_pin->config_name], *this_pin);
                    }
                }
            }

            portduino_config.spiSpeed = yamlConfig["Lora"]["spiSpeed"].as<int>(2000000);
            portduino_config.lora_usb_serial_num = yamlConfig["Lora"]["USB_Serialnum"].as<std::string>("");
            portduino_config.lora_usb_pid = yamlConfig["Lora"]["USB_PID"].as<int>(0x5512);
            portduino_config.lora_usb_vid = yamlConfig["Lora"]["USB_VID"].as<int>(0x1A86);

            portduino_config.lora_spi_dev = yamlConfig["Lora"]["spidev"].as<std::string>("spidev0.0");
            if (portduino_config.lora_spi_dev != "ch341") {
                portduino_config.lora_spi_dev = "/dev/" + portduino_config.lora_spi_dev;
                if (portduino_config.lora_spi_dev.length() == 14) {
                    int x = portduino_config.lora_spi_dev.at(11) - '0';
                    int y = portduino_config.lora_spi_dev.at(13) - '0';
                    // Pretty sure this is always true
                    if (x >= 0 && x < 10 && y >= 0 && y < 10) {
                        // I believe this bit of weirdness is specifically for the new GUI
                        portduino_config.lora_spi_dev_int = x + y << 4;
                        portduino_config.display_spi_dev_int = portduino_config.lora_spi_dev_int;
                        portduino_config.touchscreen_spi_dev_int = portduino_config.lora_spi_dev_int;
                    }
                }
            }
            if (yamlConfig["Lora"]["rfswitch_table"]) {
                portduino_config.has_rfswitch_table = true;
                portduino_config.rfswitch_table[0].mode = LR11x0::MODE_STBY;
                portduino_config.rfswitch_table[1].mode = LR11x0::MODE_RX;
                portduino_config.rfswitch_table[2].mode = LR11x0::MODE_TX;
                portduino_config.rfswitch_table[3].mode = LR11x0::MODE_TX_HP;
                portduino_config.rfswitch_table[4].mode = LR11x0::MODE_TX_HF;
                portduino_config.rfswitch_table[5].mode = LR11x0::MODE_GNSS;
                portduino_config.rfswitch_table[6].mode = LR11x0::MODE_WIFI;
                portduino_config.rfswitch_table[7] = END_OF_MODE_TABLE;

                for (int i = 0; i < 5; i++) {

                    // set up the pin array first
                    if (yamlConfig["Lora"]["rfswitch_table"]["pins"][i].as<std::string>("") == "DIO5")
                        portduino_config.rfswitch_dio_pins[i] = RADIOLIB_LR11X0_DIO5;
                    if (yamlConfig["Lora"]["rfswitch_table"]["pins"][i].as<std::string>("") == "DIO6")
                        portduino_config.rfswitch_dio_pins[i] = RADIOLIB_LR11X0_DIO6;
                    if (yamlConfig["Lora"]["rfswitch_table"]["pins"][i].as<std::string>("") == "DIO7")
                        portduino_config.rfswitch_dio_pins[i] = RADIOLIB_LR11X0_DIO7;
                    if (yamlConfig["Lora"]["rfswitch_table"]["pins"][i].as<std::string>("") == "DIO8")
                        portduino_config.rfswitch_dio_pins[i] = RADIOLIB_LR11X0_DIO8;
                    if (yamlConfig["Lora"]["rfswitch_table"]["pins"][i].as<std::string>("") == "DIO10")
                        portduino_config.rfswitch_dio_pins[i] = RADIOLIB_LR11X0_DIO10;

                    // now fill in the table
                    if (yamlConfig["Lora"]["rfswitch_table"]["MODE_STBY"][i].as<std::string>("") == "HIGH")
                        portduino_config.rfswitch_table[0].values[i] = HIGH;
                    if (yamlConfig["Lora"]["rfswitch_table"]["MODE_RX"][i].as<std::string>("") == "HIGH")
                        portduino_config.rfswitch_table[1].values[i] = HIGH;
                    if (yamlConfig["Lora"]["rfswitch_table"]["MODE_TX"][i].as<std::string>("") == "HIGH")
                        portduino_config.rfswitch_table[2].values[i] = HIGH;
                    if (yamlConfig["Lora"]["rfswitch_table"]["MODE_TX_HP"][i].as<std::string>("") == "HIGH")
                        portduino_config.rfswitch_table[3].values[i] = HIGH;
                    if (yamlConfig["Lora"]["rfswitch_table"]["MODE_TX_HF"][i].as<std::string>("") == "HIGH")
                        portduino_config.rfswitch_table[4].values[i] = HIGH;
                    if (yamlConfig["Lora"]["rfswitch_table"]["MODE_GNSS"][i].as<std::string>("") == "HIGH")
                        portduino_config.rfswitch_table[5].values[i] = HIGH;
                    if (yamlConfig["Lora"]["rfswitch_table"]["MODE_WIFI"][i].as<std::string>("") == "HIGH")
                        portduino_config.rfswitch_table[6].values[i] = HIGH;
                }
            }
        }
        readGPIOFromYaml(yamlConfig["GPIO"]["User"], portduino_config.userButtonPin);
        if (yamlConfig["GPS"]) {
            std::string serialPath = yamlConfig["GPS"]["SerialPath"].as<std::string>("");
            if (serialPath != "") {
                Serial1.setPath(serialPath);
                portduino_config.has_gps = 1;
            }
        }
        if (yamlConfig["I2C"]) {
            portduino_config.i2cdev = yamlConfig["I2C"]["I2CDevice"].as<std::string>("");
        }
        if (yamlConfig["Display"]) {

            for (auto &screen_name : portduino_config.screen_names) {
                if (yamlConfig["Display"]["Panel"].as<std::string>("") == screen_name.second)
                    portduino_config.displayPanel = screen_name.first;
            }
            portduino_config.displayHeight = yamlConfig["Display"]["Height"].as<int>(0);
            portduino_config.displayWidth = yamlConfig["Display"]["Width"].as<int>(0);

            readGPIOFromYaml(yamlConfig["Display"]["DC"], portduino_config.displayDC, -1);
            readGPIOFromYaml(yamlConfig["Display"]["CS"], portduino_config.displayCS, -1);
            readGPIOFromYaml(yamlConfig["Display"]["Backlight"], portduino_config.displayBacklight, -1);
            readGPIOFromYaml(yamlConfig["Display"]["BacklightPWMChannel"], portduino_config.displayBacklightPWMChannel, -1);
            readGPIOFromYaml(yamlConfig["Display"]["Reset"], portduino_config.displayReset, -1);

            portduino_config.displayBacklightInvert = yamlConfig["Display"]["BacklightInvert"].as<bool>(false);
            portduino_config.displayRGBOrder = yamlConfig["Display"]["RGBOrder"].as<bool>(false);
            portduino_config.displayOffsetX = yamlConfig["Display"]["OffsetX"].as<int>(0);
            portduino_config.displayOffsetY = yamlConfig["Display"]["OffsetY"].as<int>(0);
            portduino_config.displayRotate = yamlConfig["Display"]["Rotate"].as<bool>(false);
            portduino_config.displayOffsetRotate = yamlConfig["Display"]["OffsetRotate"].as<int>(1);
            portduino_config.displayInvert = yamlConfig["Display"]["Invert"].as<bool>(false);
            portduino_config.displayBusFrequency = yamlConfig["Display"]["BusFrequency"].as<int>(40000000);
            if (yamlConfig["Display"]["spidev"]) {
                portduino_config.display_spi_dev = "/dev/" + yamlConfig["Display"]["spidev"].as<std::string>("spidev0.1");
                if (portduino_config.display_spi_dev.length() == 14) {
                    int x = portduino_config.display_spi_dev.at(11) - '0';
                    int y = portduino_config.display_spi_dev.at(13) - '0';
                    if (x >= 0 && x < 10 && y >= 0 && y < 10) {
                        portduino_config.display_spi_dev_int = x + y << 4;
                        portduino_config.touchscreen_spi_dev_int = portduino_config.display_spi_dev_int;
                    }
                }
            }
        }
        if (yamlConfig["Touchscreen"]) {
            if (yamlConfig["Touchscreen"]["Module"].as<std::string>("") == "XPT2046")
                portduino_config.touchscreenModule = xpt2046;
            else if (yamlConfig["Touchscreen"]["Module"].as<std::string>("") == "STMPE610")
                portduino_config.touchscreenModule = stmpe610;
            else if (yamlConfig["Touchscreen"]["Module"].as<std::string>("") == "GT911")
                portduino_config.touchscreenModule = gt911;
            else if (yamlConfig["Touchscreen"]["Module"].as<std::string>("") == "FT5x06")
                portduino_config.touchscreenModule = ft5x06;

            readGPIOFromYaml(yamlConfig["Touchscreen"]["CS"], portduino_config.touchscreenCS, -1);
            readGPIOFromYaml(yamlConfig["Touchscreen"]["IRQ"], portduino_config.touchscreenIRQ, -1);

            portduino_config.touchscreenBusFrequency = yamlConfig["Touchscreen"]["BusFrequency"].as<int>(1000000);
            portduino_config.touchscreenRotate = yamlConfig["Touchscreen"]["Rotate"].as<int>(-1);
            portduino_config.touchscreenI2CAddr = yamlConfig["Touchscreen"]["I2CAddr"].as<int>(-1);
            if (yamlConfig["Touchscreen"]["spidev"]) {
                portduino_config.touchscreen_spi_dev = "/dev/" + yamlConfig["Touchscreen"]["spidev"].as<std::string>("");
                if (portduino_config.touchscreen_spi_dev.length() == 14) {
                    int x = portduino_config.touchscreen_spi_dev.at(11) - '0';
                    int y = portduino_config.touchscreen_spi_dev.at(13) - '0';
                    if (x >= 0 && x < 10 && y >= 0 && y < 10) {
                        portduino_config.touchscreen_spi_dev_int = x + y << 4;
                    }
                }
            }
        }
        if (yamlConfig["Input"]) {
            portduino_config.keyboardDevice = (yamlConfig["Input"]["KeyboardDevice"]).as<std::string>("");
            portduino_config.pointerDevice = (yamlConfig["Input"]["PointerDevice"]).as<std::string>("");

            readGPIOFromYaml(yamlConfig["Input"]["User"], portduino_config.userButtonPin);
            readGPIOFromYaml(yamlConfig["Input"]["TrackballUp"], portduino_config.tbUpPin);
            readGPIOFromYaml(yamlConfig["Input"]["TrackballDown"], portduino_config.tbDownPin);
            readGPIOFromYaml(yamlConfig["Input"]["TrackballLeft"], portduino_config.tbLeftPin);
            readGPIOFromYaml(yamlConfig["Input"]["TrackballRight"], portduino_config.tbRightPin);
            readGPIOFromYaml(yamlConfig["Input"]["TrackballPress"], portduino_config.tbPressPin);

            if (yamlConfig["Input"]["TrackballDirection"].as<std::string>("RISING") == "RISING") {
                portduino_config.tbDirection = 4;
            } else if (yamlConfig["Input"]["TrackballDirection"].as<std::string>("RISING") == "FALLING") {
                portduino_config.tbDirection = 3;
            }
        }

        if (yamlConfig["Webserver"]) {
            portduino_config.webserverport = (yamlConfig["Webserver"]["Port"]).as<int>(-1);
            portduino_config.webserver_root_path =
                (yamlConfig["Webserver"]["RootPath"]).as<std::string>("/usr/share/meshtasticd/web");
            portduino_config.webserver_ssl_key_path =
                (yamlConfig["Webserver"]["SSLKey"]).as<std::string>("/etc/meshtasticd/ssl/private_key.pem");
            portduino_config.webserver_ssl_cert_path =
                (yamlConfig["Webserver"]["SSLCert"]).as<std::string>("/etc/meshtasticd/ssl/certificate.pem");
        }

        if (yamlConfig["HostMetrics"]) {
            portduino_config.hostMetrics_channel = (yamlConfig["HostMetrics"]["Channel"]).as<int>(0);
            portduino_config.hostMetrics_interval = (yamlConfig["HostMetrics"]["ReportInterval"]).as<int>(0);
            portduino_config.hostMetrics_user_command = (yamlConfig["HostMetrics"]["UserStringCommand"]).as<std::string>("");
        }

        if (yamlConfig["Config"]) {
            if (yamlConfig["Config"]["DisplayMode"]) {
                portduino_config.has_configDisplayMode = true;
                if ((yamlConfig["Config"]["DisplayMode"]).as<std::string>("") == "TWOCOLOR") {
                    portduino_config.configDisplayMode = meshtastic_Config_DisplayConfig_DisplayMode_TWOCOLOR;
                } else if ((yamlConfig["Config"]["DisplayMode"]).as<std::string>("") == "INVERTED") {
                    portduino_config.configDisplayMode = meshtastic_Config_DisplayConfig_DisplayMode_INVERTED;
                } else if ((yamlConfig["Config"]["DisplayMode"]).as<std::string>("") == "COLOR") {
                    portduino_config.configDisplayMode = meshtastic_Config_DisplayConfig_DisplayMode_COLOR;
                } else {
                    portduino_config.configDisplayMode = meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT;
                }
            }
        }

        if (yamlConfig["General"]) {
            portduino_config.MaxNodes = (yamlConfig["General"]["MaxNodes"]).as<int>(200);
            portduino_config.maxtophone = (yamlConfig["General"]["MaxMessageQueue"]).as<int>(100);
            portduino_config.config_directory = (yamlConfig["General"]["ConfigDirectory"]).as<std::string>("");
            portduino_config.available_directory =
                (yamlConfig["General"]["AvailableDirectory"]).as<std::string>("/etc/meshtasticd/available.d/");
            if ((yamlConfig["General"]["MACAddress"]).as<std::string>("") != "" &&
                (yamlConfig["General"]["MACAddressSource"]).as<std::string>("") != "") {
                std::cout << "Cannot set both MACAddress and MACAddressSource!" << std::endl;
                exit(EXIT_FAILURE);
            }
            portduino_config.mac_address = (yamlConfig["General"]["MACAddress"]).as<std::string>("");
            if (portduino_config.mac_address != "") {
                portduino_config.mac_address_explicit = true;
            } else if ((yamlConfig["General"]["MACAddressSource"]).as<std::string>("") != "") {
                portduino_config.mac_address_source = (yamlConfig["General"]["MACAddressSource"]).as<std::string>("");
                std::ifstream infile("/sys/class/net/" + portduino_config.mac_address_source + "/address");
                std::getline(infile, portduino_config.mac_address);
            }

            // https://stackoverflow.com/a/20326454
            portduino_config.mac_address.erase(
                std::remove(portduino_config.mac_address.begin(), portduino_config.mac_address.end(), ':'),
                portduino_config.mac_address.end());
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
        dmac[0] = std::stoi(portduino_config.mac_address.substr(0, 2), nullptr, 16);
        dmac[1] = std::stoi(portduino_config.mac_address.substr(2, 2), nullptr, 16);
        dmac[2] = std::stoi(portduino_config.mac_address.substr(4, 2), nullptr, 16);
        dmac[3] = std::stoi(portduino_config.mac_address.substr(6, 2), nullptr, 16);
        dmac[4] = std::stoi(portduino_config.mac_address.substr(8, 2), nullptr, 16);
        dmac[5] = std::stoi(portduino_config.mac_address.substr(10, 2), nullptr, 16);
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

void readGPIOFromYaml(YAML::Node sourceNode, pinMapping &destPin, int pinDefault)
{
    if (sourceNode.IsMap()) {
        destPin.enabled = true;
        destPin.pin = sourceNode["pin"].as<int>(pinDefault);
        destPin.line = sourceNode["line"].as<int>(destPin.pin);
        destPin.gpiochip = sourceNode["gpiochip"].as<int>(portduino_config.lora_default_gpiochip);
    } else if (sourceNode) { // backwards API compatibility
        destPin.enabled = true;
        destPin.pin = sourceNode.as<int>(pinDefault);
        destPin.line = destPin.pin;
        destPin.gpiochip = portduino_config.lora_default_gpiochip;
    }
}