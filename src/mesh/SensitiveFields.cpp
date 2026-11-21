#include "SensitiveFields.h"

#include "mesh/generated/meshtastic/admin.pb.h"
#include "mesh/generated/meshtastic/channel.pb.h"
#include "mesh/generated/meshtastic/clientonly.pb.h"
#include "mesh/generated/meshtastic/config.pb.h"
#include "mesh/generated/meshtastic/connection_status.pb.h"
#include "mesh/generated/meshtastic/device_ui.pb.h"
#include "mesh/generated/meshtastic/field_metadata_registry.h"
#include "mesh/generated/meshtastic/module_config.pb.h"
#include <pb_common.h>
#include <string.h>

namespace SensitiveFields
{
namespace
{

struct KnownType {
    const char *cName; // nanopb C name: the proto name with '.' as '_'
    const pb_msgdesc_t *desc;
};

// Every message type the registry names. nanopb descriptors carry no type name, so a registry entry
// missing here is not enforced; test_sensitive_fields fails until it is added.
#define KNOWN_TYPE(type) {#type, &type##_msg}
const KnownType knownTypes[] = {
    KNOWN_TYPE(meshtastic_BluetoothConnectionStatus),
    KNOWN_TYPE(meshtastic_ChannelSettings),
    KNOWN_TYPE(meshtastic_Config_BluetoothConfig),
    KNOWN_TYPE(meshtastic_Config_NetworkConfig),
    KNOWN_TYPE(meshtastic_Config_SecurityConfig),
    KNOWN_TYPE(meshtastic_DeviceProfile),
    KNOWN_TYPE(meshtastic_DeviceUIConfig),
    KNOWN_TYPE(meshtastic_LockdownAuth),
    KNOWN_TYPE(meshtastic_ModuleConfig_MQTTConfig),
};
#undef KNOWN_TYPE

bool sameName(const char *protoName, const char *cName)
{
    for (; *protoName && *cName; protoName++, cName++)
        if ((*protoName == '.' ? '_' : *protoName) != *cName)
            return false;
    return *protoName == *cName;
}

const char *knownName(const pb_msgdesc_t *desc)
{
    for (const auto &t : knownTypes)
        if (t.desc == desc)
            return t.cName;
    return nullptr;
}

bool listed(const char *cName, pb_size_t tag)
{
    for (size_t i = 0; i < MESHTASTIC_FIELD_METADATA_COUNT; i++) {
        const meshtastic_field_metadata_t &m = MESHTASTIC_FIELD_METADATA[i];
        if (m.sensitive && m.field_tag == (int)tag && sameName(m.message_type, cName))
            return true;
    }
    return false;
}

bool allZero(const void *p, size_t n)
{
    const uint8_t *b = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n; i++)
        if (b[i])
            return false;
    return true;
}

bool elementBlank(const pb_field_iter_t &f, const void *element)
{
    switch (PB_LTYPE(f.type)) {
    case PB_LTYPE_BYTES:
        return static_cast<const pb_bytes_array_t *>(element)->size == 0;
    case PB_LTYPE_STRING:
        return *static_cast<const char *>(element) == '\0';
    default:
        return allZero(element, f.data_size);
    }
}

const void *element(const pb_field_iter_t &f, pb_size_t i)
{
    return static_cast<const uint8_t *>(f.pData) + (size_t)i * f.data_size;
}

void *element(pb_field_iter_t &f, pb_size_t i)
{
    return static_cast<uint8_t *>(f.pData) + (size_t)i * f.data_size;
}

// Used entries of a repeated field. A fixed-count array's pSize points at f.array_size itself.
pb_size_t usedCount(const pb_field_iter_t &f)
{
    const pb_size_t n = *static_cast<const pb_size_t *>(f.pSize);
    return n < f.array_size ? n : f.array_size;
}

// How many submessages the field holds: the active oneof member, each used repeated entry, or the
// single (possibly unset optional) one. Zero for anything that is not a static submessage.
pb_size_t submessageCount(const pb_field_iter_t &f)
{
    if (PB_ATYPE(f.type) != PB_ATYPE_STATIC || !PB_LTYPE_IS_SUBMSG(f.type))
        return 0;
    switch (PB_HTYPE(f.type)) {
    case PB_HTYPE_ONEOF:
        return *static_cast<const pb_size_t *>(f.pSize) == f.tag ? 1 : 0;
    case PB_HTYPE_REPEATED:
        return usedCount(f);
    default:
        return 1;
    }
}

// Pointer fields are not handled: no message reachable from AdminMessage uses them.
bool hasValue(const pb_field_iter_t &f)
{
    if (PB_ATYPE(f.type) == PB_ATYPE_CALLBACK)
        return !allZero(f.pField, f.data_size); // a callback is set to carry it
    if (PB_ATYPE(f.type) != PB_ATYPE_STATIC)
        return false;
    switch (PB_HTYPE(f.type)) {
    case PB_HTYPE_ONEOF:
        return *static_cast<const pb_size_t *>(f.pSize) == f.tag;
    case PB_HTYPE_REPEATED:
        for (pb_size_t i = 0; i < usedCount(f); i++)
            if (!elementBlank(f, element(f, i)))
                return true;
        return false;
    case PB_HTYPE_OPTIONAL: // == PB_HTYPE_SINGULAR; only proto3 optional has a has_ flag
        if (f.pSize)
            return *static_cast<const bool *>(f.pSize);
        break;
    }
    return !elementBlank(f, f.pData);
}

void clear(pb_field_iter_t &f)
{
    if (PB_ATYPE(f.type) == PB_ATYPE_CALLBACK) {
        memset(f.pField, 0, f.data_size);
        return;
    }
    if (PB_ATYPE(f.type) != PB_ATYPE_STATIC)
        return;
    switch (PB_HTYPE(f.type)) {
    case PB_HTYPE_ONEOF:
        if (*static_cast<pb_size_t *>(f.pSize) == f.tag) {
            *static_cast<pb_size_t *>(f.pSize) = 0;
            memset(f.pData, 0, f.data_size);
        }
        return;
    case PB_HTYPE_REPEATED:
        if (f.pSize != &f.array_size)
            *static_cast<pb_size_t *>(f.pSize) = 0;
        memset(f.pData, 0, (size_t)f.data_size * f.array_size);
        return;
    case PB_HTYPE_OPTIONAL:
        if (f.pSize)
            *static_cast<bool *>(f.pSize) = false;
        break;
    }
    memset(f.pData, 0, f.data_size);
}

// Copy a static field's value, presence included. A oneof member is left alone: a blank one is
// inactive, and activating it would switch the message to another member.
void copy(pb_field_iter_t &dst, const pb_field_iter_t &src)
{
    if (PB_ATYPE(dst.type) != PB_ATYPE_STATIC)
        return;
    switch (PB_HTYPE(dst.type)) {
    case PB_HTYPE_ONEOF:
        return;
    case PB_HTYPE_REPEATED:
        if (dst.pSize != &dst.array_size)
            *static_cast<pb_size_t *>(dst.pSize) = usedCount(src);
        memcpy(dst.pData, src.pData, (size_t)dst.data_size * dst.array_size);
        return;
    case PB_HTYPE_OPTIONAL:
        if (dst.pSize)
            *static_cast<bool *>(dst.pSize) = *static_cast<const bool *>(src.pSize);
        break;
    }
    memcpy(dst.pData, src.pData, dst.data_size);
}

} // namespace

