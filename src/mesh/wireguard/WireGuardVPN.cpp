#include "configuration.h"
#if HAS_WIREGUARD_VPN
#include "mesh/wireguard/WireGuardVPN.h"
#include "mesh/wireguard/WireGuardConfig.h"
#include "mesh/NodeDB.h"
#include "gps/RTC.h"
#include <cstring>
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

    const char *configError = nullptr;
    if (!wireGuardConfig.enabled) {
        setWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig_Status_DISABLED);
        return false;
    }

    if (!isWireGuardConfigComplete(&configError)) {
        LOG_WARN("WireGuard not configured: %s", configError);
        setWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig_Status_NOT_CONFIGURED, configError);
        return false;
    }

    if (getRTCQuality() < RTCQualityNTP) {
        LOG_INFO("WireGuard waiting for NTP");
        setWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig_Status_WAITING_FOR_NTP);
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
        setWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig_Status_WAITING_FOR_NETWORK);
        return false;
    }

    IPAddress serverIp;
    bool resolved = false;
    setWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig_Status_RESOLVING_SERVER);
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
        setWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig_Status_FAILED, "server unreachable");
        return false;
    }

    IPAddress localIp;
    if (!localIp.fromString(wireGuardConfig.address)) {
        LOG_ERROR("Invalid WireGuard client IP %s", wireGuardConfig.address);
        setWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig_Status_FAILED, "invalid client address");
        return false;
    }

    String serverIpStr = serverIp.toString();
    if (!vpn.begin(localIp,                       // local (client) IP/subnet
                   wireGuardConfig.privateKey,    // base64 private key
                   serverIpStr.c_str(),           // server IP address
                   wireGuardConfig.publicKey,     // server public key
                   wireGuardConfig.serverPort)) {
        LOG_ERROR("Unable to start WireGuard tunnel");
        setWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig_Status_FAILED, "tunnel start failed");
        return false;
    }

    LOG_INFO("WireGuard tunnel started to %s (%s):%u",
             wireGuardConfig.serverAddr,
             serverIpStr.c_str(),
             wireGuardConfig.serverPort);

    running = true;
    setWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig_Status_RUNNING);
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
    if (wireGuardConfig.enabled) {
        setWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig_Status_WAITING_FOR_NETWORK);
    } else {
        setWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig_Status_DISABLED);
    }
}

bool isWireGuardRunning()
{
    return running;
}

void populateWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig &config)
{
    config.status = getWireGuardStatus();
    strncpy(config.last_error, getWireGuardLastError(), sizeof(config.last_error));
    config.last_error[sizeof(config.last_error) - 1] = '\0';
}

#endif // HAS_WIREGUARD_VPN
