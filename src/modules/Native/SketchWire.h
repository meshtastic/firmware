#pragma once

#include "SketchIndex.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * The set-reconciliation message types, on and off the wire.
 *
 * These share one protobuf with the chain protocol, and three of its fields carry a second meaning
 * here: root_hash is the reconciliation scope, chain_count is the bucket index, and an advert's
 * sketch rides in message. Each pairing is the same quantity named differently rather than a
 * borrowed slot, but the overloading has to be policed in both directions - a set-reconciliation
 * message must not populate the chain's hash fields, and a chain message must not carry these.
 * Otherwise one packet can be read two ways by two handlers.
 *
 * Every parse here is a rejection point, not a convenience. A malformed sketch, a short ID of zero
 * or a fragment index past its total all produce a confident wrong answer downstream rather than an
 * error, so they are refused at the boundary.
 */
namespace sfppsr
{

// Which derivation produced the fields: hash, short-ID width, checksum domain, bucket size, counter
// origin. A receiver that does not implement the stated version discards the message rather than
// parsing it under its own rules. Zero is invalid, not a default.
constexpr uint32_t kDerivationVersion = 1;

// Capacity 32 at four bytes per unit, which is a whole bucket in one frame.
constexpr size_t kMaxSketchBytes = 4 * kBucketObjects;

// Enough to separate the roots one node knows, not a global identifier.
constexpr size_t kRootPrefixBytes = 4;

// A full bucket and more, inside one frame.
constexpr size_t kMaxShortIds = 40;

struct Scope {
    uint8_t rootPrefix[32] = {0};
    size_t rootPrefixLen = 0;
    uint32_t bucket = 0;
};

struct Advert {
    Scope scope;
    uint32_t count = 0;
    uint64_t checksum = 0;
    pinsketch::Sketch sketch;
    uint8_t frameIndex = 0;
    uint8_t frameTotal = 1;
};

struct ShortIdList {
    Scope scope;
    std::vector<pinsketch::Element> ids;
    uint8_t frameIndex = 0;
    uint8_t frameTotal = 1;
};

// Build. Each returns false rather than emitting a message that would be rejected on arrival.
bool buildAdvert(const Scope &scope, const BucketSummary &summary, meshtastic_StoreForwardPlusPlus &out);
bool buildItemRequest(const Scope &scope, const std::vector<pinsketch::Element> &ids, meshtastic_StoreForwardPlusPlus &out);
bool buildEnumRequest(const Scope &scope, meshtastic_StoreForwardPlusPlus &out);
bool buildEnumProvide(const Scope &scope, const std::vector<pinsketch::Element> &ids, uint8_t frameIndex, uint8_t frameTotal,
                      meshtastic_StoreForwardPlusPlus &out);

// Parse. False means the message must be discarded without an answer.
bool parseAdvert(const meshtastic_StoreForwardPlusPlus &in, Advert &out);
bool parseShortIds(const meshtastic_StoreForwardPlusPlus &in, ShortIdList &out);
bool parseScope(const meshtastic_StoreForwardPlusPlus &in, Scope &out);

// True when a message carries any set-reconciliation field. The chain handlers call this to refuse
// types 0-6 that arrive carrying them, which is the other half of the overloading guard.
bool hasSetReconciliationFields(const meshtastic_StoreForwardPlusPlus &msg);

// True for the six set-reconciliation types.
bool isSetReconciliationType(meshtastic_StoreForwardPlusPlus_SFPP_message_type type);

} // namespace sfppsr
