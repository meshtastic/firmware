/**
 * @file extfs-nrf52.cpp
 * @brief Optional external QSPI LittleFS for nRF52840 boards.
 *
 * Compiled only when BOTH of the following are defined in a board's variant.h:
 *
 *   EXTERNAL_FLASH_USE_QSPI        — chip is physically wired (hardware fact)
 *   MESHTASTIC_EXTERNAL_FLASH_FS   — board opts into LittleFS on that chip
 *
 * The two-define pattern lets boards that use external flash for other purposes
 * (raw storage, FAT, custom formats) declare the hardware without getting an
 * unwanted LittleFS mount.  If either define is absent this file is a no-op
 * and extFS stays null.
 *
 * Required variant.h defines (already on QSPI-capable boards):
 *   PIN_QSPI_SCK, PIN_QSPI_CS, PIN_QSPI_IO0..IO3
 *   EXTERNAL_FLASH_USE_QSPI        (hardware capability)
 *   MESHTASTIC_EXTERNAL_FLASH_FS   (intent to use as LittleFS — add this)
 *
 * Optional build flag:
 *   EXTFLASH_SIZE_BYTES   — total chip size in bytes (defaults to 2 MB)
 *
 * This file is intentionally self-contained so it can be proposed as a
 * pull request to Meshtastic firmware without touching other platform files
 * beyond the weak extFSInit() hook in FSCommon.cpp.
 */

#if defined(ARCH_NRF52) && defined(EXTERNAL_FLASH_USE_QSPI) && defined(MESHTASTIC_EXTERNAL_FLASH_FS)

#include "FSCommon.h"
#include "configuration.h"
#include <Arduino.h>
#include <nrfx_qspi.h>
#include <cstring>

// ── Chip geometry ─────────────────────────────────────────────────────────────

#ifndef EXTFLASH_SIZE_BYTES
#define EXTFLASH_SIZE_BYTES (2 * 1024 * 1024) // Default: 2 MB (MX25R1635F / GD25Q16C)
#endif

#define EXTFLASH_PAGE_SIZE   256
#define EXTFLASH_SECTOR_SIZE 4096
#define EXTFLASH_BLOCK_COUNT (EXTFLASH_SIZE_BYTES / EXTFLASH_SECTOR_SIZE)

// ── QSPI hardware init ────────────────────────────────────────────────────────

static uint8_t _qspi_scratch[EXTFLASH_PAGE_SIZE] __attribute__((aligned(4)));
static bool    _qspi_ready = false;

static bool _qspi_init()
{
    if (_qspi_ready) return true;

#ifndef NRFX_QSPI_DEFAULT_CONFIG_IRQ_PRIORITY
#define NRFX_QSPI_DEFAULT_CONFIG_IRQ_PRIORITY 6
#endif

    nrfx_qspi_config_t cfg = NRFX_QSPI_DEFAULT_CONFIG(
        PIN_QSPI_SCK, PIN_QSPI_CS,
        PIN_QSPI_IO0, PIN_QSPI_IO1, PIN_QSPI_IO2, PIN_QSPI_IO3);

    // Conservative 8 MHz clock; avoids needing the QE status-register bit set
    // for true quad I/O. Works reliably on all boards with QSPI flash.
    cfg.phy_if.sck_freq = NRF_QSPI_FREQ_DIV8;

    nrfx_err_t err = nrfx_qspi_init(&cfg, NULL, NULL); // blocking mode
    if (err == NRFX_ERROR_INVALID_STATE) {
        // Already initialised (e.g. by bootloader hand-off) — that's fine.
        _qspi_ready = true;
        return true;
    }
    if (err != NRFX_SUCCESS) {
        LOG_ERROR("QSPI init failed: %d", (int)err);
        return false;
    }

    _qspi_ready = true;
    LOG_DEBUG("External QSPI flash ready (8 MHz)");
    return true;
}

// ── LittleFS I/O callbacks ────────────────────────────────────────────────────

static int _ef_read(const struct lfs_config *c, lfs_block_t block,
                    lfs_off_t off, void *buf, lfs_size_t size)
{
    (void)c;
    uint32_t addr = block * EXTFLASH_SECTOR_SIZE + off;

    if (((uintptr_t)buf & 3) == 0 && (size & 3) == 0) {
        return (nrfx_qspi_read(buf, size, addr) == NRFX_SUCCESS) ? LFS_ERR_OK : LFS_ERR_IO;
    }

    // Unaligned: go through scratch
    uint8_t *dst = (uint8_t *)buf;
    while (size > 0) {
        uint32_t chunk  = (size > sizeof(_qspi_scratch)) ? sizeof(_qspi_scratch) : size;
        uint32_t qchunk = (chunk + 3) & ~3u;
        if (nrfx_qspi_read(_qspi_scratch, qchunk, addr) != NRFX_SUCCESS) return LFS_ERR_IO;
        memcpy(dst, _qspi_scratch, chunk);
        dst += chunk; addr += chunk; size -= chunk;
    }
    return LFS_ERR_OK;
}

