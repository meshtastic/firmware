#include "CryptoEngine.h"
#include "PortduinoGPIO.h"
#include "SPIChip.h"
#include "mesh/RF95Interface.h"
#include "sleep.h"
#include "target_specific.h"

#include <Utility.h>
#include <assert.h>

#ifdef ARCH_RASPBERRY_PI
#include "PortduinoGlue.h"
#include "linux/gpio/LinuxGPIOPin.h"
#include "pigpio.h"
#include "yaml-cpp/yaml.h"
#include <iostream>
#include <map>
#include <unistd.h>

std::map<configNames, int> settingsMap;
std::map<configNames, std::string> settingsStrings;

#else
#include <linux/gpio/LinuxGPIOPin.h>
#endif

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
#ifndef ARCH_RASPBERRY_PI
/** a simulated pin for busted IRQ hardware
 * Porduino helper class to do this i2c based polling:
 */
class PolledIrqPin : public GPIOPin
{
  public:
    PolledIrqPin() : GPIOPin(LORA_DIO1, "loraIRQ") {}

    /// Read the low level hardware for this pin
    virtual PinStatus readPinHardware()
    {
        if (isrPinStatus < 0)
            return LOW; // No interrupt handler attached, don't bother polling i2c right now
        else {
            extern RadioInterface *rIf; // FIXME, temporary hack until we know if we need to keep this

            assert(rIf);
            RadioLibInterface *rIf95 = static_cast<RadioLibInterface *>(rIf);
            bool p = rIf95->isIRQPending();
            log(SysGPIO, LogDebug, "PolledIrqPin::readPinHardware(%s, %d, %d)", getName(), getPinNum(), p);
            return p ? HIGH : LOW;
        }
    }
};

