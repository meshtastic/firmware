#include "configuration.h"

#ifdef MESHTASTIC_ENABLE_APPROTECT
#ifdef ARCH_NRF52

#include "APProtect.h"
#include <nrf.h>

void enableAPProtect()
{
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

        LOG_INFO("APPROTECT set, will take effect after next reset");
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