bool isSensitive(const pb_msgdesc_t *desc, pb_size_t tag)
{
    const char *name = knownName(desc);
    return name && listed(name, tag);
}

bool redact(const pb_msgdesc_t *desc, void *msg)
{
    const char *name = knownName(desc);
    bool any = false;
    pb_field_iter_t f;
    if (!pb_field_iter_begin(&f, desc, msg))
        return false;
    do {
        if (name && listed(name, f.tag)) {
            any |= hasValue(f);
            clear(f);
            continue;
        }
        for (pb_size_t i = 0; i < submessageCount(f); i++)
            any |= redact(f.submsg_desc, element(f, i));
    } while (pb_field_iter_next(&f));
    return any;
}

bool anySet(const pb_msgdesc_t *desc, const void *msg)
{
    const char *name = knownName(desc);
    pb_field_iter_t f;
    if (!pb_field_iter_begin_const(&f, desc, msg))
        return false;
    do {
        if (name && listed(name, f.tag)) {
            if (hasValue(f))
                return true;
            continue;
        }
        for (pb_size_t i = 0; i < submessageCount(f); i++)
            if (anySet(f.submsg_desc, element(f, i)))
                return true;
    } while (pb_field_iter_next(&f));
    return false;
}

void restore(const pb_msgdesc_t *desc, void *msg, const void *stored)
{
    const char *name = knownName(desc);
    pb_field_iter_t f, s;
    if (!pb_field_iter_begin(&f, desc, msg) || !pb_field_iter_begin_const(&s, desc, stored))
        return;
    do {
        if (name && listed(name, f.tag)) {
            if (!hasValue(f))
                copy(f, s);
            continue;
        }
        const pb_size_t fn = submessageCount(f), sn = submessageCount(s);
        for (pb_size_t i = 0; i < fn && i < sn; i++)
            restore(f.submsg_desc, element(f, i), element(s, i));
    } while (pb_field_iter_next(&f) && pb_field_iter_next(&s));
}

void restoreSection(const pb_msgdesc_t *desc, void *msg, const pb_msgdesc_t *storeDesc, const void *store)
{
    pb_field_iter_t f, s;
    if (!pb_field_iter_begin(&f, desc, msg))
        return;
    do {
        if (PB_HTYPE(f.type) != PB_HTYPE_ONEOF || submessageCount(f) == 0)
            continue;
        if (!pb_field_iter_begin_const(&s, storeDesc, store))
            return;
        do {
            if (PB_HTYPE(s.type) != PB_HTYPE_REPEATED && submessageCount(s) == 1 && s.submsg_desc == f.submsg_desc) {
                restore(f.submsg_desc, f.pData, s.pData);
                return;
            }
        } while (pb_field_iter_next(&s));
        return;
    } while (pb_field_iter_next(&f));
}

const pb_msgdesc_t *descriptorFor(const char *protoName)
{
    for (const auto &t : knownTypes)
        if (sameName(protoName, t.cName))
            return t.desc;
    return nullptr;
}

} // namespace SensitiveFields
