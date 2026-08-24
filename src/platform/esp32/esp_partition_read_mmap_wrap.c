// Workaround for the IDF 5.5 manual esp_flash read regression on t-watch-ultra.
//
// On this board (Winbond W25Q128JW, ef:8018), the IDF 5.5 *direct* flash read path
// (esp_flash_read / esp_partition_read) returns 0x00 for data that is physically
// correct on flash. We proved the *memory-mapped* (cache) read returns the right
// data, writes work, and it's not read-mode/HPM/timing-tuning/PSRAM. So route every
// esp_partition_read through esp_partition_mmap + memcpy, which uses the working
// cache path. Activated by `-Wl,--wrap=esp_partition_read` (t-watch-ultra only).
//
// That alone isn't enough: nvs_flash reads through the lower-level esp_flash_read,
// which hits the same 0x00 regression, so NVS came up empty every boot (0 entries)
// and silently dropped BLE bonds and everything else stored via Preferences. Wrap
// esp_flash_read too, via raw spi_flash_mmap, for callers that bypass esp_partition_t.
#if defined(T_WATCH_ULTRA)

#include "esp_flash.h"
#include "esp_partition.h"
#include "spi_flash_mmap.h"
#include <string.h>

extern esp_err_t __real_esp_partition_read(const esp_partition_t *partition, size_t src_offset, void *dst, size_t size);
extern esp_err_t __real_esp_flash_read(esp_flash_t *chip, void *buffer, uint32_t address, uint32_t length);

esp_err_t __wrap_esp_partition_read(const esp_partition_t *partition, size_t src_offset, void *dst, size_t size)
{
    if (partition == NULL || dst == NULL)
        return ESP_ERR_INVALID_ARG;
    if (size == 0)
        return ESP_OK;

    // mmap requires a 64KB-aligned start; map the containing page span and copy
    // out from the requested offset.
    const size_t PAGE = 0x10000;
    size_t aligned = src_offset & ~(PAGE - 1);
    size_t delta = src_offset - aligned;

    const void *ptr = NULL;
    esp_partition_mmap_handle_t handle;
    esp_err_t err = esp_partition_mmap(partition, aligned, delta + size, ESP_PARTITION_MMAP_DATA, &ptr, &handle);
    if (err != ESP_OK) {
        // Encrypted partitions / regions mmap can't serve: fall back to the real
        // read (may be wrong on this board, but better than failing the call).
        return __real_esp_partition_read(partition, src_offset, dst, size);
    }
    memcpy(dst, (const uint8_t *)ptr + delta, size);
    esp_partition_munmap(handle);
    return ESP_OK;
}

esp_err_t __wrap_esp_flash_read(esp_flash_t *chip, void *buffer, uint32_t address, uint32_t length)
{
    if (buffer == NULL)
        return ESP_ERR_INVALID_ARG;
    if (length == 0)
        return ESP_OK;
    // spi_flash_mmap only maps the default (main) chip's address space - anything
    // else (e.g. a second chip on another bus) falls back to the real call.
    if (chip != NULL && chip != esp_flash_default_chip)
        return __real_esp_flash_read(chip, buffer, address, length);

    const size_t PAGE = 0x10000;
    size_t aligned = address & ~(PAGE - 1);
    size_t delta = address - aligned;

    const void *ptr = NULL;
    spi_flash_mmap_handle_t handle;
    esp_err_t err = spi_flash_mmap(aligned, delta + length, SPI_FLASH_MMAP_DATA, &ptr, &handle);
    if (err != ESP_OK)
        return __real_esp_flash_read(chip, buffer, address, length);

    memcpy(buffer, (const uint8_t *)ptr + delta, length);
    spi_flash_munmap(handle);
    return ESP_OK;
}

#endif // T_WATCH_ULTRA
