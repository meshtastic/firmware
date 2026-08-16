#include "configuration.h" // for the extern "C" setup()/loop() the portduino runner links against
#include "modules/Native/SketchIndex.h"

#include <set>
#include <unity.h>
#include <vector>

using pinsketch::Element;
using sfppsr::BucketSummary;

void setUp(void) {}
void tearDown(void) {}

// A stand-in for the 16-byte message hash, deterministic so a failure is always reproducible.
struct ObjectId {
    uint8_t bytes[16];
};

static ObjectId nthObject(uint32_t n)
{
    ObjectId o = {};
    for (size_t i = 0; i < sizeof(o.bytes); i += 4) {
        uint32_t x = (n + (uint32_t)i) * 2654435761u + 0x9E3779B9u;
        x ^= x >> 15;
        x *= 0x85EBCA6Bu;
        o.bytes[i] = (uint8_t)(x >> 24);
        o.bytes[i + 1] = (uint8_t)(x >> 16);
        o.bytes[i + 2] = (uint8_t)(x >> 8);
        o.bytes[i + 3] = (uint8_t)x;
    }
    return o;
}

static Element shortIdOf(const ObjectId &o)
{
    return sfppsr::shortId(o.bytes, sizeof(o.bytes));
}

static uint64_t contributionOf(const ObjectId &o)
{
    return sfppsr::checksumContribution(o.bytes, sizeof(o.bytes));
}

static BucketSummary summaryOf(uint32_t first, uint32_t count, size_t capacity = sfppsr::kBucketObjects)
{
    BucketSummary s(capacity);
    for (uint32_t i = 0; i < count; i++) {
        const ObjectId o = nthObject(first + i);
        s.add(o.bytes, sizeof(o.bytes));
    }
    return s;
}

// --- Identifiers ---

void test_shortIdIsDeterministicAndNonZero()
{
    for (uint32_t i = 0; i < 256; i++) {
        const ObjectId o = nthObject(i);
        const Element id = shortIdOf(o);
        TEST_ASSERT_TRUE(id != 0);
        TEST_ASSERT_EQUAL_UINT32(id, shortIdOf(o));
    }
}

void test_distinctObjectsGetDistinctShortIds()
{
    // Not a collision-rate claim - 2000 draws at b=32 collide with probability ~5e-4, so a failure
    // here means the derivation lost entropy, not that the birthday bound was reached.
    std::set<Element> seen;
    for (uint32_t i = 0; i < 2000; i++) {
        const ObjectId o = nthObject(i);
        TEST_ASSERT_TRUE(seen.insert(shortIdOf(o)).second);
    }
}

void test_theTwoIdentifiersAreDomainSeparated()
{
    for (uint32_t i = 0; i < 64; i++) {
        const ObjectId o = nthObject(i);
        TEST_ASSERT_TRUE(shortIdOf(o) != (uint32_t)(contributionOf(o) >> 32));
    }
}

void test_oneFlippedBitChangesBothIdentifiers()
{
    ObjectId o = nthObject(7);
    const Element id = shortIdOf(o);
    const uint64_t contribution = contributionOf(o);

    o.bytes[9] ^= 0x01;
    TEST_ASSERT_TRUE(id != shortIdOf(o));
    TEST_ASSERT_TRUE(contribution != contributionOf(o));
}

void test_anAbsentObjectIdYieldsNoIdentifier()
{
    const ObjectId o = nthObject(1);
    TEST_ASSERT_EQUAL_UINT32(0, sfppsr::shortId(nullptr, sizeof(o.bytes)));
    TEST_ASSERT_EQUAL_UINT32(0, sfppsr::shortId(o.bytes, 0));
    TEST_ASSERT_TRUE(sfppsr::checksumContribution(nullptr, sizeof(o.bytes)) == 0);
    TEST_ASSERT_TRUE(sfppsr::checksumContribution(o.bytes, 0) == 0);
}

// --- Buckets ---

void test_anObjectOffTheChainHasNoBucket()
{
    uint32_t bucket = 0xFFFFFFFF;
    TEST_ASSERT_FALSE(sfppsr::bucketOf(0, bucket));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, bucket);
}

void test_bucketsCloseEveryFixedNumberOfObjects()
{
    const uint32_t n = sfppsr::kBucketObjects;
    uint32_t bucket = 0;

    TEST_ASSERT_TRUE(sfppsr::bucketOf(1, bucket));
    TEST_ASSERT_EQUAL_UINT32(0, bucket);
    TEST_ASSERT_TRUE(sfppsr::bucketOf(n, bucket));
    TEST_ASSERT_EQUAL_UINT32(0, bucket);
    TEST_ASSERT_TRUE(sfppsr::bucketOf(n + 1, bucket));
    TEST_ASSERT_EQUAL_UINT32(1, bucket);
    TEST_ASSERT_TRUE(sfppsr::bucketOf(2 * n, bucket));
    TEST_ASSERT_EQUAL_UINT32(1, bucket);
    TEST_ASSERT_TRUE(sfppsr::bucketOf(2 * n + 1, bucket));
    TEST_ASSERT_EQUAL_UINT32(2, bucket);
}

