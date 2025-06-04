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
#define WIREGUARD_DEFAULT_ADDRESS "192.168.1.1" //EXAMPLE
#endif

#ifndef WIREGUARD_DEFAULT_SERVER_ADDR
#define WIREGUARD_DEFAULT_SERVER_ADDR "216.58.214.174" //EXAMPLE
#endif

#ifndef WIREGUARD_DEFAULT_SERVER_PORT
#define WIREGUARD_DEFAULT_SERVER_PORT 60105 //EXAMPLE
#endif

#ifndef WIREGUARD_DEFAULT_PRIVATE_KEY
#define WIREGUARD_DEFAULT_PRIVATE_KEY "YElkG5676G/sE1X8WN14deSpb6yCPxgwxWel8OXvDlE=" //EXAMPLE
#endif

#ifndef WIREGUARD_DEFAULT_PUBLIC_KEY
#define WIREGUARD_DEFAULT_PUBLIC_KEY "4rWE3xfIDMV0/tR0tDMNHmELhF3Gs&*SeL3KLsYLjE=" //EXAMPLE
#endif

// #ifndef WIREGUARD_DEFAULT_PRESHARED_KEY  // Note: WireGuard-ESP32 does not support preshared keys as of now.
// #define WIREGUARD_DEFAULT_PRESHARED_KEY ""
// #endif

#ifndef WIREGUARD_DEFAULT_DNS
#define WIREGUARD_DEFAULT_DNS "192.168.200.60"
#endif

#ifndef WIREGUARD_DEFAULT_ALLOWED_IPS
#define WIREGUARD_DEFAULT_ALLOWED_IPS "0.0.0.0/0"
#endif

#ifndef WIREGUARD_DEFAULT_PERSISTENT_KEEPALIVE
#define WIREGUARD_DEFAULT_PERSISTENT_KEEPALIVE 0
#endif

/// Global WireGuard configuration in RAM.
extern WireGuardConfig wireGuardConfig;

#endif // HAS_WIREGUARD_VPN