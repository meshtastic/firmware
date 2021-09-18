#include "CryptoEngine.h"
#include "PortduinoGPIO.h"
#include "SPIChip.h"
#include "mesh/RF95Interface.h"
#include "sleep.h"
#include "target_specific.h"

#include <Utility.h>
#include <assert.h>
#include <linux/gpio/LinuxGPIOPin.h>

// FIXME - move setBluetoothEnable into a HALPlatform class

void setBluetoothEnable(bool on)
{
    // not needed
}

void cpuDeepSleep(uint64_t msecs)
{
    notImplemented("cpuDeepSleep");
}

void updateBatteryLevel(uint8_t level) NOT_IMPLEMENTED("updateBatteryLevel");


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

/** apps run under portduino can optionally define a portduinoSetup() to
 * use portduino specific init code (such as gpioBind) to setup portduino on their host machine,
 * before running 'arduino' code.
 */
void portduinoSetup()
{
    printf("Setting up Meshtastic on Porduino...\n");

#ifdef PORTDUINO_LINUX_HARDWARE
    SPI.begin(); // We need to create SPI 
    bool usePineLora = !spiChip->isSimulated();
    if(usePineLora) {
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
    }
    else 
#endif

    {
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
}
