#include "configuration.h"

#if defined(HAS_AUDIO_CODEC2) && !MESHTASTIC_EXCLUDE_AUDIO && defined(CODEC2_EXTERNAL_CODEBOOKS) && defined(ARCH_NRF52)

#include "AudioCodebooks.h"
#include <codebook_q8.h>

// The core enables NRFX_QSPI but does not supply its config defaults the way it
// does for POWER, SPIS and TEMP. Lowest priority, matching those.
#ifndef NRFX_QSPI_DEFAULT_CONFIG_IRQ_PRIORITY
#define NRFX_QSPI_DEFAULT_CONFIG_IRQ_PRIORITY 7
#endif

#include <nrfx_qspi.h>
#include <string.h>

// The nrfx drivers are on the include path but not compiled by the core, so
// they are pulled in as source. main-nrf52.cpp does the same for nrfx_wdt.
#include <nrfx_qspi.c>

#if !defined(PIN_QSPI_SCK) || !defined(PIN_QSPI_CS) || !defined(PIN_QSPI_IO0)
#error "CODEC2_EXTERNAL_CODEBOOKS needs the PIN_QSPI_* set in variant.h"
#endif

// nRF52840 maps external QSPI here, and has no XIPEN register: the window is
// live as soon as nrfx_qspi_init() triggers TASKS_ACTIVATE.
#define QSPI_XIP_BASE 0x12000000UL

// Blob position within the chip. The firmware does not mount this flash
// (FSCom is InternalFS), so byte 0 is free; a variant can move it if that
// ever changes.
#ifndef AUDIO_CODEBOOK_QSPI_OFFSET
#define AUDIO_CODEBOOK_QSPI_OFFSET 0
#endif

// Must match tools/make_codebook_blob.py in the codec2 repo.
#define CODEBOOK_BLOB_VERSION 1
#define CODEBOOK_BLOB_TABLES 3
#define CODEBOOK_BLOB_SCALE_MILLI 500

struct CodebookBlobHeader {
    char magic[4]; // "C2CB"
    uint16_t version;
    uint16_t tables;
    uint32_t scaleMilliDb;
    uint32_t offset[CODEBOOK_BLOB_TABLES];
    uint32_t length[CODEBOOK_BLOB_TABLES];
};

static const uint32_t EXPECTED_LEN[CODEBOOK_BLOB_TABLES] = {512 * 20, 512 * 20, 500 * 41};

const char *audioCodebooksBind()
{
    static bool bound = false;
    if (bound)
        return nullptr;

    nrfx_qspi_config_t cfg =
        NRFX_QSPI_DEFAULT_CONFIG(PIN_QSPI_SCK, PIN_QSPI_CS, PIN_QSPI_IO0, PIN_QSPI_IO1, PIN_QSPI_IO2, PIN_QSPI_IO3);

    // The default is DIV16, about 250 KB/s. 32 MHz is the ceiling the part and
    // these boards support for the 0x0B fast read.
    cfg.phy_if.sck_freq = NRF_QSPI_FREQ_DIV1;

    // Single-line FASTREAD, so no vendor-specific quad-enable sequence and the
    // same code works on every chip. Quad would cut the 700C codebook sweep from
    // 16 ms to about 4 ms, but enabling it here boot looped the board twice and
    // wants proving in tools/nrf52_bench first.
    cfg.prot_if.readoc = NRF_QSPI_READOC_FASTREAD;
    nrfx_err_t err = nrfx_qspi_init(&cfg, NULL, NULL);
    if (err != NRFX_SUCCESS && err != NRFX_ERROR_INVALID_STATE) // already up is fine
        return "QSPI init failed";

    const uint8_t *base = (const uint8_t *)(QSPI_XIP_BASE + AUDIO_CODEBOOK_QSPI_OFFSET);
    CodebookBlobHeader hdr;
    memcpy(&hdr, base, sizeof(hdr));

    if (memcmp(hdr.magic, "C2CB", 4) != 0)
        return "codebook blob missing";
    if (hdr.version != CODEBOOK_BLOB_VERSION || hdr.tables != CODEBOOK_BLOB_TABLES)
        return "codebook blob version mismatch";
    if (hdr.scaleMilliDb != CODEBOOK_BLOB_SCALE_MILLI)
        return "codebook blob quantisation mismatch";
    for (int i = 0; i < CODEBOOK_BLOB_TABLES; i++)
        if (hdr.length[i] != EXPECTED_LEN[i])
            return "codebook blob table size mismatch";

#ifdef AUDIO_CODEBOOK_CHECK
    // The checks above only read the header. Corrupt table bytes pass every one
    // of them and then quietly wreck the VQ, so hash what is actually there.
    for (int t = 0; t < CODEBOOK_BLOB_TABLES; t++) {
        uint32_t h = 2166136261u;
        const uint8_t *p = base + hdr.offset[t];
        for (uint32_t i = 0; i < hdr.length[t]; i++) {
            h ^= p[i];
            h *= 16777619u;
        }
        LOG_WARN("Audio: cb table %d off 0x%06lx len %lu fnv1a %08lx first %02x %02x %02x %02x", t, (unsigned long)hdr.offset[t],
                 (unsigned long)hdr.length[t], (unsigned long)h, p[0], p[1], p[2], p[3]);
    }
#endif

    if (codec2_set_external_codebooks((const int8_t *)(base + hdr.offset[0]), (const int8_t *)(base + hdr.offset[1]),
                                      (const int8_t *)(base + hdr.offset[2])) != 0)
        return "codebook bind rejected";

    LOG_INFO("Audio: codebooks bound from QSPI XIP at 0x%08lx", (unsigned long)(QSPI_XIP_BASE + AUDIO_CODEBOOK_QSPI_OFFSET));
    bound = true;
    return nullptr;
}

#endif
