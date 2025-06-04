#include "mesh/wireguard/WireGuardConfig.h"

#if HAS_WIREGUARD_VPN

// Default WireGuard configuration. Users may modify this at runtime
// in the future when persistent storage is implemented.
WireGuardConfig wireGuardConfig = {
    WIREGUARD_DEFAULT_ADDRESS,
    WIREGUARD_DEFAULT_SERVER_ADDR,
    WIREGUARD_DEFAULT_SERVER_PORT,
    WIREGUARD_DEFAULT_PRIVATE_KEY,
    WIREGUARD_DEFAULT_PUBLIC_KEY,
    WIREGUARD_DEFAULT_PRESHARED_KEY,
    WIREGUARD_DEFAULT_DNS,
    WIREGUARD_DEFAULT_ALLOWED_IPS,
    WIREGUARD_DEFAULT_PERSISTENT_KEEPALIVE
};

#endif // HAS_WIREGUARD_VPN