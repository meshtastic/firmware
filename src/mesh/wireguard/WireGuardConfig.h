#pragma once

#include "configuration.h"

#if HAS_WIREGUARD_VPN

#include <Arduino.h>

/**
 * Configuration values for the experimental WireGuard VPN client.
 * These defaults may be overridden at compile time.
 */

typedef struct WireGuardConfig {
    const char *address;       ///< Client address/subnet (e.g. 10.0.0.2/24)
    const char *serverAddr;    ///< WireGuard server host
    uint16_t serverPort;       ///< WireGuard server port
    const char *privateKey;    ///< Client private key
    const char *publicKey;     ///< Server public key
    const char *presharedKey;  ///< Optional preshared key, not available in Wireguard-ESP32 as of now
    const char *dns;           ///< DNS server to use while connected
    const char *allowedIps;    ///< Routes through the tunnel
    uint16_t persistentKeepalive; ///< Optional keepalive interval
} WireGuardConfig;

#ifndef WIREGUARD_DEFAULT_ADDRESS
#define WIREGUARD_DEFAULT_ADDRESS "" // Client address in Wireguard configuration. Must not include subnet mask.
#endif

#ifndef WIREGUARD_DEFAULT_SERVER_ADDR
#define WIREGUARD_DEFAULT_SERVER_ADDR "" // Default WireGuard server (public) IP address - FQDN not tested
#endif

#ifndef WIREGUARD_DEFAULT_SERVER_PORT
#define WIREGUARD_DEFAULT_SERVER_PORT 60105
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

#ifndef WIREGUARD_DEFAULT_DNS
#define WIREGUARD_DEFAULT_DNS "" // Default DNS server to use while connected - not tested yet
#endif

#ifndef WIREGUARD_DEFAULT_ALLOWED_IPS
#define WIREGUARD_DEFAULT_ALLOWED_IPS "0.0.0.0/0" // Default allowed IPs for the WireGuard tunnel - all traffic - not tested yet
#endif

#ifndef WIREGUARD_DEFAULT_PERSISTENT_KEEPALIVE
#define WIREGUARD_DEFAULT_PERSISTENT_KEEPALIVE 0 // Not tested yet
#endif

/// Global WireGuard configuration in RAM.
extern WireGuardConfig wireGuardConfig;

#endif // HAS_WIREGUARD_VPN