static int _ef_prog(const struct lfs_config *c, lfs_block_t block,
                    lfs_off_t off, const void *buf, lfs_size_t size)
{
    (void)c;
    uint32_t addr = block * EXTFLASH_SECTOR_SIZE + off;

    if (((uintptr_t)buf & 3) == 0 && (size & 3) == 0) {
        if (nrfx_qspi_write(buf, size, addr) != NRFX_SUCCESS) return LFS_ERR_IO;
    } else {
        const uint8_t *src = (const uint8_t *)buf;
        while (size > 0) {
            uint32_t chunk  = (size > sizeof(_qspi_scratch)) ? sizeof(_qspi_scratch) : size;
            uint32_t qchunk = (chunk + 3) & ~3u;
            memcpy(_qspi_scratch, src, chunk);
            for (uint32_t i = chunk; i < qchunk; i++) _qspi_scratch[i] = 0xFF;
            if (nrfx_qspi_write(_qspi_scratch, qchunk, addr) != NRFX_SUCCESS) return LFS_ERR_IO;
            src += chunk; addr += chunk; size -= chunk;
        }
    }

    while (nrfx_qspi_mem_busy_check() == NRFX_ERROR_BUSY) yield();
    return LFS_ERR_OK;
}

static int _ef_erase(const struct lfs_config *c, lfs_block_t block)
{
    (void)c;
    uint32_t addr = block * EXTFLASH_SECTOR_SIZE;
    if (nrfx_qspi_erase(NRF_QSPI_ERASE_LEN_4KB, addr) != NRFX_SUCCESS) return LFS_ERR_IO;
    while (nrfx_qspi_mem_busy_check() == NRFX_ERROR_BUSY) yield();
    return LFS_ERR_OK;
}

static int _ef_sync(const struct lfs_config *c)
{
    (void)c;
    while (nrfx_qspi_mem_busy_check() == NRFX_ERROR_BUSY) yield();
    return LFS_ERR_OK;
}

// ── LittleFS instance ─────────────────────────────────────────────────────────

static uint8_t _read_buf     [EXTFLASH_PAGE_SIZE] __attribute__((aligned(4)));
static uint8_t _prog_buf     [EXTFLASH_PAGE_SIZE] __attribute__((aligned(4)));
static uint8_t _lookahead_buf[64]                 __attribute__((aligned(4)));

static struct lfs_config _cfg = {
    .context  = NULL,
    .read     = _ef_read,
    .prog     = _ef_prog,
    .erase    = _ef_erase,
    .sync     = _ef_sync,

    .read_size      = EXTFLASH_PAGE_SIZE,
    .prog_size      = EXTFLASH_PAGE_SIZE,
    .block_size     = EXTFLASH_SECTOR_SIZE,
    .block_count    = EXTFLASH_BLOCK_COUNT,
    .lookahead      = 512,

    .read_buffer      = _read_buf,
    .prog_buffer      = _prog_buf,
    .lookahead_buffer = _lookahead_buf,
    .file_buffer      = NULL,
};

static Adafruit_LittleFS _extLittleFS(&_cfg);

// ── extFSInit() — overrides the weak no-op in FSCommon.cpp ───────────────────

void extFSInit()
{
    if (!_qspi_init()) return;

    if (_extLittleFS.begin()) {
        LOG_INFO("External QSPI LittleFS mounted (%u blocks x %u B)",
                 EXTFLASH_BLOCK_COUNT, EXTFLASH_SECTOR_SIZE);
        extFS = &_extLittleFS;
        return;
    }

    // First boot or corrupted — format then remount
    LOG_WARN("External QSPI LittleFS mount failed, formatting...");
    if (!_extLittleFS.format() || !_extLittleFS.begin()) {
        LOG_ERROR("External QSPI LittleFS format/mount failed");
        return;
    }

    LOG_INFO("External QSPI LittleFS formatted and mounted");
    extFS = &_extLittleFS;
}

#endif // ARCH_NRF52 && EXTERNAL_FLASH_USE_QSPI && MESHTASTIC_EXTERNAL_FLASH_FS
