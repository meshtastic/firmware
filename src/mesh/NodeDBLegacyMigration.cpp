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

#include "NodeDB.h"
#include "concurrency/LockGuard.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "mesh/generated/meshtastic/deviceonly_legacy.pb.h"
#include "meshUtils.h"

#include <algorithm>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>

bool meshtastic_NodeDatabase_Legacy_callback(pb_istream_t *istream, pb_ostream_t *ostream, const pb_field_t *field)
{
    const auto *iter = reinterpret_cast<const pb_field_iter_t *>(field);
    if (ostream) {
        const auto *vec = static_cast<const std::vector<meshtastic_NodeInfoLite_Legacy> *>(iter->pData);
        for (auto item : *vec) {
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
            slim.bitfield = legacy.bitfield;
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
