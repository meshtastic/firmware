// Migration from the legacy (pre-v25) NodeDatabase shape to the slim header +
// satellite maps layout. Lives in its own translation unit so the bulk
// fixed-shape decode + field-by-field copy doesn't clutter NodeDB.cpp.
//
// Caller (NodeDB::loadFromDisk) decides what to do with the result:
//   - true  -> persist via saveNodeDatabaseToDisk()
//   - false -> reset via installDefaultNodeDatabase()
//
// This file (and the deviceonly_legacy proto) can be removed once
// DEVICESTATE_MIN_VER advances past 24.

#include "FSCommon.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "concurrency/LockGuard.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "mesh/generated/meshtastic/deviceonly_legacy.pb.h"
#include "meshUtils.h"
#include "security/SecureZero.h"

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
#include "security/EncryptedStorage.h"
#endif

#include <algorithm>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>

bool meshtastic_NodeDatabase_Legacy_callback(pb_istream_t *istream, pb_ostream_t *ostream, const pb_field_t *field)
{
    const auto *iter = reinterpret_cast<const pb_field_iter_t *>(field);
    if (ostream) {
        const auto *vec = static_cast<const std::vector<meshtastic_NodeInfoLite_Legacy> *>(iter->pData);
        for (const auto &item : *vec) {
            if (!pb_encode_tag_for_field(ostream, iter))
                return false;
            if (!pb_encode_submessage(ostream, meshtastic_NodeInfoLite_Legacy_fields, &item))
                return false;
        }
    }
    if (istream) {
        meshtastic_NodeInfoLite_Legacy node;
        auto *vec = static_cast<std::vector<meshtastic_NodeInfoLite_Legacy> *>(iter->pData);
        if (istream->bytes_left && pb_decode(istream, meshtastic_NodeInfoLite_Legacy_fields, &node))
            vec->push_back(node);
    }
    return true;
}

