#include "configuration.h"
#if HAS_WIREGUARD_VPN
#include "mesh/wireguard/WireGuardVPN.h"

static bool running = false;

bool startWireGuard()
{
    // TODO: Implement WireGuard initialization and connection setup
    running = true;
    return running;
}

void stopWireGuard()
{
    // TODO: Implement teardown for WireGuard
    running = false;
}

#endif // HAS_WIREGUARD_VPN