void test_aFullBucketSketchFitsOneFrame()
{
    // 128 bytes at capacity 32, leaving room for the advert envelope inside a 237-byte payload.
    const BucketSummary full = summaryOf(0, sfppsr::kBucketObjects);
    TEST_ASSERT_EQUAL_UINT(4 * sfppsr::kBucketObjects, full.sketch().serializedSize());
    TEST_ASSERT_TRUE(full.sketch().serializedSize() + 16 < 237);
}

// --- Summary bookkeeping ---

void test_countAndChecksumTrackMembership()
{
    BucketSummary s;
    uint64_t expected = 0;
    for (uint32_t i = 0; i < 5; i++) {
        const ObjectId o = nthObject(i);
        s.add(o.bytes, sizeof(o.bytes));
        expected ^= contributionOf(o);
    }
    TEST_ASSERT_EQUAL_UINT32(5, s.count());
    TEST_ASSERT_TRUE(expected == s.checksum());

    const ObjectId dropped = nthObject(2);
    s.remove(dropped.bytes, sizeof(dropped.bytes));
    TEST_ASSERT_EQUAL_UINT32(4, s.count());
    TEST_ASSERT_TRUE((expected ^ contributionOf(dropped)) == s.checksum());
}

void test_theChecksumDoesNotDependOnIngestOrder()
{
    BucketSummary forward, backward;
    for (uint32_t i = 0; i < 12; i++) {
        const ObjectId o = nthObject(i);
        forward.add(o.bytes, sizeof(o.bytes));
        const ObjectId r = nthObject(11 - i);
        backward.add(r.bytes, sizeof(r.bytes));
    }
    TEST_ASSERT_TRUE(forward.checksum() == backward.checksum());
    TEST_ASSERT_EQUAL_UINT32(forward.count(), backward.count());
}

void test_removingEverythingLeavesAnEmptyBucket()
{
    BucketSummary s;
    for (uint32_t i = 0; i < 8; i++) {
        const ObjectId o = nthObject(i);
        s.add(o.bytes, sizeof(o.bytes));
    }
    for (uint32_t i = 0; i < 8; i++) {
        const ObjectId o = nthObject(i);
        s.remove(o.bytes, sizeof(o.bytes));
    }
    TEST_ASSERT_EQUAL_UINT32(0, s.count());
    TEST_ASSERT_TRUE(s.checksum() == 0);
    TEST_ASSERT_TRUE(s.sketch().empty());
}

void test_reloadingFromStoredIdentifiersMatchesIngest()
{
    // The store keeps both identifiers, so a restart rebuilds the summary without rehashing.
    BucketSummary ingested, reloaded;
    for (uint32_t i = 0; i < 10; i++) {
        const ObjectId o = nthObject(i);
        ingested.add(o.bytes, sizeof(o.bytes));
        reloaded.add(shortIdOf(o), contributionOf(o));
    }
    TEST_ASSERT_TRUE(ingested.checksum() == reloaded.checksum());
    TEST_ASSERT_EQUAL_UINT32(ingested.count(), reloaded.count());

    std::vector<Element> difference;
    TEST_ASSERT_TRUE(ingested.difference(reloaded.sketch(), difference));
    TEST_ASSERT_EQUAL_UINT(0, difference.size());
}

// --- Reconciliation ---

void test_bucketsHoldingTheSameObjectsShowNoDifference()
{
    const BucketSummary mine = summaryOf(0, 20);
    const BucketSummary peer = summaryOf(0, 20);

    std::vector<Element> difference;
    TEST_ASSERT_TRUE(mine.difference(peer.sketch(), difference));
    TEST_ASSERT_EQUAL_UINT(0, difference.size());
    TEST_ASSERT_TRUE(mine.checksum() == peer.checksum());
}

void test_theDifferenceNamesExactlyTheObjectsOneSideIsMissing()
{
    const BucketSummary mine = summaryOf(0, 18);
    const BucketSummary peer = summaryOf(0, 21); // holds three more

    std::set<Element> expected;
    for (uint32_t i = 18; i < 21; i++)
        expected.insert(shortIdOf(nthObject(i)));

    std::vector<Element> difference;
    TEST_ASSERT_TRUE(mine.difference(peer.sketch(), difference));
    TEST_ASSERT_EQUAL_UINT(3, difference.size());
    for (Element e : difference)
        TEST_ASSERT_TRUE(expected.count(e) == 1);
    TEST_ASSERT_FALSE(mine.checksum() == peer.checksum());
}

