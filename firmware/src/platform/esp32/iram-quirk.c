// Free up some precious space in the iram0_0_seg memory segment

#include <stdint.h>

#include <esp_attr.h>
#include <esp_flash.h>
#include <spi_flash_chip_driver.h>

#define IRAM_SECTION section(".iram1.stub")

IRAM_ATTR esp_err_t stub_probe(esp_flash_t *chip, uint32_t flash_id)
{
    return ESP_ERR_NOT_FOUND;
}

const spi_flash_chip_t stub_flash_chip __attribute__((IRAM_SECTION)) = {
    .name = "stub",
    .probe = stub_probe,
};

extern const spi_flash_chip_t __wrap_esp_flash_chip_gd __attribute__((IRAM_SECTION, alias("stub_flash_chip")));
extern const spi_flash_chip_t __wrap_esp_flash_chip_issi __attribute__((IRAM_SECTION, alias("stub_flash_chip")));
extern const spi_flash_chip_t __wrap_esp_flash_chip_winbond __attribute__((IRAM_SECTION, alias("stub_flash_chip")));
