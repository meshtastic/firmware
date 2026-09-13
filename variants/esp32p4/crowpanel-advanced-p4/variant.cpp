#include "variant.h"
#include "Arduino.h"
#include <esp32-hal-periman.h>

extern "C" void initVariant(void)
{
#if defined(CONFIG_IDF_TARGET_ESP32P4) && defined(BOARD_SDMMC_POWER_PIN)
    // Ensure setLDOPower() exits early for this board family.
    // On ESP32-P4, Arduino SPI may attempt SDMMC on-chip LDO setup when SPI uses
    // pins that match SDMMC slot0 IOMUX pins (for example, GPIO 47/48 on p4-50).
    // Pre-tagging BOARD_SDMMC_POWER_PIN as "SDMMC POWER" short-circuits that path.
    if (perimanPinIsValid(BOARD_SDMMC_POWER_PIN)) {
        pinMode(BOARD_SDMMC_POWER_PIN, OUTPUT);
        digitalWrite(BOARD_SDMMC_POWER_PIN, BOARD_SDMMC_POWER_ON_LEVEL);

        if (perimanGetPinBusType(BOARD_SDMMC_POWER_PIN) != ESP32_BUS_TYPE_MAX &&
            perimanGetPinBusExtraType(BOARD_SDMMC_POWER_PIN) == nullptr) {
            perimanSetPinBusExtraType(BOARD_SDMMC_POWER_PIN, "");
        }
        perimanSetPinBusExtraType(BOARD_SDMMC_POWER_PIN, "SDMMC POWER");
    }
#endif
}
