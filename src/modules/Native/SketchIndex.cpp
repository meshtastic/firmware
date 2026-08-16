#include "SketchIndex.h"

#include <SHA256.h>

namespace sfppsr
{

namespace
{

// Domain separation for the checksum half of the pair. Any change to this string changes every
// stored contribution and is a wire break.
const char kChecksumDomain[] = "sfpp-ck-v3";

uint32_t bigEndian32(const uint8_t *b)
{
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | (uint32_t)b[3];
}

uint64_t bigEndian64(const uint8_t *b)
{
    return (uint64_t)bigEndian32(b) << 32 | bigEndian32(b + 4);
}

} // namespace

pinsketch::Element shortId(const uint8_t *objectId, size_t len)
{
    if (objectId == nullptr || len == 0)
        return 0;

    uint8_t digest[SHA256::HASH_SIZE];
    SHA256 hash;
    hash.reset();
    hash.update(objectId, len);
    hash.finalize(digest, sizeof(digest));

    // Zero is not a representable sketch member, so a zero word falls through to the next one. Both
    // sides run the same walk, so the result stays universally comparable.
    for (size_t i = 0; i + sizeof(uint32_t) <= sizeof(digest); i += sizeof(uint32_t)) {
        uint32_t candidate = bigEndian32(digest + i);
        if (candidate != 0)
            return candidate;
    }
    return 1;
}

uint64_t checksumContribution(const uint8_t *objectId, size_t len)
{
    if (objectId == nullptr || len == 0)
        return 0;

    uint8_t digest[SHA256::HASH_SIZE];
    SHA256 hash;
    hash.reset();
    hash.update(kChecksumDomain, sizeof(kChecksumDomain) - 1);
    hash.update(objectId, len);
    hash.finalize(digest, sizeof(digest));

    return bigEndian64(digest);
}

bool bucketOf(uint32_t chainCounter, uint32_t &bucketOut)
{
    if (chainCounter == 0)
        return false;

    bucketOut = (chainCounter - 1) / kBucketObjects;
    return true;
}

void BucketSummary::add(const uint8_t *objectId, size_t len)
{
    add(shortId(objectId, len), checksumContribution(objectId, len));
}

void BucketSummary::remove(const uint8_t *objectId, size_t len)
{
    remove(shortId(objectId, len), checksumContribution(objectId, len));
}

void BucketSummary::add(pinsketch::Element id, uint64_t contribution)
{
    if (!members.add(id))
        return;
    checksumAccumulator ^= contribution;
    objects++;
}

void BucketSummary::remove(pinsketch::Element id, uint64_t contribution)
{
    if (objects == 0 || !members.add(id))
        return;
    checksumAccumulator ^= contribution;
    objects--;
}

bool BucketSummary::difference(const pinsketch::Sketch &peer, std::vector<pinsketch::Element> &out) const
{
    pinsketch::Sketch merged = members;
    if (!merged.merge(peer))
        return false;
    return merged.decode(out);
}

void BucketSummary::clear()
{
    members.clear();
    checksumAccumulator = 0;
    objects = 0;
}

} // namespace sfppsr
