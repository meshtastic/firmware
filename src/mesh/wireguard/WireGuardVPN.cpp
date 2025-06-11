#include "configuration.h"
#if HAS_WIREGUARD_VPN
#include "mesh/wireguard/WireGuardVPN.h"
#include "mesh/wireguard/WireGuardConfig.h"
#include "mesh/NodeDB.h"
#include "gps/RTC.h"
#include <WiFi.h>
#include <WireGuard-ESP32.h>
#if HAS_WIFI
#include "mesh/wifi/WiFiAPClient.h"
#endif
#if HAS_ETHERNET
#include "mesh/eth/ethClient.h"
#endif

static bool running = false;
static WireGuard vpn;

bool startWireGuard()
{
    if (running) {
        return true;
    }

    if (getRTCQuality() < RTCQualityNTP) {
        LOG_INFO("WireGuard waiting for NTP");
        return false;
    }

    bool haveNetwork = false;
#if HAS_WIFI
    if (isWifiAvailable() && WiFi.isConnected()) {
        haveNetwork = true;
    }
#endif
#if HAS_ETHERNET
    if (isEthernetAvailable()) {
        haveNetwork = true;
    }
#endif
    if (!haveNetwork) {
        LOG_WARN("WireGuard requires an active network");
        return false;
    }

    IPAddress serverIp;
    bool resolved = false;
#if HAS_ETHERNET
    if (isEthernetAvailable()) {
        resolved = Ethernet.hostByName(wireGuardConfig.serverAddr, serverIp);
    }
#endif
#if HAS_WIFI
    if (!resolved && isWifiAvailable() && WiFi.isConnected()) {
        resolved = WiFi.hostByName(wireGuardConfig.serverAddr, serverIp);
    }
#endif
    if (!resolved) {
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