#pragma once

#include "configuration.h"

#if HAS_WIREGUARD_VPN

#include <Arduino.h>

/// Initialize WireGuard VPN. Returns true on success.
bool startWireGuard();

/// Stop the WireGuard VPN service.
void stopWireGuard();

/// Query whether the VPN is currently running.
bool isWireGuardRunning();

#endif // HAS_WIREGUARD_VPN