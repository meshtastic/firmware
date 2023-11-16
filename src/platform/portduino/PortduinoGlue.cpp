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
#include "pigpio.h"
#include "yaml-cpp/yaml.h"
#include <iostream>
#include <map>

std::map<int, int> settingsMap;

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
    YAML::Node yamlConfig;
    try {
        yamlConfig = YAML::LoadFile("config.yaml");
    } catch (YAML::Exception e) {
        std::cout << "*** Exception " << e.what() << std::endl;
    }
    if (yamlConfig.IsNull()) {
        std::cout << "Looking in /etc/meshtastic/" << std::endl;
        try {
            yamlConfig = YAML::LoadFile("/etc/meshtastic/config.yaml");
        } catch (YAML::Exception e) {
            std::cout << "*** Exception " << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    try {
        settingsMap[use_sx1262] = yamlConfig["USE_SX1262"].as<bool>(false);
        settingsMap[sx126x_dio2_as_rf_switch] = yamlConfig["SX126X_DIO2_AS_RF_SWITCH"].as<bool>(false);
        settingsMap[sx126x_cs] = yamlConfig["SX126X_CS"].as<int>(RADIOLIB_NC);
        settingsMap[sx126x_dio1] = yamlConfig["SX126X_DIO1"].as<int>(RADIOLIB_NC);
        settingsMap[sx126x_busy] = yamlConfig["SX126X_BUSY"].as<int>(RADIOLIB_NC);
        settingsMap[sx126x_reset] = yamlConfig["SX126X_RESET"].as<int>(RADIOLIB_NC);
        settingsMap[use_rf95] = yamlConfig["USE_RF95"].as<bool>(false);
        settingsMap[rf95_nss] = yamlConfig["RF95_NSS"].as<int>(RADIOLIB_NC);
        settingsMap[rf95_irq] = yamlConfig["RF95_IRQ"].as<int>(RADIOLIB_NC);
        settingsMap[rf95_reset] = yamlConfig["RF95_RESET"].as<int>(RADIOLIB_NC);
        settingsMap[rf95_dio1] = yamlConfig["RF95_DIO1"].as<int>(RADIOLIB_NC);

    } catch (YAML::Exception e) {
        std::cout << "*** Exception " << e.what() << std::endl;
        exit(EXIT_FAILURE);
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
    // gpioBind((new SimGPIOPin(RF95_NSS, "RF95_NSS"))->setSilent());
#endif
}