#include "Default.h"
#include "../userPrefs.h"

uint32_t Default::getConfiguredOrDefaultMs(uint32_t configuredInterval, uint32_t defaultInterval)
{
    if (configuredInterval > 0)
        return configuredInterval * 1000;
    return defaultInterval * 1000;
}

uint32_t Default::getConfiguredOrDefaultMs(uint32_t configuredInterval)
{
    if (configuredInterval > 0)
        return configuredInterval * 1000;
    return default_broadcast_interval_secs * 1000;
}

uint32_t Default::getConfiguredOrDefault(uint32_t configured, uint32_t defaultValue)
{
    if (configured > 0)
        return configured;
    return defaultValue;
}
/**
 * Calculates the scaled value of the configured or default value in ms based on the number of online nodes.
 *
 * For example a default of 30 minutes (1800 seconds * 1000) would yield:
 *   45 nodes = 2475 * 1000
 *   60 nodes = 4500 * 1000
 *   75 nodes = 6525 * 1000
 *   90 nodes = 8550 * 1000
 * @param configured The configured value.
 * @param defaultValue The default value.
 * @param numOnlineNodes The number of online nodes.
 * @return The scaled value of the configured or default value.
 */
uint32_t Default::getConfiguredOrDefaultMsScaled(uint32_t configured, uint32_t defaultValue, uint32_t numOnlineNodes)
{
    // If we are a router, we don't scale the value. It's already significantly higher.
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER)
        return getConfiguredOrDefaultMs(configured, defaultValue);

    return getConfiguredOrDefaultMs(configured, defaultValue) * congestionScalingCoefficient(numOnlineNodes);
}

uint8_t Default::getConfiguredOrDefaultHopLimit(uint8_t configured)
{
#if USERPREFS_EVENT_MODE
    return (configured > HOP_RELIABLE) ? HOP_RELIABLE : config.lora.hop_limit;
#else
    return (configured >= HOP_MAX) ? HOP_MAX : config.lora.hop_limit;
#endif
}