#include "CryptoEngine.h"
#include "target_specific.h"
#include "PortduinoGPIO.h"
#include <Utility.h>
#include "sleep.h"

// FIXME - move getMacAddr/setBluetoothEnable into a HALPlatform class

uint32_t hwId; // fixme move into portduino

void getMacAddr(uint8_t *dmac)
{
    if (!hwId) {
        notImplemented("getMacAddr");
        hwId = random();
    }

    dmac[0] = 0x80;
    dmac[1] = 0;
    dmac[2] = 0;
    dmac[3] = hwId >> 16;
    dmac[4] = hwId >> 8;
    dmac[5] = hwId & 0xff;
}

void setBluetoothEnable(bool on)
{
    notImplemented("setBluetoothEnable");
}

void cpuDeepSleep(uint64_t msecs) {
    notImplemented("cpuDeepSleep");
}

// FIXME - implement real crypto for linux
CryptoEngine *crypto = new CryptoEngine();

void updateBatteryLevel(uint8_t level) NOT_IMPLEMENTED("updateBatteryLevel");



/** apps run under portduino can optionally define a portduinoSetup() to 
 * use portduino specific init code (such as gpioBind) to setup portduino on their host machine,
 * before running 'arduino' code.
 */
void  portduinoSetup() {
  printf("Setting up Meshtastic on Porduino...\n");
  gpioBind((new SimGPIOPin(LORA_DIO0, "LORA_DIO0")));
  gpioBind((new SimGPIOPin(LORA_RESET, "LORA_RESET")));
  gpioBind((new SimGPIOPin(RF95_NSS, "RF95_NSS"))->setSilent());
}
