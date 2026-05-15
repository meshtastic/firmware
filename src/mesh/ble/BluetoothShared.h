#pragma once

/**
 * @file BluetoothShared.h
 * @brief BLE helpers shared by platform-specific Bluetooth backends.
 */

#include <Arduino.h>

#ifdef MODE_SHARED_NODE
#include "mesh/sharedNode/PairingPolicy.h"
#endif

namespace bluetooth
{

/**
 * @brief Chooses the passkey for the next secure pairing attempt.
 *
 * In shared-node mode this delegates to SharedNode::PairingPolicy. In normal
 * mode it mirrors the configured fixed/random Bluetooth PIN behavior.
 */
uint32_t choosePairingPasskey();

/**
 * @brief Returns whether Bluetooth must require authenticated/encrypted access.
 */
bool requiresSecurePairing();

/**
 * @brief Publishes pairing state to power/UI/status observers.
 */
void showPairingPrompt(uint32_t passkey);

/**
 * @brief Publishes pairing state to power/UI/status observers.
 *
 * The string overload preserves leading zeroes from BLE stacks that provide
 * passkeys as ASCII digits.
 */
void showPairingPrompt(const char *passkeyText);

/**
 * @brief Clears any active pairing prompt from the display.
 */
void clearPairingPrompt();

/**
 * @brief Publishes a connected Bluetooth status update.
 */
void notifyConnected();

/**
 * @brief Publishes a disconnected Bluetooth status update and clears pairing UI.
 */
void notifyDisconnected();

#ifdef MODE_SHARED_NODE
/**
 * @brief Forces shared-node Bluetooth config to the required random PIN mode.
 */
void enforceSharedNodePairingMode();

/**
 * @brief Attaches a known peer identity to its stored slot for a live connection.
 */
void rememberKnownConnection(uint16_t connHandle, const SharedNode::PeerIdentity &identity);

/**
 * @brief Resolves and stores the slot for an authenticated connection.
 */
uint8_t resolveConnectionSlot(uint16_t connHandle, const SharedNode::PeerIdentity &identity);

/**
 * @brief Logs the shared-node role associated with a resolved pairing slot.
 */
void logResolvedPairingSlot(uint8_t slot);

/**
 * @brief Drops any passkey-reserved shared-node slot after pairing failure.
 */
void consumePendingPairingSlot();

/**
 * @brief Checks whether known shared-node/BLE client state may be cleared.
 */
bool canClearKnownClients(const char *operationName);

/**
 * @brief Clears known shared-node clients from the pairing policy.
 */
void clearKnownClients();

/**
 * @brief FNV-1a hash used for non-sensitive identity fingerprints.
 */
uint32_t fnv1a32(const uint8_t *data, size_t length);

/**
 * @brief Returns true when a byte address is all zeroes or missing.
 */
bool addressIsEmpty(const uint8_t *data, size_t length);
#endif

} // namespace bluetooth
