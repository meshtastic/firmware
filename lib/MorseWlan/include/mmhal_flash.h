/*
 * Copyright 2021-2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @ingroup MMHAL Morse Micro Flash Hardware Abstraction Layer (mmhal_flash) API
 *
 * This API provides abstraction from the underlying flash hardware/.
 *
 * @{
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * This is the value erased flash bytes are set to. This shall be @c 0xFF as this is the
 * value that hardware flash erases to.
 */
#define MMHAL_FLASH_ERASE_VALUE 0xFF

/** LittleFS configuration structure. Include @c lfs.h for definition. */
struct lfs_config;

/**
 * Flash partition configuration structure
 *
 * This should be initialized using @c MMHAL_FLASH_PARTITION_CONFIG_DEFAULT.
 * For example:
 *
 * @code{.c}
 * struct mmhal_flash_partition_config partition = MMHAL_FLASH_PARTITION_CONFIG_DEFAULT;
 * @endcode
 */
struct mmhal_flash_partition_config {
    /** The start address of the partition, this may be a physical address
     *  or a relative address depending on implementation
     */
    uint32_t partition_start;

    /** The size of the partition */
    uint32_t partition_size;

    /**
     * If true, then the partition is not memory mapped and cannot be directly accessed
     * at the physical @c partition_start address
     */
    bool not_memory_mapped;
};

/** Initial values for @ref mmhal_flash_partition_config. */
#define MMHAL_FLASH_PARTITION_CONFIG_DEFAULT                                                                                     \
    {                                                                                                                            \
        0, 0, false                                                                                                              \
    }

/**
 * Get MMCONFIG flash partition configuration.
 *
 * MMCONFIG initialization is done by @c mmconfig_init() in @c mmconfig.c.
 * which in turn calls this function to fetch the partition configuration for config store
 * from the HAL layer. If config store is not supported by the platform then we just
 * return NULL. This function returns a static pointer to
 * @c struct @c mmhal_flash_partition_config.
 *
 * @return A static pointer to the partition config for MMCONFIG, or NULL if not supported.
 */
const struct mmhal_flash_partition_config *mmhal_get_mmconfig_partition(void);

/**
 * Erases a block of Flash pointed to by the block_address.
 *
 * The block address may be anywhere within the block to erase. The entire block gets erased.
 * Once erased all bytes in the block shall be @c MMHAL_FLASH_ERASE_VALUE (@c 0xFF).
 *
 * @param block_address The address of the block of Flash to erase.
 * @return              0 on success, negative number on failure
 */
int mmhal_flash_erase(uint32_t block_address);

/**
 * Returns the size of the Flash block at the specified address.
 *
 * @param block_address The address of the Flash block.
 * @return              The size of the Flash block in bytes.
 *                      Returns 0 if an invalid address is specified.
 */
uint32_t mmhal_flash_getblocksize(uint32_t block_address);

/**
 * Read a block of data from the specified Flash address into the buffer.
 *
 * @param read_address  The address in Flash to read from.
 * @param buf           The buffer to read into.
 * @param size          The number of bytes to read.
 * @return              0 on success, or a negative number on failure.
 */
int mmhal_flash_read(uint32_t read_address, uint8_t *buf, size_t size);

/**
 * Write a block of data to the specified Flash address.
 *
 * There is no alignment or minimum size requirement.  This function will
 * take care of aligning the data and merging with existing Flash contents.
 * The Flash block is not erased, it is up to the application to determine
 * if the block needs to be erased before programming.
 *
 * @param write_address The address in Flash to write to.
 * @param data          A pointer to the block of data to write.
 * @param size          The number of bytes to write.
 * @return              0 on success, or a negative number on failure.
 */
int mmhal_flash_write(uint32_t write_address, const uint8_t *data, size_t size);

/**
 * Get LittleFS configuration.
 *
 * LittleFS initialization is done by @c littlefs_init() in @c mmosal_shim_fileio.c.
 * which in turn calls this function to fetch the hardware configuration for LittleFS
 * from the HAL layer. The LittleFS configuration will vary from platform to platform.
 * If LittleFS is not supported by the platform then we just return NULL. This function
 * returns a static pointer to @c struct @c lfs_config which is defined in @c lfs.h.
 *
 * See @c mmhal_littlefs.c for the full HAL layer implementation for your platform.
 * See @c mmosal_shim_fileio.c for the @c libc shims for LittleFS.
 * See @c README.md in the @c src/littlefs folder for detailed information on LittleFS.
 *
 * @return A static pointer to the LittleFS config structure, or NULL if not supported.
 */
const struct lfs_config *mmhal_get_littlefs_config(void);

#ifdef __cplusplus
}
#endif

/** @} */
