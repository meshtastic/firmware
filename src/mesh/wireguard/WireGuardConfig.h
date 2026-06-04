#pragma once

#include "configuration.h"

#if HAS_WIREGUARD_VPN

#include <Arduino.h>
#include "meshtastic/module_config.pb.h"

/**
 * Configuration values for the experimental WireGuard VPN client.
 * These defaults may be overridden at compile time.
 */

typedef struct WireGuardConfig {
    bool enabled;           ///< Whether the tunnel should start when networking and NTP are ready
    char address[32];       ///< Client IPv4 address (e.g. 10.0.0.2)
    char serverAddr[64];    ///< WireGuard server host
    uint16_t serverPort;    ///< WireGuard server port
    char privateKey[64];    ///< Client private key
    char publicKey[64];     ///< Server public key
    char presharedKey[64];  ///< Optional preshared key, not available in Wireguard-ESP32 as of now
} WireGuardConfig;

#ifndef WIREGUARD_DEFAULT_ENABLED
#define WIREGUARD_DEFAULT_ENABLED false
#endif

#ifndef WIREGUARD_DEFAULT_ADDRESS
#define WIREGUARD_DEFAULT_ADDRESS "" // Client address in Wireguard configuration. Must not include subnet mask.
#endif

#ifndef WIREGUARD_DEFAULT_SERVER_ADDR
#define WIREGUARD_DEFAULT_SERVER_ADDR "" // Default WireGuard server (public) IP address - FQDN not tested
#endif

#ifndef WIREGUARD_DEFAULT_SERVER_PORT
#define WIREGUARD_DEFAULT_SERVER_PORT 0
#endif

#ifndef WIREGUARD_DEFAULT_PRIVATE_KEY
#define WIREGUARD_DEFAULT_PRIVATE_KEY ""
#endif

#ifndef WIREGUARD_DEFAULT_PUBLIC_KEY
#define WIREGUARD_DEFAULT_PUBLIC_KEY ""
#endif

#ifndef WIREGUARD_DEFAULT_PRESHARED_KEY
// WireGuard-ESP32 does not currently make use of preshared keys but the
// structure contains a field for future compatibility. Define a default
// empty value so that the configuration compiles when the feature is
// enabled.
#define WIREGUARD_DEFAULT_PRESHARED_KEY ""
#endif

/// Global WireGuard configuration in RAM.
extern WireGuardConfig wireGuardConfig;

void applyWireGuardModuleConfig(const meshtastic_ModuleConfig_WireGuardConfig &config);
bool isWireGuardConfigComplete(const char **reason = nullptr);
meshtastic_ModuleConfig_WireGuardConfig_Status getWireGuardStatus();
const char *getWireGuardLastError();
void setWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig_Status status, const char *lastError = "");

#endif // HAS_WIREGUARD_VPN
