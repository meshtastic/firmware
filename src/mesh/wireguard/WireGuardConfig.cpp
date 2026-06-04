#include "mesh/wireguard/WireGuardConfig.h"

#if HAS_WIREGUARD_VPN

#include <cstring>

// Default WireGuard configuration. Users may modify this at runtime
// in the future when persistent storage is implemented.
WireGuardConfig wireGuardConfig = {
    WIREGUARD_DEFAULT_ADDRESS,
    WIREGUARD_DEFAULT_SERVER_ADDR,
    WIREGUARD_DEFAULT_SERVER_PORT,
    WIREGUARD_DEFAULT_PRIVATE_KEY,
    WIREGUARD_DEFAULT_PUBLIC_KEY,
    WIREGUARD_DEFAULT_PRESHARED_KEY
}; 

template <size_t N>
static void copyWireGuardString(char (&dest)[N], const char *src)
{
    strncpy(dest, src ? src : "", N);
    dest[N - 1] = '\0';
}

void applyWireGuardModuleConfig(const meshtastic_ModuleConfig_WireGuardConfig &config)
{
    copyWireGuardString(wireGuardConfig.address, config.address);
    copyWireGuardString(wireGuardConfig.serverAddr, config.server_addr);
    wireGuardConfig.serverPort = config.server_port;
    copyWireGuardString(wireGuardConfig.privateKey, config.private_key);
    copyWireGuardString(wireGuardConfig.publicKey, config.public_key);
    copyWireGuardString(wireGuardConfig.presharedKey, config.preshared_key);
}

#endif // HAS_WIREGUARD_VPN
