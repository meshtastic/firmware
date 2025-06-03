#pragma once

#include "configuration.h"

#if HAS_WIREGUARD_VPN

#include <Arduino.h>

/**
 * Configuration values for the experimental WireGuard VPN client.
 * Eventually these will be persisted on the device, but for now
 * they can be set at compile time by overriding the defaults below.
 */

typedef struct WireGuardConfig {
    const char *serverAddr;
    uint16_t serverPort;
    const char *privateKey;
    const char *publicKey;
    const char *presharedKey;
} WireGuardConfig;

#ifndef WIREGUARD_DEFAULT_SERVER_ADDR
#define WIREGUARD_DEFAULT_SERVER_ADDR "192.168.1.1"
#endif

#ifndef WIREGUARD_DEFAULT_SERVER_PORT
#define WIREGUARD_DEFAULT_SERVER_PORT 51820
#endif

#ifndef WIREGUARD_DEFAULT_PRIVATE_KEY
#define WIREGUARD_DEFAULT_PRIVATE_KEY ""
#endif

#ifndef WIREGUARD_DEFAULT_PUBLIC_KEY
#define WIREGUARD_DEFAULT_PUBLIC_KEY ""
#endif

#ifndef WIREGUARD_DEFAULT_PRESHARED_KEY
#define WIREGUARD_DEFAULT_PRESHARED_KEY ""
#endif

/// Global WireGuard configuration in RAM.
extern WireGuardConfig wireGuardConfig;

/// Initialize WireGuard VPN. Returns true on success.
bool startWireGuard();

/// Stop the WireGuard VPN service.
void stopWireGuard();

/// Query whether the VPN is currently running.
bool isWireGuardRunning();

#endif // HAS_WIREGUARD_VPN