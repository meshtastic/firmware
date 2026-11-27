#pragma once

#include "mesh/generated/meshtastic/config.pb.h"
#include <stddef.h>
#include <stdint.h>

/**
 * The public keys authorized to administer this node.
 *
 * These are the up-to-three keys carried in config.security.admin_key plus, on meshtasticd, any
 * further keys the host supplied in config.yaml that did not fit that array. Every authorization
 * site goes through here so a new key source reaches all of them at once.
 */
namespace AdminKeys
{
/// Slots in config.security.admin_key. Keys past this many live only in the host's config.
constexpr size_t PROTOBUF_SLOTS =
    sizeof(meshtastic_Config_SecurityConfig::admin_key) / sizeof(meshtastic_Config_SecurityConfig_admin_key_t);

/// How many authorized keys are configured.
size_t count();

/// The i-th authorized key, 32 bytes, or nullptr when i is past the end.
const uint8_t *keyAt(size_t i);

/// True when publicKey, read as 32 bytes, is authorized.
bool isAuthorized(const uint8_t *publicKey);

/// Mirrors the host's admin keys into config.security.admin_key, replacing what was there; a no-op
/// when the host configured none. Called at boot and after a remote admin writes security config.
void applyHostKeys();

/// True when any key is configured, i.e. remote admin is possible at all.
inline bool any()
{
    return count() > 0;
}
} // namespace AdminKeys
