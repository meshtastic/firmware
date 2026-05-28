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

/** Suppress unused-parameter warnings. */
#define LFS_UNUSED(p) (void)((p))

#define STM32WL_PAGE_SIZE (FLASH_PAGE_SIZE) // physical flash erase granularity: 2048 B
#define STM32WL_PAGE_COUNT (FLASH_PAGE_NB)
#define STM32WL_FLASH_BASE (FLASH_BASE)

/*
 * LFS tunables — all of these are stored in the LFS superblock.
 * Changing ANY of them is incompatible with the existing on-disk format;
 * the filesystem will be detected as corrupted and reformatted on first boot.
 *
 * LFS_FLASH_TOTAL_SIZE and LFS_BLOCK_SIZE are the only values to edit here.
 * All other parameters are derived.
 *
 * FLASH_END_ADDR is computed from FLASH_SIZE (read from the chip at link time).
 */
#define LFS_FLASH_TOTAL_SIZE                                                                                                     \
    (7 * STM32WL_PAGE_SIZE)  /* 14 KiB — last 7 physical pages (FORMAT BREAK: reduced from 10 pages / 20 KiB) */
#define LFS_BLOCK_SIZE (256) /* virtual block size (FORMAT BREAK if changed) */

#define LFS_FLASH_ADDR_END (FLASH_END_ADDR)
#define LFS_FLASH_ADDR_BASE (LFS_FLASH_ADDR_END - LFS_FLASH_TOTAL_SIZE + 1)

#if !CFG_DEBUG
#define _LFS_DBG(fmt, ...)
#else
#define _LFS_DBG(fmt, ...) printf("%s:%d (%s): " fmt "\n", __FILE__, __LINE__, __func__, __VA_ARGS__)
#endif

//--------------------------------------------------------------------+
// Write-behind page cache
//
// LFS requires block_size == erase granularity, but the STM32WL flash
// erases in 2048-byte pages.  To use smaller virtual LFS blocks we
// maintain a single-page RAM cache: the LFS erase/prog callbacks only
// update this buffer; the physical erase+reprogram is deferred to
// _internal_flash_sync() (or triggered automatically when a different
// physical page is addressed).
//
// This mirrors the approach used by the NRF52 Adafruit driver
// (flash_cache.c / flash_nrf5x.c) but adapted for the 2048-byte STM32WL
// page size and HAL doubleword-program requirement.
//--------------------------------------------------------------------+

alignas(8) static uint8_t _page_cache[STM32WL_PAGE_SIZE];
static uint32_t _page_cache_addr = UINT32_MAX; // UINT32_MAX = no page cached
static bool _page_cache_dirty = false;

/** Flush the cached page to flash (physical erase + doubleword program). */
static int _flash_cache_flush(void)
{
    if (!_page_cache_dirty)
        return LFS_ERR_OK;

    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .Page = (_page_cache_addr - STM32WL_FLASH_BASE) / STM32WL_PAGE_SIZE,
        .NbPages = 1,
    };
    uint32_t page_error = 0;

    if (HAL_FLASH_Unlock() != HAL_OK)
        return LFS_ERR_IO;
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    HAL_StatusTypeDef rc = HAL_FLASHEx_Erase(&erase, &page_error);
    if (rc == HAL_OK) {
        const uint64_t *p = (const uint64_t *)_page_cache;
        uint32_t addr = _page_cache_addr;
        for (size_t i = 0; i < STM32WL_PAGE_SIZE / 8; i++) {
            rc = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, *p++);
            if (rc != HAL_OK)
                break;
            addr += 8;
        }
    }
    HAL_StatusTypeDef lock_rc = HAL_FLASH_Lock();
    if (rc == HAL_OK)
        rc = lock_rc;

    if (rc == HAL_OK)
        _page_cache_dirty = false;
    return rc == HAL_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

/**
 * Ensure the physical page containing `page_addr` is loaded into the cache.
 * If a different dirty page is already cached it is flushed first.
 */
static int _flash_cache_load(uint32_t page_addr)
{
    if (_page_cache_addr == page_addr)
        return LFS_ERR_OK; // already cached

    int rc = _flash_cache_flush();
    if (rc != LFS_ERR_OK)
        return rc;

    memcpy(_page_cache, (const void *)page_addr, STM32WL_PAGE_SIZE);
    _page_cache_addr = page_addr;
    return LFS_ERR_OK;
}

//--------------------------------------------------------------------+
// LFS Disk IO
//--------------------------------------------------------------------+

static int _internal_flash_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
    LFS_UNUSED(c);

    uint32_t addr = LFS_FLASH_ADDR_BASE + block * LFS_BLOCK_SIZE + off;
    uint32_t page_addr = addr & ~(uint32_t)(STM32WL_PAGE_SIZE - 1);
    uint32_t page_off = addr & (uint32_t)(STM32WL_PAGE_SIZE - 1);

    if (_page_cache_addr == page_addr)
        memcpy(buffer, _page_cache + page_off, size);
    else
        memcpy(buffer, (const void *)addr, size);

    return LFS_ERR_OK;
}

// Program a region in a block. The block must have previously been erased.
// Writes are accumulated in the page cache and flushed on sync or page eviction.
static int _internal_flash_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    LFS_UNUSED(c);

    uint32_t addr = LFS_FLASH_ADDR_BASE + block * LFS_BLOCK_SIZE + off;
    uint32_t page_addr = addr & ~(uint32_t)(STM32WL_PAGE_SIZE - 1);
    uint32_t page_off = addr & (uint32_t)(STM32WL_PAGE_SIZE - 1);

    int rc = _flash_cache_load(page_addr);
    if (rc != LFS_ERR_OK)
        return rc;

    memcpy(_page_cache + page_off, buffer, size);
    _page_cache_dirty = true;
    return LFS_ERR_OK;
}

// Erase a virtual block. Marks the corresponding region in the page cache as 0xFF.
// Physical erase of the containing page is deferred until sync or page eviction.
static int _internal_flash_erase(const struct lfs_config *c, lfs_block_t block)
{
    LFS_UNUSED(c);

    uint32_t addr = LFS_FLASH_ADDR_BASE + block * LFS_BLOCK_SIZE;
    uint32_t page_addr = addr & ~(uint32_t)(STM32WL_PAGE_SIZE - 1);
    uint32_t page_off = addr & (uint32_t)(STM32WL_PAGE_SIZE - 1);

    int rc = _flash_cache_load(page_addr);
    if (rc != LFS_ERR_OK)
        return rc;

    memset(_page_cache + page_off, 0xFF, LFS_BLOCK_SIZE);
    _page_cache_dirty = true;
    return LFS_ERR_OK;
}

// Flush the write-behind cache to flash.
static int _internal_flash_sync(const struct lfs_config *c)
{
    LFS_UNUSED(c);
    return _flash_cache_flush();
}

static struct lfs_config _InternalFSConfig = {
    .context = NULL,

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
    .file_buffer = NULL,
};

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
    // failed to mount, erase all virtual blocks then format and mount again
    if (!STM32_LittleFS::begin()) {
        for (lfs_block_t block = 0; block < _InternalFSConfig.block_count; block++) {
            _internal_flash_erase(&_InternalFSConfig, block);
        }
        _flash_cache_flush(); // flush the last cached page

        // lfs format
        this->format();

        // mount again; if still failed, give up
        if (!STM32_LittleFS::begin())
            return false;
    }

    return true;
}
