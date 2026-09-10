#include "configuration.h"

#ifdef MESHTASTIC_ENABLE_APPROTECT
#ifdef ARCH_NRF52

#include "APProtect.h"
#include <nrf.h>

// M22 (audit): refuse to engage APPROTECT on silicon revisions where the
// debug-port lockout is publicly known to be bypassable. nRF52840 build
// codes AAB0..AAF0 are all affected by the SWD glitching attack documented
// in LimitedResults' nRF52-series research - i.e. every nRF52840 currently
// in shipping Meshtastic hardware. Engaging APPROTECT on these revisions
// gives the operator a false sense of security AND irreversibly blocks
// legitimate SWD-based dev/recovery: the worst of both. Detect-and-skip
// is the policy; log loudly so the operator knows.
//
// FICR.INFO.VARIANT is a 32-bit register storing 4 ASCII characters as a
// big-endian word ('AAB0' = 0x41414230). Compare whole-word.
static bool isApProtectVulnerableSilicon(uint32_t variant)
{
    // Known-affected nRF52840 build codes. Only remove entries with
    // positive evidence the variant is fixed.
    static const uint32_t kVulnerable[] = {
        0x41414230, // AAB0
        0x41414330, // AAC0
        0x41414430, // AAD0
        0x41414530, // AAE0
        0x41414630, // AAF0
    };
    for (uint32_t v : kVulnerable) {
        if (variant == v)
            return true;
    }
    return false;
}

static void logApProtectVariant(const char *prefix, uint32_t variant)
{
    // Render the 4-byte ASCII variant for the log line. FICR encodes it
    // big-endian, so the high byte is the first ASCII character.
    char buf[5] = {(char)((variant >> 24) & 0xFF), (char)((variant >> 16) & 0xFF), (char)((variant >> 8) & 0xFF),
                   (char)(variant & 0xFF), '\0'};
    LOG_WARN("%s (FICR.INFO.VARIANT='%s', 0x%08x)", prefix, buf, variant);
}

void enableAPProtect()
{
    const uint32_t variant = NRF_FICR->INFO.VARIANT;

    if (isApProtectVulnerableSilicon(variant)) {
        logApProtectVariant("APPROTECT NOT engaged: silicon revision is publicly known "
                            "bypassable via SWD glitching. Skipping irreversible UICR write so "
                            "the operator is not misled into thinking SWD is locked when it is "
                            "not. To override (e.g. for testing on a known-vulnerable board), "
                            "rebuild with -DMESHTASTIC_APPROTECT_OVERRIDE_VULNERABLE_SILICON=1",
                            variant);
#ifndef MESHTASTIC_APPROTECT_OVERRIDE_VULNERABLE_SILICON
        return;
#else
        LOG_WARN("APPROTECT vulnerable-silicon override flag set; engaging anyway");
#endif
    }

    // APPROTECT register: 0x00 = enabled (protected), 0xFF = disabled (open)
    // On nRF52840, UICR.APPROTECT at address 0x10001208
    if (NRF_UICR->APPROTECT != 0x00) {
        LOG_WARN("Enabling APPROTECT - debug port will be disabled after reset");

        // UICR writes require NVMC to be in write mode
        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
            ;

        NRF_UICR->APPROTECT = 0x00;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
            ;

        // Return NVMC to read-only mode
        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
            ;

        // UICR APPROTECT is latched at chip reset, so the lock is NOT in effect
        // until we reset. Force a reset now to close the window where SWD remains
        // attachable on this same boot. We're called early in setup() before any
        // sensitive data is in RAM, so the reboot is safe.
        LOG_INFO("APPROTECT written; resetting to engage debug port lockout");
        NVIC_SystemReset();
        // unreachable
    } else {
        LOG_DEBUG("APPROTECT already enabled");
    }
}

#else
// Non-nRF52 builds - no-op
void enableAPProtect()
{
    LOG_DEBUG("APPROTECT not supported on this platform");
}
#endif // ARCH_NRF52
#endif // MESHTASTIC_ENABLE_APPROTECT
