#include "configuration.h"

#if (defined(USE_LR2021) || defined(ARCH_PORTDUINO)) && RADIOLIB_EXCLUDE_LR2021 != 1

#include "LR2021Patched.h"
#include "LR2021PramImage.h"
#include <modules/LR2021/LR2021_registers.h>

int16_t LR2021Patched::standby()
{
    const int16_t res = LR2021::standby();
    if (patchPending && res == RADIOLIB_ERR_NONE) {
        patchPending = false;
        (void)loadPatch(); // a failure is logged and begin() carries on with the ROM firmware
    }
    return res;
}

// DS 22.3.2: the magic word at 0x800FF8 says a patch is running; the word after it carries its type (bits 23:16) and
// version (bits 15:8), as Semtech's lr20xx_patch_get_version() reads them
int16_t LR2021Patched::readPatchState(uint32_t &magic, uint32_t &typeVersion)
{
    const uint8_t req[4] = {(uint8_t)(RADIOLIB_LR2021_PRAM_ADDR_LOADED >> 16), (uint8_t)(RADIOLIB_LR2021_PRAM_ADDR_LOADED >> 8),
                            (uint8_t)RADIOLIB_LR2021_PRAM_ADDR_LOADED, 2};
    uint8_t rpl[8] = {0};
    const int16_t res = SPIcommand(RADIOLIB_LR2021_CMD_READ_REG_MEM_32, false, rpl, sizeof(rpl), req, sizeof(req));
    magic = ((uint32_t)rpl[0] << 24) | ((uint32_t)rpl[1] << 16) | ((uint32_t)rpl[2] << 8) | rpl[3];
    typeVersion = ((uint32_t)rpl[4] << 24) | ((uint32_t)rpl[5] << 16) | ((uint32_t)rpl[6] << 8) | rpl[7];
    return res;
}

int16_t LR2021Patched::loadPatch()
{
    // The ROM version picks the image (DS 22.3.1: LR2021 is 0x01 0x18); RadioLib's getVersion() is private
    uint8_t fw[2] = {0};
    (void)SPIcommand(RADIOLIB_LR2021_CMD_GET_VERSION, false, fw, sizeof(fw));

    // What the chip holds before we write anything, to see whether a part ever comes up already patched
    uint32_t magicBefore = 0;
    uint32_t versionWordBefore = 0;
    if (readPatchState(magicBefore, versionWordBefore) == RADIOLIB_ERR_NONE)
        LOG_INFO("LR2021 FW 0x%02x%02x, before PRAM load: magic 0x%08x, type/version word 0x%08x%s", fw[0], fw[1],
                 (unsigned)magicBefore, (unsigned)versionWordBefore,
                 magicBefore == RADIOLIB_LR2021_PRAM_LOADED_MAGIC ? " (a patch is already running)" : "");

    // RadioLib's writeRegMem32() is private, and its shared write path sends one stray byte after an LR2021's 24-bit
    // address frame. Send the exact frame instead, in Semtech's 32-word blocks.
    constexpr size_t blockWords = 32;
    uint8_t frame[3 + blockWords * sizeof(uint32_t)];
    int16_t res = RADIOLIB_ERR_NONE;
    for (size_t word = 0; word < LR2021_PRAM_IMAGE_WORDS && res == RADIOLIB_ERR_NONE; word += blockWords) {
        const size_t n = (LR2021_PRAM_IMAGE_WORDS - word < blockWords) ? LR2021_PRAM_IMAGE_WORDS - word : blockWords;
        const uint32_t addr = RADIOLIB_LR2021_PRAM_BASE + word * sizeof(uint32_t);
        frame[0] = (uint8_t)(addr >> 16);
        frame[1] = (uint8_t)(addr >> 8);
        frame[2] = (uint8_t)addr;
        for (size_t i = 0; i < n; i++) {
            const uint32_t w = LR2021_PRAM_IMAGE[word + i];
            frame[3 + i * 4] = (uint8_t)(w >> 24);
            frame[4 + i * 4] = (uint8_t)(w >> 16);
            frame[5 + i * 4] = (uint8_t)(w >> 8);
            frame[6 + i * 4] = (uint8_t)w;
        }
        res = mod->SPIwriteStream((uint16_t)RADIOLIB_LR2021_CMD_WRITE_REG_MEM_32, frame, 3 + n * sizeof(uint32_t), true, false);
    }

    if (res == RADIOLIB_ERR_NONE) {
        uint8_t activate[] = {0x00};
        res = SPIcommand(RADIOLIB_LR2021_CMD_ACTIVATE_PRAM, true, activate, sizeof(activate));
    }

    uint32_t magic = 0;
    uint32_t versionWord = 0;
    if (res == RADIOLIB_ERR_NONE)
        res = readPatchState(magic, versionWord);

    if (res != RADIOLIB_ERR_NONE) {
        LOG_ERROR("LR2021 FW 0x%02x%02x PRAM load failed, err %d; running the ROM firmware", fw[0], fw[1], res);
    } else if (magic != RADIOLIB_LR2021_PRAM_LOADED_MAGIC) {
        LOG_ERROR("LR2021 FW 0x%02x%02x PRAM not running after load, magic 0x%08x; running the ROM firmware", fw[0], fw[1],
                  (unsigned)magic);
        res = RADIOLIB_ERR_UNKNOWN;
    } else {
        LOG_INFO("LR2021 FW 0x%02x%02x, PRAM type 0x%02x version 0x%02x loaded", fw[0], fw[1],
                 (unsigned)((versionWord >> 16) & 0xFF), (unsigned)((versionWord >> 8) & 0xFF));
    }
    return res;
}

#endif