void test_costFollowsTheDifferenceNotTheBucketSize()
{
    // Full buckets on both sides, differing by one object: the capacity spent is still one.
    BucketSummary mine = summaryOf(0, sfppsr::kBucketObjects, 4);
    BucketSummary peer = summaryOf(0, sfppsr::kBucketObjects, 4);
    const ObjectId extra = nthObject(9000);
    peer.add(extra.bytes, sizeof(extra.bytes));

    std::vector<Element> difference;
    TEST_ASSERT_TRUE(mine.difference(peer.sketch(), difference));
    TEST_ASSERT_EQUAL_UINT(1, difference.size());
    TEST_ASSERT_EQUAL_UINT32(shortIdOf(extra), difference[0]);
}

void test_aDifferenceBeyondCapacityIsNotResolved()
{
    // Capacity 8 rather than something smaller: below about 6 an over-capacity difference decodes to
    // a wrong set often enough that this could not assert a clean failure.
    const BucketSummary mine = summaryOf(0, 4, 8);
    const BucketSummary peer = summaryOf(0, 24, 8);

    std::vector<Element> difference;
    TEST_ASSERT_FALSE(mine.difference(peer.sketch(), difference));
    TEST_ASSERT_FALSE(mine.checksum() == peer.checksum());
}

void test_aPeerSketchOfAnotherCapacityIsRejected()
{
    const BucketSummary mine = summaryOf(0, 5, 8);
    const BucketSummary peer = summaryOf(0, 5, 16);

    std::vector<Element> difference;
    TEST_ASSERT_FALSE(mine.difference(peer.sketch(), difference));
}

void test_aShortIdCollisionCancelsInTheSketchButNotInTheChecksum()
{
    // The whole reason the checksum exists: two different objects sharing a short ID cancel, so the
    // sketch reports agreement while one side is missing a message. Only the checksum sees it.
    const ObjectId mineOnly = nthObject(100);
    const ObjectId peerOnly = nthObject(200);
    const Element collided = shortIdOf(mineOnly);

    BucketSummary mine = summaryOf(0, 6);
    BucketSummary peer = summaryOf(0, 6);
    mine.add(collided, contributionOf(mineOnly));
    peer.add(collided, contributionOf(peerOnly));

    std::vector<Element> difference;
    TEST_ASSERT_TRUE(mine.difference(peer.sketch(), difference));
    TEST_ASSERT_EQUAL_UINT(0, difference.size());
    TEST_ASSERT_EQUAL_UINT32(peer.count(), mine.count());
    TEST_ASSERT_FALSE(mine.checksum() == peer.checksum());
}

void test_theSketchIgnoresAnObjectWithNoIdentifier()
{
    BucketSummary s;
    s.add(0u, 0xDEADBEEFu);
    TEST_ASSERT_EQUAL_UINT32(0, s.count());
    TEST_ASSERT_TRUE(s.checksum() == 0);
}

void setup()
{
    UNITY_BEGIN();

    // Identifiers
    RUN_TEST(test_shortIdIsDeterministicAndNonZero);
    RUN_TEST(test_distinctObjectsGetDistinctShortIds);
    RUN_TEST(test_theTwoIdentifiersAreDomainSeparated);
    RUN_TEST(test_oneFlippedBitChangesBothIdentifiers);
    RUN_TEST(test_anAbsentObjectIdYieldsNoIdentifier);

    // Buckets
    RUN_TEST(test_anObjectOffTheChainHasNoBucket);
    RUN_TEST(test_bucketsCloseEveryFixedNumberOfObjects);
    RUN_TEST(test_aFullBucketSketchFitsOneFrame);

    // Summary bookkeeping
    RUN_TEST(test_countAndChecksumTrackMembership);
    RUN_TEST(test_theChecksumDoesNotDependOnIngestOrder);
    RUN_TEST(test_removingEverythingLeavesAnEmptyBucket);
    RUN_TEST(test_reloadingFromStoredIdentifiersMatchesIngest);

    // Reconciliation
    RUN_TEST(test_bucketsHoldingTheSameObjectsShowNoDifference);
    RUN_TEST(test_theDifferenceNamesExactlyTheObjectsOneSideIsMissing);
    RUN_TEST(test_costFollowsTheDifferenceNotTheBucketSize);
    RUN_TEST(test_aDifferenceBeyondCapacityIsNotResolved);
    RUN_TEST(test_aPeerSketchOfAnotherCapacityIsRejected);
    RUN_TEST(test_aShortIdCollisionCancelsInTheSketchButNotInTheChecksum);
    RUN_TEST(test_theSketchIgnoresAnObjectWithNoIdentifier);

    exit(UNITY_END());
}

void loop() {}
