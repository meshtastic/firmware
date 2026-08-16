#include "SketchWire.h"

#include <cstring>

namespace sfppsr
{

namespace
{

bool validScope(const Scope &scope)
{
    return scope.rootPrefixLen >= kRootPrefixBytes && scope.rootPrefixLen <= sizeof(scope.rootPrefix);
}

void writeScope(const Scope &scope, meshtastic_StoreForwardPlusPlus &out)
{
    out.sr_version = kDerivationVersion;
    out.root_hash.size = scope.rootPrefixLen;
    memcpy(out.root_hash.bytes, scope.rootPrefix, scope.rootPrefixLen);
    out.chain_count = scope.bucket;
}

uint32_t packFrame(uint8_t index, uint8_t total)
{
    return (uint32_t)index | (uint32_t)total << 8;
}

// A single frame is the absent-field case, so it never costs the two bytes.
bool readFrame(const meshtastic_StoreForwardPlusPlus &in, uint8_t &index, uint8_t &total)
{
    if (in.sr_frame == 0) {
        index = 0;
        total = 1;
        return true;
    }
    index = in.sr_frame & 0xFF;
    total = in.sr_frame >> 8 & 0xFF;
    return total != 0 && index < total;
}

// The chain's own fields have no meaning on a set-reconciliation message, and one carrying both is
// either malformed or an attempt to have a single packet read differently by two handlers.
bool carriesChainFields(const meshtastic_StoreForwardPlusPlus &in)
{
    return in.message_hash.size != 0 || in.commit_hash.size != 0;
}

bool commonHeaderValid(const meshtastic_StoreForwardPlusPlus &in, Scope &scope)
{
    if (in.sr_version != kDerivationVersion)
        return false;
    if (in.root_hash.size < kRootPrefixBytes || in.root_hash.size > sizeof(scope.rootPrefix))
        return false;

    scope.rootPrefixLen = in.root_hash.size;
    memcpy(scope.rootPrefix, in.root_hash.bytes, in.root_hash.size);
    scope.bucket = in.chain_count;
    return true;
}

bool readShortIds(const meshtastic_StoreForwardPlusPlus &in, std::vector<pinsketch::Element> &out)
{
    if (in.sr_short_ids_count == 0 || in.sr_short_ids_count > kMaxShortIds)
        return false;

    out.clear();
    out.reserve(in.sr_short_ids_count);
    for (pb_size_t i = 0; i < in.sr_short_ids_count; i++) {
        // Zero is not a representable sketch member, so it can only be malformed.
        if (in.sr_short_ids[i] == 0)
            return false;
        out.push_back(in.sr_short_ids[i]);
    }
    return true;
}

bool writeShortIds(const std::vector<pinsketch::Element> &ids, meshtastic_StoreForwardPlusPlus &out)
{
    if (ids.empty() || ids.size() > kMaxShortIds)
        return false;

    for (size_t i = 0; i < ids.size(); i++) {
        if (ids[i] == 0)
            return false;
        out.sr_short_ids[i] = ids[i];
    }
    out.sr_short_ids_count = (pb_size_t)ids.size();
    return true;
}

} // namespace

bool isSetReconciliationType(meshtastic_StoreForwardPlusPlus_SFPP_message_type type)
{
    return type >= meshtastic_StoreForwardPlusPlus_SFPP_message_type_ADVERT &&
           type <= meshtastic_StoreForwardPlusPlus_SFPP_message_type_GONE;
}

bool hasSetReconciliationFields(const meshtastic_StoreForwardPlusPlus &msg)
{
    return msg.sr_version != 0 || msg.sr_count != 0 || msg.sr_frame != 0 || msg.sr_checksum != 0 || msg.sr_short_ids_count != 0 ||
           msg.sr_signature.size != 0;
}

bool buildAdvert(const Scope &scope, const BucketSummary &summary, meshtastic_StoreForwardPlusPlus &out)
{
    const size_t sketchBytes = summary.sketch().serializedSize();
    if (!validScope(scope) || sketchBytes == 0 || sketchBytes > kMaxSketchBytes || sketchBytes > sizeof(out.message.bytes))
        return false;

    out = meshtastic_StoreForwardPlusPlus_init_default;
    out.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ADVERT;
    writeScope(scope, out);
    out.sr_count = summary.count();
    out.sr_checksum = summary.checksum();
    summary.sketch().serialize(out.message.bytes);
    out.message.size = sketchBytes;
    return true;
}

bool buildItemRequest(const Scope &scope, const std::vector<pinsketch::Element> &ids, meshtastic_StoreForwardPlusPlus &out)
{
    if (!validScope(scope))
        return false;

    out = meshtastic_StoreForwardPlusPlus_init_default;
    out.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST;
    writeScope(scope, out);
    return writeShortIds(ids, out);
}

bool buildEnumRequest(const Scope &scope, meshtastic_StoreForwardPlusPlus &out)
{
    if (!validScope(scope))
        return false;

    out = meshtastic_StoreForwardPlusPlus_init_default;
    out.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST;
    writeScope(scope, out);
    return true;
}

bool buildEnumProvide(const Scope &scope, const std::vector<pinsketch::Element> &ids, uint8_t frameIndex, uint8_t frameTotal,
                      meshtastic_StoreForwardPlusPlus &out)
{
    if (!validScope(scope) || frameTotal == 0 || frameIndex >= frameTotal)
        return false;

    out = meshtastic_StoreForwardPlusPlus_init_default;
    out.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE;
    writeScope(scope, out);
    if (frameTotal > 1)
        out.sr_frame = packFrame(frameIndex, frameTotal);
    return writeShortIds(ids, out);
}

bool parseScope(const meshtastic_StoreForwardPlusPlus &in, Scope &out)
{
    if (!isSetReconciliationType(in.sfpp_message_type))
        return false;
    // ITEM_PROVIDE moves an object in the chain protocol's own encoding, so its hash fields are
    // exactly what that type should carry. Every other type populating them is malformed.
    if (in.sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_PROVIDE && carriesChainFields(in))
        return false;
    return commonHeaderValid(in, out);
}

bool parseAdvert(const meshtastic_StoreForwardPlusPlus &in, Advert &out)
{
    if (in.sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ADVERT)
        return false;
    if (!parseScope(in, out.scope))
        return false;
    if (in.sr_short_ids_count != 0 || in.sr_signature.size != 0)
        return false;
    if (!readFrame(in, out.frameIndex, out.frameTotal))
        return false;

    // deserialize refuses a length that is not a whole number of elements, which is what a truncated
    // sketch looks like. Decoding one produces a wrong answer rather than an error, so it is refused
    // here rather than downstream.
    if (in.message.size == 0 || in.message.size > kMaxSketchBytes)
        return false;
    if (!out.sketch.deserialize(in.message.bytes, in.message.size))
        return false;

    out.count = in.sr_count;
    out.checksum = in.sr_checksum;
    return true;
}

bool parseShortIds(const meshtastic_StoreForwardPlusPlus &in, ShortIdList &out)
{
    if (in.sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST &&
        in.sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE)
        return false;
    if (!parseScope(in, out.scope))
        return false;
    if (in.message.size != 0 || in.sr_checksum != 0)
        return false;
    if (!readFrame(in, out.frameIndex, out.frameTotal))
        return false;

    return readShortIds(in, out.ids);
}

} // namespace sfppsr
