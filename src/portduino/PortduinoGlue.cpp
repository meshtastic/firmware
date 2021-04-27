#include "CryptoEngine.h"
#include "PortduinoGPIO.h"
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


GPIOPin *loraIrq;

/** apps run under portduino can optionally define a portduinoSetup() to
 * use portduino specific init code (such as gpioBind) to setup portduino on their host machine,
 * before running 'arduino' code.
 */
void portduinoSetup()
{
    printf("Setting up Meshtastic on Porduino...\n");

    // FIXME: remove this hack once interrupts are confirmed to work on new pine64 board
    loraIrq = new LinuxGPIOPin(LORA_DIO1, "ch341", "int", "loraIrq"); // or "err"?
    gpioBind(loraIrq);

    // BUSY hw was busted on current board - just use the simulated pin (which will read low)
    gpioBind(new LinuxGPIOPin(SX1262_BUSY, "ch341", "slct", "loraBusy"));
    // auto fakeBusy = new SimGPIOPin(SX1262_BUSY, "fakeBusy");
    // fakeBusy->writePin(LOW);
    // fakeBusy->setSilent(true);
    // gpioBind(fakeBusy);

    auto loraCs = new LinuxGPIOPin(SX1262_CS, "ch341", "cs0", "loraCs");
    loraCs->setSilent(true);
    gpioBind(loraCs);

    // gpioBind((new SimGPIOPin(LORA_RESET, "LORA_RESET")));
    // gpioBind((new SimGPIOPin(RF95_NSS, "RF95_NSS"))->setSilent());
}
