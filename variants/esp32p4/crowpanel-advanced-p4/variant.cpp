#include "variant.h"
#include "Arduino.h"
#include <esp32-hal-periman.h>

void earlyInitVariant()
{
#if defined(CONFIG_IDF_TARGET_ESP32P4) && defined(BOARD_SDMMC_POWER_PIN)
    // Work around Arduino-ESP32 P4 SPI null-deref in setLDOPower() when this pin
    // already has a bus type but no extra type string assigned.
    if (perimanPinIsValid(BOARD_SDMMC_POWER_PIN) && perimanGetPinBusType(BOARD_SDMMC_POWER_PIN) != ESP32_BUS_TYPE_MAX &&
        perimanGetPinBusExtraType(BOARD_SDMMC_POWER_PIN) == nullptr) {
        perimanSetPinBusExtraType(BOARD_SDMMC_POWER_PIN, "");
    }
#endif
}