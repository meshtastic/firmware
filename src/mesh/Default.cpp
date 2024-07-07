#include "Default.h"

uint32_t Default::getConfiguredOrDefaultMs(uint32_t configuredInterval)
{
    if (configuredInterval > 0)
        return configuredInterval * 1000;
    return default_broadcast_interval_secs * 1000;
}

uint32_t Default::getConfiguredOrDefaultMs(uint32_t configuredInterval, uint32_t defaultInterval)
{
    if (configuredInterval > 0)
        return configuredInterval * 1000;
    return defaultInterval * 1000;
}

uint32_t Default::getConfiguredOrDefault(uint32_t configured, uint32_t defaultValue)
{
    if (configured > 0)
        return configured;

    return defaultValue;
}