#include "configuration.h"
#if HAS_WIREGUARD_VPN
#include "mesh/wireguard/WireGuardVPN.h"
#include "mesh/wireguard/WireGuardConfig.h"
#include "mesh/NodeDB.h"
#include <WiFi.h>
#include <WireGuard-ESP32.h>

static bool running = false;
static WireGuard vpn;

bool startWireGuard()
{
    if (running) {
        return true;
    }

    if (WiFi.status() != WL_CONNECTED) {
        LOG_WARN("WireGuard requires active WiFi");
        return false;
    }

    IPAddress serverIp;
    if (!WiFi.hostByName(wireGuardConfig.serverAddr, serverIp)) {
        LOG_ERROR("WireGuard server %s unreachable", wireGuardConfig.serverAddr);
        return false;
    }

    IPAddress localIp;
    if (!localIp.fromString(wireGuardConfig.address)) {
        LOG_ERROR("Invalid WireGuard client IP %s", wireGuardConfig.address);
        return false;
    }

    if (!vpn.begin(localIp,                       // local (client) IP/subnet
                   wireGuardConfig.privateKey,    // base64 private key
                   wireGuardConfig.serverAddr,    // server hostname/IP
                   wireGuardConfig.publicKey,     // server public key
                   wireGuardConfig.serverPort)) {
        LOG_ERROR("Unable to start WireGuard tunnel");
        return false;
    }

    LOG_INFO("WireGuard tunnel started to %s:%u",
             serverIp.toString().c_str(),
             wireGuardConfig.serverPort);

    running = true;
    return running;
}

void stopWireGuard()
{
    if (!running) {
        return;
    }
    vpn.end();
    LOG_INFO("WireGuard VPN stopped");
    running = false;
}

bool isWireGuardRunning()
{
    return running;
}

#endif // HAS_WIREGUARD_VPN