#include "AdminKeys.h"
#include "NodeDB.h"
#include "configuration.h"
#include <string.h>

#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif

namespace AdminKeys
{

#ifdef ARCH_PORTDUINO
// applyHostKeys() mirrors the first PROTOBUF_SLOTS host keys into config.security, so only the
// ones past that are counted here.
static size_t hostExtraCount()
{
    const size_t configured = portduino_config.admin_keys.size();
    return configured > PROTOBUF_SLOTS ? configured - PROTOBUF_SLOTS : 0;
}
#endif

size_t count()
{
    size_t n = 0;
    for (size_t slot = 0; slot < PROTOBUF_SLOTS; slot++) {
        if (config.security.admin_key[slot].size == 32)
            n++;
    }
#ifdef ARCH_PORTDUINO
    n += hostExtraCount();
#endif
    return n;
}

const uint8_t *keyAt(size_t i)
{
    // Skips empty slots, so callers see a dense list whatever admin_key_count says.
    for (size_t slot = 0; slot < PROTOBUF_SLOTS; slot++) {
        if (config.security.admin_key[slot].size != 32)
            continue;
        if (i == 0)
            return config.security.admin_key[slot].bytes;
        i--;
    }
#ifdef ARCH_PORTDUINO
    if (i < hostExtraCount())
        return portduino_config.admin_keys[PROTOBUF_SLOTS + i].data();
#endif
    return nullptr;
}

void applyHostKeys()
{
#ifdef ARCH_PORTDUINO
    if (portduino_config.admin_keys.empty())
        return;

    size_t slot = 0;
    for (; slot < portduino_config.admin_keys.size() && slot < PROTOBUF_SLOTS; slot++) {
        memcpy(config.security.admin_key[slot].bytes, portduino_config.admin_keys[slot].data(), 32);
        config.security.admin_key[slot].size = 32;
    }
    // Clear any slot the host file no longer fills, so removing a key removes its access.
    for (size_t i = slot; i < PROTOBUF_SLOTS; i++) {
        memset(config.security.admin_key[i].bytes, 0, sizeof(config.security.admin_key[i].bytes));
        config.security.admin_key[i].size = 0;
    }
    config.security.admin_key_count = (pb_size_t)slot;
    LOG_INFO("Admin keys from host config: %u authorized", (unsigned)count());
#endif
}

bool isAuthorized(const uint8_t *publicKey)
{
    if (!publicKey)
        return false;
    for (size_t i = 0, n = count(); i < n; i++) {
        const uint8_t *key = keyAt(i);
        if (key && memcmp(publicKey, key, 32) == 0)
            return true;
    }
    return false;
}

} // namespace AdminKeys
