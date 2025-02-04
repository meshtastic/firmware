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

/**********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/
/** This macro is used to suppress compiler messages about a parameter not being used in a function. */
#define PARAMETER_NOT_USED(p) (void)((p))

#define STM32WL_SECTOR_SIZE 0x800 /* 2K */
#define STM32WL_SECTOR_COUNT 14

#define LFS_FLASH_TOTAL_SIZE (STM32WL_SECTOR_COUNT * STM32WL_SECTOR_SIZE)
#define LFS_BLOCK_SIZE 128

#define LFS_FLASH_ADDR (262144 - LFS_FLASH_TOTAL_SIZE)

//--------------------------------------------------------------------+
// LFS Disk IO
//--------------------------------------------------------------------+

static inline uint32_t lba2addr(uint32_t block)
{
    return ((uint32_t)LFS_FLASH_ADDR) + block * LFS_BLOCK_SIZE;
}

static int _internal_flash_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
    PARAMETER_NOT_USED(c);
    if (!buffer || !size) {
        printf("%s Invalid parameter!\r\n", __func__);
        return LFS_ERR_INVAL;
    }

    lfs_block_t address = LFS_FLASH_ADDR + (block * STM32WL_SECTOR_SIZE + off);
    // printf("+%s(Addr 0x%06lX, Len 0x%04lX)\r\n",__func__,address,size);
    // hexdump((void *)address,size);
    memcpy(buffer, (void *)address, size);

    return LFS_ERR_OK;
}

// Program a region in a block. The block must have previously
// been erased. Negative error codes are propogated to the user.
// May return LFS_ERR_CORRUPT if the block should be considered bad.
static int _internal_flash_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    // (void) c;

    // uint32_t addr = lba2addr(block) + off;
    // VERIFY( flash_nrf5x_write(addr, buffer, size), -1)

    // return 0;
    PARAMETER_NOT_USED(c);
    lfs_block_t address = LFS_FLASH_ADDR + (block * STM32WL_SECTOR_SIZE + off);
    HAL_StatusTypeDef hal_rc = HAL_OK;
    uint32_t block_count = size / 8;

    // printf("+%s(Addr 0x%06lX, Len 0x%04lX)\r\n",__func__,address,size);
    // hexdump((void *)address,size);
    /* Program the user Flash area word by word
    (area defined by FLASH_USER_START_ADDR and FLASH_USER_END_ADDR) ***********/

    uint64_t data_source;

    for (uint32_t i = 0; i < block_count; i++) {
        memcpy(&data_source, buffer, 8); // load the 64-bit source from the buffer
        hal_rc = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, data_source);
        if (hal_rc == HAL_OK) {
            address += 8;
            buffer = (uint8_t *)buffer + 8;
        } else {
            /* Error occurred while writing data in Flash memory.
                   User can add here some code to deal with this error */
            printf("Program Error, 0x%X\n", hal_rc);

        } // else
    }     // for
    // printf("-%s\n",__func__);

    return hal_rc == HAL_OK ? LFS_ERR_OK : LFS_ERR_IO; // If HAL_OK, return LFS_ERR_OK, else return LFS_ERR_IO
}

// Erase a block. A block must be erased before being programmed.
// The state of an erased block is undefined. Negative error codes
// are propogated to the user.
// May return LFS_ERR_CORRUPT if the block should be considered bad.
static int _internal_flash_erase(const struct lfs_config *c, lfs_block_t block)
{
    // (void) c;

    // uint32_t addr = lba2addr(block);

    // // implement as write 0xff to whole block address
    // for(int i=0; i <LFS_BLOCK_SIZE; i++)
    // {
    //   flash_nrf5x_write8(addr + i, 0xFF);
    // }

    // // flash_nrf5x_flush();

    // return 0;
    PARAMETER_NOT_USED(c);
    lfs_block_t address = LFS_FLASH_ADDR + (block * STM32WL_SECTOR_SIZE);
    // printf("+%s(Addr 0x%06lX)\r\n",__func__,address);

    HAL_StatusTypeDef hal_rc;
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t PAGEError = 0;

    /* Fill EraseInit structure*/
    EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
    EraseInitStruct.Page = (address - FLASH_BASE) / STM32WL_SECTOR_SIZE;
    EraseInitStruct.NbPages = 1;
    hal_rc = HAL_FLASHEx_Erase(&EraseInitStruct, &PAGEError);
    //	if (hal_rc != HAL_OK)
    //	{
    //		printf("%s ERROR 0x%X\n",__func__,hal_rc);
    //	}
    //	else
    //		printf("%s SUCCESS\n",__func__);

    return hal_rc == HAL_OK ? LFS_ERR_OK : LFS_ERR_IO; // If HAL_OK, return LFS_ERR_OK, else return LFS_ERR_IO
}

// Sync the state of the underlying block device. Negative error codes
// are propogated to the user.
static int _internal_flash_sync(const struct lfs_config *c)
{
    // (void) c;
    // flash_nrf5x_flush();
    // return 0;
    PARAMETER_NOT_USED(c);
    // write function performs no caching.  No need for sync.
    // printf("+%s()\r\n",__func__);
    return LFS_ERR_OK;
    // return LFS_ERR_IO;
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
    // failed to mount, erase all sector then format and mount again
    if (!STM32_LittleFS::begin()) {
        // Erase all sectors of internal flash region for Filesystem.
        for (uint32_t addr = LFS_FLASH_ADDR; addr < LFS_FLASH_ADDR + LFS_FLASH_TOTAL_SIZE; addr += STM32WL_SECTOR_SIZE) {
            _internal_flash_erase(&_InternalFSConfig, (addr - LFS_FLASH_ADDR) / STM32WL_SECTOR_SIZE);
        }

        // lfs format
        this->format();

        // mount again if still failed, give up
        if (!STM32_LittleFS::begin())
            return false;
    }

    return true;
}
