#pragma once

#include "configuration.h"

#if HAS_WIREGUARD_VPN

#include <Arduino.h>

/// Initialize WireGuard VPN. Returns true on success.
bool startWireGuard();

/// Stop the WireGuard VPN service.
void stopWireGuard();

#endif // HAS_WIREGUARD_VPN