bool NodeDB::migrateLegacyNodeDatabase()
{
    LOG_WARN("NodeDatabase v%u: migrating to v%u", nodeDatabase.version, DEVICESTATE_CUR_VER);

    // _init_zero brace-inits the embedded std::vector via its explicit
    // (size_type, allocator) ctor, so default-construct instead.
    meshtastic_NodeDatabase_Legacy legacyDb{};
    legacyDb.version = 0;
    meshtastic_NodeDatabase_Legacy legacyEmpty{};
    legacyEmpty.version = DEVICESTATE_CUR_VER;
    size_t legacyEmptyEncoded = 0;
    pb_get_encoded_size(&legacyEmptyEncoded, meshtastic_NodeDatabase_Legacy_fields, &legacyEmpty);
    const size_t legacyBufSize = legacyEmptyEncoded + (MAX_NUM_NODES * meshtastic_NodeInfoLite_Legacy_size);
    auto legacyState = loadProto(nodeDatabaseFileName, legacyBufSize, sizeof(meshtastic_NodeDatabase_Legacy),
                                 &meshtastic_NodeDatabase_Legacy_msg, &legacyDb);
    if (legacyState != LoadFileResult::LOAD_SUCCESS) {
        LOG_ERROR("Failed to load NodeDatabase via legacy descriptor; installing default");
        return false;
    }

    nodeDatabase.nodes.clear();
    const size_t maxToMigrate = std::min<size_t>(legacyDb.nodes.size(), MAX_NUM_NODES);
    nodeDatabase.nodes.reserve(maxToMigrate);
    size_t posCount = 0, telCount = 0;
    {
        concurrency::LockGuard guard(&satelliteMutex);
        for (size_t i = 0; i < maxToMigrate; ++i) {
            const auto &legacy = legacyDb.nodes[i];
            meshtastic_NodeInfoLite slim = meshtastic_NodeInfoLite_init_default;
            slim.num = legacy.num;
            slim.snr = legacy.snr;
            slim.last_heard = legacy.last_heard;
            slim.channel = legacy.channel;
            slim.has_hops_away = legacy.has_hops_away;
            slim.hops_away = legacy.hops_away;
            slim.next_hop = legacy.next_hop;
            // v24 assigned bits 0..10 only; anything above is noise and must not arrive as RF-hear
            // state or a slot fingerprint (see NODEINFO_BITFIELD_HEARD_SLOT_SHIFT).
            slim.bitfield = legacy.bitfield & (NODEINFO_BITFIELD_HAS_RF_HEAR_MASK - 1);
            if (legacy.via_mqtt)
                slim.bitfield |= NODEINFO_BITFIELD_VIA_MQTT_MASK;
            if (legacy.is_favorite)
                slim.bitfield |= NODEINFO_BITFIELD_IS_FAVORITE_MASK;
            if (legacy.is_ignored)
                slim.bitfield |= NODEINFO_BITFIELD_IS_IGNORED_MASK;
            if (legacy.has_user) {
                slim.bitfield |= NODEINFO_BITFIELD_HAS_USER_MASK;
                strncpy(slim.long_name, legacy.user.long_name, sizeof(slim.long_name));
                slim.long_name[sizeof(slim.long_name) - 1] = '\0';
                sanitizeUtf8(slim.long_name, sizeof(slim.long_name)); // replace bad bytes so nanopb encode never fails
                strncpy(slim.short_name, legacy.user.short_name, sizeof(slim.short_name));
                slim.short_name[sizeof(slim.short_name) - 1] = '\0';
                sanitizeUtf8(slim.short_name, sizeof(slim.short_name)); // same - v24 names may contain non-UTF-8 bytes
                slim.hw_model = legacy.user.hw_model;
                slim.role = legacy.user.role;
                if (legacy.user.is_licensed)
                    slim.bitfield |= NODEINFO_BITFIELD_IS_LICENSED_MASK;
                slim.public_key.size = legacy.user.public_key.size;
                memcpy(slim.public_key.bytes, legacy.user.public_key.bytes, sizeof(slim.public_key.bytes));
                if (legacy.user.has_is_unmessagable) {
                    slim.bitfield |= NODEINFO_BITFIELD_HAS_IS_UNMESSAGABLE_MASK;
                    if (legacy.user.is_unmessagable)
                        slim.bitfield |= NODEINFO_BITFIELD_IS_UNMESSAGABLE_MASK;
                }
                // macaddr deprecated since 1.2.11 - dropped from slim header.
            }
            nodeDatabase.nodes.push_back(slim);
#if !MESHTASTIC_EXCLUDE_POSITIONDB
            if (legacy.has_position)
                nodePositions[legacy.num] = legacy.position;
#endif
#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
            if (legacy.has_device_metrics)
                nodeTelemetry[legacy.num] = legacy.device_metrics;
#endif
        }
#if !MESHTASTIC_EXCLUDE_POSITIONDB
        posCount = nodePositions.size();
#endif
#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
        telCount = nodeTelemetry.size();
#endif
    }
    nodeDatabase.version = DEVICESTATE_CUR_VER;
    meshNodes = &nodeDatabase.nodes;
    numMeshNodes = nodeDatabase.nodes.size();
    LOG_INFO("Migrated %u nodes from legacy -> v%u (positions: %u, telemetry: %u)", (unsigned)numMeshNodes, DEVICESTATE_CUR_VER,
             (unsigned)posCount, (unsigned)telCount);
    return true;
}

// Encoded maxima before the broadcast_message cut (max_size 101), from develop's generated localonly/module_config
// headers. Remove this block, and its caller in loadFromDisk(), once pre-cut 2.8.0 builds are out of support.
static constexpr size_t kLegacyLocalModuleConfigSize = 1044;
static constexpr size_t kLegacyMeshBeaconConfigSize = 242;

