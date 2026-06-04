#pragma once

#include "configuration.h"

#if HAS_WIREGUARD_VPN

#include <Arduino.h>
#include "mesh/wireguard/WireGuardConfig.h"

/// Initialize WireGuard VPN. Returns true on success.
bool startWireGuard();

/// Stop the WireGuard VPN service.
void stopWireGuard();

/// Query whether the VPN is currently running.
bool isWireGuardRunning();

/// Copy transient runtime status into a WireGuard config protobuf.
void populateWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig &config);

#endif // HAS_WIREGUARD_VPN
