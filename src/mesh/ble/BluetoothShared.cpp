#include "BluetoothShared.h"

#include "BluetoothStatus.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"

#ifdef MODE_SHARED_NODE
#include "mesh/sharedNode/VirtualNodeManager.h"
#endif

#include <cstring>

namespace bluetooth
{

#if HAS_SCREEN
static char pairingPromptPasskey[7] = {};
#endif
static bool pairingPromptShowing = false;

static void formatPasskey(uint32_t passkey, char *out, size_t outSize)
{
    if (!out || outSize == 0) {
        return;
    }
    snprintf(out, outSize, "%06u", passkey);
}

#if HAS_SCREEN
static void drawPairingPrompt(OLEDDisplay *display, OLEDDisplayUiState *, int16_t x, int16_t y)
{
    char pin[8] = {};
    snprintf(pin, sizeof(pin), "%.3s %.3s", pairingPromptPasskey, pairingPromptPasskey + 3);

    int xOffset = display->width() / 2;
    int yOffset = display->height() <= 80 ? 0 : 12;
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(xOffset + x, yOffset + y, "Bluetooth");

#if !defined(M5STACK_UNITC6L)
    display->setFont(FONT_SMALL);
    yOffset = display->height() == 64 ? yOffset + FONT_HEIGHT_MEDIUM - 4 : yOffset + FONT_HEIGHT_MEDIUM + 5;
    display->drawString(xOffset + x, yOffset + y, "Enter this code");
#endif

    display->setFont(FONT_LARGE);
    yOffset = display->height() == 64 ? yOffset + FONT_HEIGHT_SMALL - 5 : yOffset + FONT_HEIGHT_SMALL + 5;
    display->drawString(xOffset + x, yOffset + y, pin);

    display->setFont(FONT_SMALL);
    char deviceName[64] = {};
    snprintf(deviceName, sizeof(deviceName), "Name: %s", getDeviceName());
    yOffset = display->height() == 64 ? yOffset + FONT_HEIGHT_LARGE - 6 : yOffset + FONT_HEIGHT_LARGE + 5;
    display->drawString(xOffset + x, yOffset + y, deviceName);
}
#endif

uint32_t choosePairingPasskey()
{
#ifdef MODE_SHARED_NODE
    SharedNode::Pairing pairing = SharedNode::pairingPolicy.beginPairing();
    const SharedNode::Role role = SharedNode::roleForSlot(pairing.slot);
    if (role == SharedNode::Role::ADMIN) {
        LOG_INFO("Use shared-node admin random passkey");
    } else if (role == SharedNode::Role::GUEST) {
        LOG_INFO("Use shared-node guest fixed passkey");
    } else if (config.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN) {
        LOG_INFO("Use random passkey");
    }
    return pairing.passkey;
#else
    if (config.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN) {
        LOG_INFO("Use random passkey");
        return random(100000, 999999);
    }
    return config.bluetooth.fixed_pin;
#endif
}

bool requiresSecurePairing()
{
#ifdef MODE_SHARED_NODE
    return true;
#else
    return config.bluetooth.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN;
#endif
}

void showPairingPrompt(uint32_t passkey)
{
    char passkeyText[7] = {};
    formatPasskey(passkey, passkeyText, sizeof(passkeyText));
    showPairingPrompt(passkeyText);
}

void showPairingPrompt(const char *passkeyText)
{
    if (!passkeyText) {
        return;
    }

    powerFSM.trigger(EVENT_BLUETOOTH_PAIR);
    meshtastic::BluetoothStatus newStatus(passkeyText);
    bluetoothStatus->updateStatus(&newStatus);

#if HAS_SCREEN
    strncpy(pairingPromptPasskey, passkeyText, sizeof(pairingPromptPasskey) - 1);
    pairingPromptPasskey[sizeof(pairingPromptPasskey) - 1] = '\0';
    if (screen) {
        screen->startAlert(drawPairingPrompt);
    }
#endif
    pairingPromptShowing = true;
}

void clearPairingPrompt()
{
    if (!pairingPromptShowing) {
        return;
    }

    pairingPromptShowing = false;
#if HAS_SCREEN
    if (screen) {
        screen->endAlert();
    }
#endif
}

void notifyConnected()
{
    meshtastic::BluetoothStatus newStatus(meshtastic::BluetoothStatus::ConnectionState::CONNECTED);
    bluetoothStatus->updateStatus(&newStatus);
}

void notifyDisconnected()
{
    meshtastic::BluetoothStatus newStatus(meshtastic::BluetoothStatus::ConnectionState::DISCONNECTED);
    bluetoothStatus->updateStatus(&newStatus);
    clearPairingPrompt();
}

#ifdef MODE_SHARED_NODE
void enforceSharedNodePairingMode()
{
    if (config.bluetooth.mode != meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN) {
        LOG_WARN("Shared-node requires random Bluetooth pairing mode; forcing random mode");
        config.bluetooth.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
    }
}

void rememberKnownConnection(uint16_t connHandle, const SharedNode::PeerIdentity &identity)
{
    const uint8_t knownSlot = SharedNode::pairingPolicy.slotForIdentity(identity);
    if (knownSlot != SharedNode::INVALID_SLOT) {
        SharedNode::pairingPolicy.rememberConnectionSlot(connHandle, identity, knownSlot);
    }
}

uint8_t resolveConnectionSlot(uint16_t connHandle, const SharedNode::PeerIdentity &identity)
{
    return SharedNode::pairingPolicy.resolveSlotForConnection(connHandle, identity);
}

void logResolvedPairingSlot(uint8_t slot)
{
    const SharedNode::Role role = SharedNode::roleForSlot(slot);
    if (role == SharedNode::Role::ADMIN) {
        LOG_INFO("Shared-node admin paired");
    } else if (role == SharedNode::Role::UNKNOWN) {
        LOG_WARN("Shared-node pairing completed without an available slot");
    }
}

void consumePendingPairingSlot()
{
    SharedNode::pairingPolicy.consumePendingPairingSlot();
}

bool canClearKnownClients(const char *operationName)
{
    if (virtualNodeManager.hasActiveAdminSession()) {
        return true;
    }

    LOG_WARN("Ignoring shared-node %s without an active admin session", operationName ? operationName : "clear");
    return false;
}

void clearKnownClients()
{
    SharedNode::pairingPolicy.clearAllKnownClients();
}

uint32_t fnv1a32(const uint8_t *data, size_t length)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; data && i < length; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

bool addressIsEmpty(const uint8_t *data, size_t length)
{
    if (!data) {
        return true;
    }

    for (size_t i = 0; i < length; i++) {
        if (data[i] != 0) {
            return false;
        }
    }
    return true;
}
#endif

} // namespace bluetooth
