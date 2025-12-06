/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 hathach for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "LittleFS.h"
#include "stm32wlxx_hal_flash.h"

/**********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/
/** This macro is used to suppress compiler messages about a parameter not being used in a function. */
#define LFS_UNUSED(p) (void)((p))

#define STM32WL_PAGE_SIZE (FLASH_PAGE_SIZE)
#define STM32WL_PAGE_COUNT (FLASH_PAGE_NB)
#define STM32WL_FLASH_BASE (FLASH_BASE)

/*
 * FLASH_SIZE from stm32wle5xx.h will read the actual FLASH size from the chip.
 * FLASH_END_ADDR is calculated from FLASH_SIZE.
 * Use the last 28 KiB of the FLASH
 */
#define LFS_FLASH_TOTAL_SIZE (14 * 2048) /* needs to be a multiple of LFS_BLOCK_SIZE */
#define LFS_BLOCK_SIZE (2048)
#define LFS_FLASH_ADDR_END (FLASH_END_ADDR)
#define LFS_FLASH_ADDR_BASE (LFS_FLASH_ADDR_END - LFS_FLASH_TOTAL_SIZE + 1)

#if !CFG_DEBUG
#define _LFS_DBG(fmt, ...)
#else
#define _LFS_DBG(fmt, ...) printf("%s:%d (%s): " fmt "\n", __FILE__, __LINE__, __func__, __VA_ARGS__)
#endif

//--------------------------------------------------------------------+
// LFS Disk IO
//--------------------------------------------------------------------+

static int _internal_flash_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
    LFS_UNUSED(c);

    if (!buffer || !size) {
        _LFS_DBG("%s Invalid parameter!\r\n", __func__);
        return LFS_ERR_INVAL;
    }

    lfs_block_t address = LFS_FLASH_ADDR_BASE + (block * STM32WL_PAGE_SIZE + off);

    memcpy(buffer, (void *)address, size);

    return LFS_ERR_OK;
}

// Program a region in a block. The block must have previously
// been erased. Negative error codes are propogated to the user.
// May return LFS_ERR_CORRUPT if the block should be considered bad.
static int _internal_flash_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    lfs_block_t address = LFS_FLASH_ADDR_BASE + (block * STM32WL_PAGE_SIZE + off);
    HAL_StatusTypeDef hal_rc = HAL_OK;
    uint32_t dw_count = size / 8;
    uint64_t *bufp = (uint64_t *)buffer;

    LFS_UNUSED(c);

    _LFS_DBG("Programming %d bytes/%d doublewords at address 0x%08x/block %d, offset %d.", size, dw_count, address, block, off);
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return LFS_ERR_IO;
    }
    for (uint32_t i = 0; i < dw_count; i++) {
        if ((address < LFS_FLASH_ADDR_BASE) || (address > LFS_FLASH_ADDR_END)) {
            _LFS_DBG("Wanted to program out of bound of FLASH: 0x%08x.\n", address);
            HAL_FLASH_Lock();
            return LFS_ERR_INVAL;
        }
        hal_rc = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, *bufp);
        if (hal_rc != HAL_OK) {
            /* Error occurred while writing data in Flash memory.
             * User can add here some code to deal with this error.
             */
            _LFS_DBG("Program error at (0x%08x), 0x%X, error: 0x%08x\n", address, hal_rc, HAL_FLASH_GetError());
        }
        address += 8;
        bufp += 1;
    }
    if (HAL_FLASH_Lock() != HAL_OK) {
        return LFS_ERR_IO;
    }

    return hal_rc == HAL_OK ? LFS_ERR_OK : LFS_ERR_IO; // If HAL_OK, return LFS_ERR_OK, else return LFS_ERR_IO
}

// Erase a block. A block must be erased before being programmed.
// The state of an erased block is undefined. Negative error codes
// are propogated to the user.
// May return LFS_ERR_CORRUPT if the block should be considered bad.
static int _internal_flash_erase(const struct lfs_config *c, lfs_block_t block)
{
    lfs_block_t address = LFS_FLASH_ADDR_BASE + (block * STM32WL_PAGE_SIZE);
    HAL_StatusTypeDef hal_rc;
    FLASH_EraseInitTypeDef EraseInitStruct = {.TypeErase = FLASH_TYPEERASE_PAGES, .Page = 0, .NbPages = 1};
    uint32_t PAGEError = 0;

    LFS_UNUSED(c);

    if ((address < LFS_FLASH_ADDR_BASE) || (address > LFS_FLASH_ADDR_END)) {
        _LFS_DBG("Wanted to erase out of bound of FLASH: 0x%08x.\n", address);
        return LFS_ERR_INVAL;
    }
    /* calculate the absolute page, i.e. what the ST wants */
    EraseInitStruct.Page = (address - STM32WL_FLASH_BASE) / STM32WL_PAGE_SIZE;
    _LFS_DBG("Erasing block %d at 0x%08x... ", block, address);
    HAL_FLASH_Unlock();
    hal_rc = HAL_FLASHEx_Erase(&EraseInitStruct, &PAGEError);
    HAL_FLASH_Lock();

    return hal_rc == HAL_OK ? LFS_ERR_OK : LFS_ERR_IO; // If HAL_OK, return LFS_ERR_OK, else return LFS_ERR_IO
}

// Sync the state of the underlying block device. Negative error codes
// are propogated to the user.
static int _internal_flash_sync(const struct lfs_config *c)
{
    LFS_UNUSED(c);
    // write function performs no caching.  No need for sync.

    return LFS_ERR_OK;
}

static struct lfs_config _InternalFSConfig = {.context = NULL,

                                              .read = _internal_flash_read,
                                              .prog = _internal_flash_prog,
                                              .erase = _internal_flash_erase,
                                              .sync = _internal_flash_sync,

                                              .read_size = LFS_BLOCK_SIZE,
                                              .prog_size = LFS_BLOCK_SIZE,
                                              .block_size = LFS_BLOCK_SIZE,
                                              .block_count = LFS_FLASH_TOTAL_SIZE / LFS_BLOCK_SIZE,
                                              .lookahead = 128,

                                              .read_buffer = NULL,
                                              .prog_buffer = NULL,
                                              .lookahead_buffer = NULL,
                                              .file_buffer = NULL};

LittleFS InternalFS;

//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+

LittleFS::LittleFS(void) : STM32_LittleFS(&_InternalFSConfig) {}

bool LittleFS::begin(void)
{
    if (FLASH_BASE >= LFS_FLASH_ADDR_BASE) {
        /* There is not enough space on this device for a filesystem. */
        return false;
    }
    // failed to mount, erase all pages then format and mount again
    if (!STM32_LittleFS::begin()) {
        // Erase all pages of internal flash region for Filesystem.
        for (uint32_t addr = LFS_FLASH_ADDR_BASE; addr < (LFS_FLASH_ADDR_END + 1); addr += STM32WL_PAGE_SIZE) {
            _internal_flash_erase(&_InternalFSConfig, (addr - LFS_FLASH_ADDR_BASE) / STM32WL_PAGE_SIZE);
        }

        // lfs format
        this->format();

        // mount again if still failed, give up
        if (!STM32_LittleFS::begin())
            return false;
    }

    return true;
}
