#include "configuration.h"
#if HAS_WIREGUARD_VPN
#include "mesh/wireguard/WireGuardVPN.h"
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
    if (!WiFi.hostByName(WIREGUARD_SERVER_ADDR, serverIp)) {
        LOG_ERROR("WireGuard server %s unreachable", WIREGUARD_SERVER_ADDR);
        return false;
    }

    if (!wgUDP.begin(0)) {
        LOG_ERROR("Unable to open UDP socket for WireGuard");
        return false;
    }

    const char handshake[] = "MESHTASTIC_WG_HELLO";
    wgUDP.beginPacket(serverIp, WIREGUARD_SERVER_PORT);
    wgUDP.write((const uint8_t *)handshake, sizeof(handshake) - 1);
    wgUDP.endPacket();

    LOG_INFO("WireGuard handshake sent to %s:%u", serverIp.toString().c_str(),
             WIREGUARD_SERVER_PORT);

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