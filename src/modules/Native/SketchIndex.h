#pragma once

#include "PinSketch.h"

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * The set layer between stored objects and a PinSketch: what a node summarises, and what two nodes
 * compare.
 *
 * An object is identified on the chain by its 16-byte message hash. A sketch costs four bytes per
 * unit of capacity, so members are the 32-bit short ID instead - a truncated hash of that object
 * hash, computed identically by every node with no shared key, no negotiation and no session. That
 * is what lets one broadcast summary be compared by everyone who hears it.
 *
 * Truncation collides, so a short ID cannot carry correctness. Each object also contributes 64
 * domain-separated bits to a per-bucket checksum, XOR-accumulated so ingest and retention are one
 * XOR rather than a pass over the bucket. Two sketches that cancel prove nothing on their own; two
 * checksums that match do. The checksum is not an extra: without it a colliding pair cancels inside
 * the sketch and the object is silently lost, and an unsalted 32-bit short ID can be ground out in
 * under a second. It also covers the honest case - PinSketch decodes an over-capacity difference to
 * a wrong set at a rate near 1/c!, which no re-encoding detects.
 *
 * Buckets keep the difference small enough to resolve in one frame. They close on the chain
 * counter, not on time: a count-based boundary is one both sides derive from the data itself, and it
 * bounds members per bucket on any mesh rather than depending on a guessed message rate.
 */
namespace sfppsr
{

// The sketch member. Derived from the object hash, so it is stable across nodes and stored beside
// the object rather than recomputed.
pinsketch::Element shortId(const uint8_t *objectId, size_t len);

// The object's contribution to its bucket checksum. Domain-separated from the short ID so that
// grinding a short-ID collision does not also produce a matching contribution.
uint64_t checksumContribution(const uint8_t *objectId, size_t len);

// A full bucket's sketch is 128 bytes, which leaves room for the advert envelope in one frame.
constexpr uint32_t kBucketObjects = 32;

// Chain counters are 1-based; counter 0 means the object is not on the chain yet and has no bucket.
bool bucketOf(uint32_t chainCounter, uint32_t &bucketOut);

/**
 * One bucket's summary: what an advert carries, and what a peer's advert is compared against.
 */
class BucketSummary
{
  public:
    explicit BucketSummary(size_t capacity = kBucketObjects) : members(capacity) {}

    // Ingest and retention. Both identifiers are derived here; the store keeps them so a reload
    // does not rehash.
    void add(const uint8_t *objectId, size_t len);
    void remove(const uint8_t *objectId, size_t len);

    void add(pinsketch::Element id, uint64_t contribution);
    void remove(pinsketch::Element id, uint64_t contribution);

    uint32_t count() const { return objects; }
    uint64_t checksum() const { return checksumAccumulator; }
    const pinsketch::Sketch &sketch() const { return members; }

    // Recovers the symmetric difference against a peer's sketch of the same bucket. False when the
    // two differ by more than the capacity, which is the signal to escalate to enumeration. A true
    // return is not proof of anything until the objects arrive and the checksums match.
    bool difference(const pinsketch::Sketch &peer, std::vector<pinsketch::Element> &out) const;

    void clear();

  private:
    pinsketch::Sketch members;
    uint64_t checksumAccumulator = 0;
    uint32_t objects = 0;
};

} // namespace sfppsr
