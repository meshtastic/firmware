#pragma once

#include "mesh/generated/meshtastic/config.pb.h"

/** The list in config.transport.enabled, which says which transports this node runs. */
namespace TransportConfig
{
/** Whether `t` is in `cfg.enabled`. */
inline bool enables(const meshtastic_Config_TransportConfig &cfg, meshtastic_Config_TransportConfig_Transport t)
{
    for (pb_size_t i = 0; i < cfg.enabled_count; i++) {
        if (cfg.enabled[i] == t)
            return true;
    }
    return false;
}

/** Put `t` in or take it out of `cfg.enabled`. False when a full list refused an add. */
inline bool set(meshtastic_Config_TransportConfig &cfg, meshtastic_Config_TransportConfig_Transport t, bool on)
{
    const pb_size_t capacity = sizeof(cfg.enabled) / sizeof(cfg.enabled[0]);
    pb_size_t kept = 0;
    for (pb_size_t i = 0; i < cfg.enabled_count; i++) {
        if (cfg.enabled[i] != t)
            cfg.enabled[kept++] = cfg.enabled[i];
    }
    cfg.enabled_count = kept;
    if (!on)
        return true;
    if (kept >= capacity)
        return false;
    cfg.enabled[cfg.enabled_count++] = t;
    return true;
}
} // namespace TransportConfig
