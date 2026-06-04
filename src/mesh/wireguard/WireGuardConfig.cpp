#include "mesh/wireguard/WireGuardConfig.h"

#if HAS_WIREGUARD_VPN

#include <cstring>

static meshtastic_ModuleConfig_WireGuardConfig_Status wireGuardStatus =
    meshtastic_ModuleConfig_WireGuardConfig_Status_DISABLED;
static char wireGuardLastError[96] = "";

WireGuardConfig wireGuardConfig = {
    WIREGUARD_DEFAULT_ENABLED,
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

static bool isEmpty(const char *s)
{
    return !s || s[0] == '\0';
}

static bool isWireGuardKeyShapeValid(const char *key)
{
    if (!key || strlen(key) != 44) {
        return false;
    }

    for (size_t i = 0; i < 44; i++) {
        const char c = key[i];
        const bool base64Char = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' ||
                                c == '/' || c == '=';
        if (!base64Char) {
            return false;
        }
    }

    return true;
}

bool isWireGuardConfigComplete(const char **reason)
{
    const char *error = nullptr;

    if (!wireGuardConfig.enabled) {
        error = "disabled";
    } else if (isEmpty(wireGuardConfig.address)) {
        error = "missing client address";
    } else if (isEmpty(wireGuardConfig.serverAddr)) {
        error = "missing server address";
    } else if (wireGuardConfig.serverPort == 0) {
        error = "missing server port";
    } else if (!isWireGuardKeyShapeValid(wireGuardConfig.privateKey)) {
        error = "invalid private key";
    } else if (!isWireGuardKeyShapeValid(wireGuardConfig.publicKey)) {
        error = "invalid public key";
    } else if (!isEmpty(wireGuardConfig.presharedKey) && !isWireGuardKeyShapeValid(wireGuardConfig.presharedKey)) {
        error = "invalid preshared key";
    }

    if (reason) {
        *reason = error;
    }
    return error == nullptr;
}

void setWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig_Status status, const char *lastError)
{
    wireGuardStatus = status;
    copyWireGuardString(wireGuardLastError, lastError);
}

meshtastic_ModuleConfig_WireGuardConfig_Status getWireGuardStatus()
{
    return wireGuardStatus;
}

const char *getWireGuardLastError()
{
    return wireGuardLastError;
}

void applyWireGuardModuleConfig(const meshtastic_ModuleConfig_WireGuardConfig &config)
{
    wireGuardConfig.enabled = config.enabled;
    copyWireGuardString(wireGuardConfig.address, config.address);
    copyWireGuardString(wireGuardConfig.serverAddr, config.server_addr);
    wireGuardConfig.serverPort = config.server_port;
    copyWireGuardString(wireGuardConfig.privateKey, config.private_key);
    copyWireGuardString(wireGuardConfig.publicKey, config.public_key);
    copyWireGuardString(wireGuardConfig.presharedKey, config.preshared_key);

    const char *reason = nullptr;
    if (!wireGuardConfig.enabled) {
        setWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig_Status_DISABLED);
    } else if (!isWireGuardConfigComplete(&reason)) {
        setWireGuardStatus(meshtastic_ModuleConfig_WireGuardConfig_Status_NOT_CONFIGURED, reason);
    }
}

#endif // HAS_WIREGUARD_VPN