static GPIOPin *loraIrq;
#endif
int TCPPort = 4403;

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    switch (key) {
    case 'p':
        if (sscanf(arg, "%d", &TCPPort) < 1)
            return ARGP_ERR_UNKNOWN;
        else
            printf("Using TCP port %d\n", TCPPort);
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
    static struct argp_option options[] = {{"port", 'p', "PORT", 0, "The TCP port to use."}, {0}};
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

#ifdef ARCH_RASPBERRY_PI
    gpioInit();

    std::string gpioChipName = "gpiochip";
    YAML::Node yamlConfig;

    if (access("config.yaml", R_OK) == 0) {
        try {
            yamlConfig = YAML::LoadFile("config.yaml");
        } catch (YAML::Exception e) {
            std::cout << "*** Exception " << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
    } else if (access("/etc/meshtasticd/config.yaml", R_OK) == 0) {
        try {
            yamlConfig = YAML::LoadFile("/etc/meshtasticd/config.yaml");
        } catch (YAML::Exception e) {
            std::cout << "*** Exception " << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
    } else {
        std::cout << "No 'config.yaml' found, exiting." << std::endl;
        exit(EXIT_FAILURE);
    }

    try {
        if (yamlConfig["Lora"]) {
            settingsMap[use_sx1262] = false;
            settingsMap[use_rf95] = false;

            if (yamlConfig["Lora"]["Module"] && yamlConfig["Lora"]["Module"].as<std::string>("") == "sx1262") {
                settingsMap[use_sx1262] = true;
            } else if (yamlConfig["Lora"]["Module"] && yamlConfig["Lora"]["Module"].as<std::string>("") == "RF95") {
                settingsMap[use_rf95] = true;
            }
            settingsMap[dio2_as_rf_switch] = yamlConfig["Lora"]["DIO2_AS_RF_SWITCH"].as<bool>(false);
            settingsMap[cs] = yamlConfig["Lora"]["CS"].as<int>(RADIOLIB_NC);
            settingsMap[irq] = yamlConfig["Lora"]["IRQ"].as<int>(RADIOLIB_NC);
            settingsMap[busy] = yamlConfig["Lora"]["Busy"].as<int>(RADIOLIB_NC);
            settingsMap[reset] = yamlConfig["Lora"]["Reset"].as<int>(RADIOLIB_NC);
            settingsMap[gpiochip] = yamlConfig["Lora"]["gpiochip"].as<int>(0);
            gpioChipName += std::to_string(settingsMap[gpiochip]);
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
        settingsMap[displayPanel] = no_screen;
        if (yamlConfig["Display"]) {
            if (yamlConfig["Display"]["Panel"].as<std::string>("") == "ST7789")
                settingsMap[displayPanel] = st7789;
            settingsMap[displayHeight] = yamlConfig["Display"]["Height"].as<int>(0);
            settingsMap[displayWidth] = yamlConfig["Display"]["Width"].as<int>(0);
            settingsMap[displayDC] = yamlConfig["Display"]["DC"].as<int>(-1);
            settingsMap[displayCS] = yamlConfig["Display"]["CS"].as<int>(-1);
            settingsMap[displayBacklight] = yamlConfig["Display"]["Backlight"].as<int>(-1);
            settingsMap[displayReset] = yamlConfig["Display"]["Reset"].as<int>(-1);
            settingsMap[displayRotate] = yamlConfig["Display"]["Rotate"].as<bool>(false);
        }
        settingsMap[touchscreenModule] = no_touchscreen;
        if (yamlConfig["Touchscreen"]) {
            if (yamlConfig["Touchscreen"]["Module"].as<std::string>("") == "XPT2046")
                settingsMap[touchscreenModule] = xpt2046;
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
    if (access("/sys/kernel/debug/bluetooth/hci0/identity", R_OK) != 0) {
        std::cout << "Cannot read Bluetooth MAC Address. Please run as root" << std::endl;
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
#endif

#ifdef defined(PORTDUINO_LINUX_HARDWARE)
    SPI.begin(); // We need to create SPI
    bool usePineLora = !spiChip->isSimulated();
    if (usePineLora) {
        printf("Connecting to PineLora board...\n");

        // FIXME: remove this hack once interrupts are confirmed to work on new pine64 board
        // loraIrq = new PolledIrqPin();
        loraIrq = new LinuxGPIOPin(LORA_DIO1, "ch341", "int", "loraIrq"); // or "err"?
        loraIrq->setSilent();
        gpioBind(loraIrq);

        // BUSY hw was busted on current board - just use the simulated pin (which will read low)
        auto busy = new LinuxGPIOPin(SX126X_BUSY, "ch341", "slct", "loraBusy");
        busy->setSilent();
        gpioBind(busy);

        gpioBind(new LinuxGPIOPin(SX126X_RESET, "ch341", "ini", "loraReset"));

        auto loraCs = new LinuxGPIOPin(SX126X_CS, "ch341", "cs0", "loraCs");
        loraCs->setSilent();
        gpioBind(loraCs);
    } else
#endif
#ifndef ARCH_RASPBERRY_PI
    {
        // Set the random seed equal to TCPPort to have a different seed per instance
        randomSeed(TCPPort);

        auto fakeBusy = new SimGPIOPin(SX126X_BUSY, "fakeBusy");
        fakeBusy->writePin(LOW);
        fakeBusy->setSilent(true);
        gpioBind(fakeBusy);

        auto cs = new SimGPIOPin(SX126X_CS, "fakeLoraCS");
        cs->setSilent(true);
        gpioBind(cs);

        gpioBind(new SimGPIOPin(SX126X_RESET, "fakeLoraReset"));
        gpioBind(new SimGPIOPin(LORA_DIO1, "fakeLoraIrq"));
    }
    // gpioBind((new SimGPIOPin(LORA_RESET, "LORA_RESET")));
    // gpioBind((new SimGPIOPin(LORA_CS, "LORA_CS"))->setSilent());
#endif
}

#ifdef ARCH_RASPBERRY_PI
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
#endif