// Copies one length-delimited body, cutting field 4 (broadcast_message) to what the current struct holds.
static bool truncateBeaconBody(const uint8_t *in, size_t inLen, pb_ostream_t *out, bool &cut)
{
    constexpr size_t maxMessage = sizeof(meshtastic_ModuleConfig_MeshBeaconConfig::broadcast_message) - 1;
    pb_istream_t is = pb_istream_from_buffer(in, inLen);
    while (is.bytes_left) {
        const size_t start = inLen - is.bytes_left;
        pb_wire_type_t wireType;
        uint32_t tag;
        bool eof;
        if (!pb_decode_tag(&is, &wireType, &tag, &eof))
            return false;
        if (tag == meshtastic_ModuleConfig_MeshBeaconConfig_broadcast_message_tag && wireType == PB_WT_STRING) {
            uint32_t len;
            if (!pb_decode_varint32(&is, &len) || len > is.bytes_left)
                return false;
            const char *msg = reinterpret_cast<const char *>(in + (inLen - is.bytes_left));
            const size_t keep = utf8TruncateLen(msg, len, maxMessage);
            if (keep < len) {
                LOG_WARN("moduleConfig: beacon message %u bytes, over the %u limit, cut to %u", (unsigned)len,
                         (unsigned)maxMessage, (unsigned)keep);
                cut = true;
            }
            if (!pb_encode_tag(out, PB_WT_STRING, tag) || !pb_encode_string(out, (const pb_byte_t *)msg, keep) ||
                !pb_read(&is, NULL, len))
                return false;
        } else {
            if (!pb_skip_field(&is, wireType) || !pb_write(out, in + start, (inLen - is.bytes_left) - start))
                return false;
        }
    }
    return true;
}

bool truncateLegacyBeaconMessage(const uint8_t *in, size_t inLen, uint8_t *out, size_t outCap, size_t &outLen)
{
    pb_istream_t is = pb_istream_from_buffer(in, inLen);
    pb_ostream_t os = pb_ostream_from_buffer(out, outCap);
    bool cut = false;
    while (is.bytes_left) {
        const size_t start = inLen - is.bytes_left;
        pb_wire_type_t wireType;
        uint32_t tag;
        bool eof;
        if (!pb_decode_tag(&is, &wireType, &tag, &eof))
            return false;
        if (tag == meshtastic_LocalModuleConfig_mesh_beacon_tag && wireType == PB_WT_STRING) {
            uint32_t len;
            if (!pb_decode_varint32(&is, &len) || len > is.bytes_left)
                return false;
            uint8_t body[kLegacyMeshBeaconConfigSize];
            pb_ostream_t bodyOut = pb_ostream_from_buffer(body, sizeof(body));
            if (!truncateBeaconBody(in + (inLen - is.bytes_left), len, &bodyOut, cut) || !pb_read(&is, NULL, len) ||
                !pb_encode_tag(&os, PB_WT_STRING, tag) || !pb_encode_string(&os, body, bodyOut.bytes_written))
                return false;
        } else {
            if (!pb_skip_field(&is, wireType) || !pb_write(&os, in + start, (inLen - is.bytes_left) - start))
                return false;
        }
    }
    outLen = os.bytes_written;
    return cut;
}

bool NodeDB::migrateLegacyModuleConfig()
{
    // Re-read at the legacy maximum: loadProto() stops at the current one, which a pre-cut file can exceed.
    auto raw = meshtastic_security::make_zeroizing_array(kLegacyLocalModuleConfigSize);
    auto fixed = meshtastic_security::make_zeroizing_array(kLegacyLocalModuleConfigSize);
    if (!raw || !fixed)
        return false;

    size_t rawLen = 0;
#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    if (EncryptedStorage::isEncrypted(moduleConfigFileName)) {
        if (!EncryptedStorage::readAndDecrypt(moduleConfigFileName, raw.get(), kLegacyLocalModuleConfigSize, rawLen))
            return false;
    } else
#endif
    {
#ifdef FSCom
        concurrency::LockGuard g(spiLock);
        auto f = FSCom.open(moduleConfigFileName, FILE_O_READ);
        if (!f)
            return false;
        const int got = f.read(raw.get(), kLegacyLocalModuleConfigSize);
        f.close();
        if (got <= 0)
            return false;
        rawLen = (size_t)got;
#else
        return false;
#endif
    }

    size_t fixedLen = 0;
    if (!truncateLegacyBeaconMessage(raw.get(), rawLen, fixed.get(), kLegacyLocalModuleConfigSize, fixedLen))
        return false;

    pb_istream_t stream = pb_istream_from_buffer(fixed.get(), fixedLen);
    memset(&moduleConfig, 0, sizeof(moduleConfig));
    if (!pb_decode(&stream, &meshtastic_LocalModuleConfig_msg, &moduleConfig)) {
        LOG_ERROR("moduleConfig: still undecodable after the beacon message cut: %s", PB_GET_ERROR(&stream));
        return false;
    }
    LOG_WARN("moduleConfig: migrated a save with an over-long beacon message");
    return true;
}
