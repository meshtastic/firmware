#include "configuration.h"
#if HAS_WIREGUARD_VPN
#include "mesh/wireguard/WireGuardVPN.h"
#include "mesh/wireguard/WireGuardConfig.h"
#include <WiFiUdp.h>
#include <WiFi.h>

static bool running = false;
static WiFiUDP wgUDP;

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

    if (!wgUDP.begin(0)) {
        LOG_ERROR("Unable to open UDP socket for WireGuard");
        return false;
    }

    const char handshake[] = "MESHTASTIC_WG_HELLO";
    wgUDP.beginPacket(serverIp, wireGuardConfig.serverPort);
    wgUDP.write((const uint8_t *)handshake, sizeof(handshake) - 1);
    wgUDP.endPacket();

    LOG_INFO("WireGuard handshake sent to %s:%u", serverIp.toString().c_str(),
             wireGuardConfig.serverPort);

    running = true;
    return running;
}

void stopWireGuard()
{
    if (!running) {
        return;
    }
    wgUDP.stop();
    LOG_INFO("WireGuard VPN stopped");
    running = false;
}

bool isWireGuardRunning()
{
    return running;
}

#endif // HAS_WIREGUARD_VPN