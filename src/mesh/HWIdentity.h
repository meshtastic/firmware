#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * Hardware-bound identity derivation for the Meshtastic Curve25519 keypair.
 *
 * Instead of generating the node's keypair from pure HW RNG on first boot and
 * losing it on factory reset (firmware issue #8211), deterministically derive
 * the private key from values tied to the physical silicon:
 *
 *     seed = SHA256(device_id || salt || "meshtastic-id-v1")
 *     priv = clamp25519(seed)
 *     pub  = Curve25519::eval(priv)
 *
 * Where:
 *   device_id : 16-byte immutable per-chip UID
 *     - ESP32-S3/C3/C6: eFuse BLK2 OPTIONAL_UNIQUE_ID (128-bit, random per die,
 *       burned by Espressif at the factory, publicly readable but per-chip)
 *     - nRF52840:        FICR.DEVICEID + FICR.DEVICEADDR (64 + 48 bits,
 *       factory-programmed by Nordic, per-chip, publicly readable)
 *     Both are already read into myNodeInfo.device_id at NodeDB.cpp init time.
 *
 *   salt : 32 bytes random, generated on first boot via HW RNG
 *     - Stored at /hwid_salt.bin on the root of LittleFS (ESP32) or
 *       InternalFS (nRF52). Both survive Meshtastic factoryReset() which
 *       only removes /prefs. That means the salt persists across key
 *       regeneration initiated by admin message or by the user wiping
 *       config.
 *
 * Security properties (Tier A — no eFuse burn):
 *   + Factory reset preserves identity (same chip + same salt = same key)
 *   + Firmware flash preserves identity (NVS is not touched by flash)
 *   + Different chips produce different keys (UID differs)
 *   + No new hardware lockdown step required — ships on existing fleet
 *   - An attacker with a firmware dump that includes both the eFuse UID read
 *     and NVS salt can reproduce the key (no better than status quo, which
 *     stores the private key in LittleFS /prefs)
 *
 * Tier B (deferred) would burn a secret into an RD_DIS'd eFuse HMAC_UP slot
 * on ESP32-S3; that key physically cannot leave the silicon even with a full
 * firmware dump. Requires a one-time irreversible provisioning step and is
 * out of scope for this PR.
 *
 * The derivation is called exactly once per key generation decision point
 * (NodeDB::installDefaultNodeDB + AdminModule keypair regen); afterwards the
 * key lives in the normal security config just like a random key, so the
 * rest of the firmware is unaware of the new mechanism.
 */
namespace HWIdentity
{
/**
 * Derive a Curve25519 keypair deterministically from the chip's per-die UID
 * plus a persistent NVS-stored salt.
 *
 * @param pubOut  32-byte buffer for the derived public key
 * @param privOut 32-byte buffer for the derived (clamped) private key
 * @return true on success, false if the UID is unavailable (e.g. portduino
 *         native build, or eFuse read failed). Caller should fall back to
 *         random keygen on false.
 */
bool deriveKey(uint8_t *pubOut, uint8_t *privOut);

/**
 * Check whether HW-bound derivation is possible without actually computing
 * a key. Used for logging and conditional paths.
 *
 * @return true if this target has a per-chip UID plus persistent storage
 *         for the salt.
 */
bool isAvailable();
} // namespace HWIdentity
