#include "CryptoEngine.h"
#include "PortduinoGPIO.h"
#include "mesh/RF95Interface.h"
#include "sleep.h"
#include "target_specific.h"

#include <Utility.h>
#include <assert.h>

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

/** Dear pinetab hardware geeks!
 *
 * The current pinetab lora module has a slight bug.  The ch341 part only provides ISR assertions on edges.
 * This makes sense because USB interrupts happen through fast/repeated special irq urbs that are constantly
 * chattering on the USB bus.
 *
 * But this isn't sufficient for level triggered ISR sources like the sx127x radios.  The common way that seems to
 * be addressed by cs341 users is to **always** connect the INT# (pin 26 on the ch341f) signal to one of the GPIO signals
 * on the part.  I'd recommend connecting that LORA_DIO0/INT# line to pin 19 (data 4) on the pinetab board.  This would
 * provide an efficent mechanism so that the (kernel) code in the cs341 driver that I've slightly hacked up to see the
 * current state of LORA_DIO0.  Without that access, I can't know if the interrupt is still pending - which would create
 * race conditions in packet handling.
 *
 * My workaround is to poll the status register internally to the sx127x.  Which is expensive because it involves a number of
 * i2c transactions and many trips back and forth between kernel and my userspace app.  I think shipping the current version
 * of the pinetab lora device would be fine because I can poll slowly (because lora is slow).  But if you ever have cause to
 * rev this board.  I highly encourage this small change.
 *
 * Btw - your little "USB lora dongle" is really neat.  I encourage you to sell it, because even non pinetab customers could
 * use it to easily add lora to rasberry pi, desktop pcs etc...
 *
 * Porduino helper class to do this i2c based polling:
 */
class R595PolledIrqPin : public GPIOPin
{
  public:
    R595PolledIrqPin() : GPIOPin(LORA_DIO0, "LORA_DIO0") {}

    /// Read the low level hardware for this pin
    virtual PinStatus readPinHardware()
    {
        if (isrPinStatus < 0)
            return LOW; // No interrupt handler attached, don't bother polling i2c right now
        else {
            extern RadioInterface *rIf; // FIXME, temporary hack until we know if we need to keep this

            assert(rIf);
            RF95Interface *rIf95 = static_cast<RF95Interface *>(rIf);
            bool p = rIf95->isIRQPending();
            // log(SysGPIO, LogDebug, "R595PolledIrqPin::readPinHardware(%s, %d, %d)", getName(), getPinNum(), p);
            return p ? HIGH : LOW;
        }
    }
};

/** apps run under portduino can optionally define a portduinoSetup() to
 * use portduino specific init code (such as gpioBind) to setup portduino on their host machine,
 * before running 'arduino' code.
 */
void portduinoSetup()
{
    printf("Setting up Meshtastic on Porduino...\n");

    // FIXME: disable while not testing with real hardware
    // gpioBind(new R595PolledIrqPin());

    // gpioBind((new SimGPIOPin(LORA_RESET, "LORA_RESET")));
    // gpioBind((new SimGPIOPin(RF95_NSS, "RF95_NSS"))->setSilent());
}
