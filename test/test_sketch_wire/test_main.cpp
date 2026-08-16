#include "configuration.h" // for the extern "C" setup()/loop() the portduino runner links against
#include "modules/Native/SketchWire.h"

#include <unity.h>
#include <vector>

using pinsketch::Element;
using sfppsr::Advert;
using sfppsr::BucketSummary;
using sfppsr::Scope;
using sfppsr::ShortIdList;

void setUp(void) {}
void tearDown(void) {}

static Scope aScope(uint32_t bucket = 7)
{
    Scope s;
    s.rootPrefix[0] = 0xDE;
    s.rootPrefix[1] = 0xAD;
    s.rootPrefix[2] = 0xBE;
    s.rootPrefix[3] = 0xEF;
    s.rootPrefixLen = 4;
    s.bucket = bucket;
    return s;
}

// Deterministic 16-byte object hashes, so a failure is always reproducible.
static void nthObject(uint32_t n, uint8_t out[16])
{
    for (size_t i = 0; i < 16; i += 4) {
        uint32_t x = (n + (uint32_t)i) * 2654435761u + 0x9E3779B9u;
        x ^= x >> 15;
        x *= 0x85EBCA6Bu;
        out[i] = (uint8_t)(x >> 24);
        out[i + 1] = (uint8_t)(x >> 16);
        out[i + 2] = (uint8_t)(x >> 8);
        out[i + 3] = (uint8_t)x;
    }
}

static BucketSummary summaryOf(uint32_t first, uint32_t count, size_t capacity = sfppsr::kBucketObjects)
{
    BucketSummary s(capacity);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t id[16];
        nthObject(first + i, id);
        s.add(id, sizeof(id));
    }
    return s;
}

static std::vector<Element> someIds(size_t n)
{
    std::vector<Element> ids;
    for (size_t i = 0; i < n; i++)
        ids.push_back((Element)(0x1000 + i * 7));
    return ids;
}

// --- Round trips ---

void test_anAdvertCarriesTheWholeSummary()
{
    const BucketSummary mine = summaryOf(0, 12);
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildAdvert(aScope(9), mine, msg));

    Advert heard;
    TEST_ASSERT_TRUE(sfppsr::parseAdvert(msg, heard));
    TEST_ASSERT_EQUAL_UINT32(9, heard.scope.bucket);
    TEST_ASSERT_EQUAL_UINT(4, heard.scope.rootPrefixLen);
    TEST_ASSERT_EQUAL_UINT32(12, heard.count);
    TEST_ASSERT_TRUE(mine.checksum() == heard.checksum);
    TEST_ASSERT_EQUAL_UINT(mine.sketch().serializedSize(), heard.sketch.serializedSize());
}

void test_anAdvertResolvesADifferenceAfterTheRoundTrip()
{
    // The point of the whole exchange: what the peer sends is enough to name what this node lacks.
    const BucketSummary peer = summaryOf(0, 15);
    const BucketSummary mine = summaryOf(0, 13);

    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildAdvert(aScope(), peer, msg));

    Advert heard;
    TEST_ASSERT_TRUE(sfppsr::parseAdvert(msg, heard));

    std::vector<Element> difference;
    TEST_ASSERT_TRUE(mine.difference(heard.sketch, difference));
    TEST_ASSERT_EQUAL_UINT(2, difference.size());
    TEST_ASSERT_FALSE(mine.checksum() == heard.checksum);

    // And the request naming them round-trips too.
    meshtastic_StoreForwardPlusPlus request;
    TEST_ASSERT_TRUE(sfppsr::buildItemRequest(aScope(), difference, request));

    ShortIdList wanted;
    TEST_ASSERT_TRUE(sfppsr::parseShortIds(request, wanted));
    TEST_ASSERT_EQUAL_UINT(2, wanted.ids.size());
    TEST_ASSERT_EQUAL_UINT32(difference[0], wanted.ids[0]);
    TEST_ASSERT_EQUAL_UINT32(difference[1], wanted.ids[1]);
}

void test_anEnumProvideCarriesItsFramePosition()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildEnumProvide(aScope(3), someIds(40), 2, 5, msg));

    ShortIdList page;
    TEST_ASSERT_TRUE(sfppsr::parseShortIds(msg, page));
    TEST_ASSERT_EQUAL_UINT(40, page.ids.size());
    TEST_ASSERT_EQUAL_UINT32(2, page.frameIndex);
    TEST_ASSERT_EQUAL_UINT32(5, page.frameTotal);
}

void test_aSingleFrameSpendsNoFrameField()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildEnumProvide(aScope(), someIds(3), 0, 1, msg));
    TEST_ASSERT_EQUAL_UINT32(0, msg.sr_frame);

    ShortIdList page;
    TEST_ASSERT_TRUE(sfppsr::parseShortIds(msg, page));
    TEST_ASSERT_EQUAL_UINT32(0, page.frameIndex);
    TEST_ASSERT_EQUAL_UINT32(1, page.frameTotal);
}

void test_anEnumRequestIsScopeAndBucketOnly()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildEnumRequest(aScope(41), msg));
    TEST_ASSERT_EQUAL_UINT32(41, msg.chain_count);
    TEST_ASSERT_EQUAL_UINT(0, msg.message.size);
    TEST_ASSERT_EQUAL_UINT(0, msg.sr_short_ids_count);

    Scope scope;
    TEST_ASSERT_TRUE(sfppsr::parseScope(msg, scope));
    TEST_ASSERT_EQUAL_UINT32(41, scope.bucket);
}

// --- Version ---

void test_anUnimplementedVersionIsDiscarded()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildAdvert(aScope(), summaryOf(0, 4), msg));

    Advert heard;
    msg.sr_version = sfppsr::kDerivationVersion + 1;
    TEST_ASSERT_FALSE(sfppsr::parseAdvert(msg, heard));

    // Absent is not "version 1" - a derivation nobody stated is one nobody can check.
    msg.sr_version = 0;
    TEST_ASSERT_FALSE(sfppsr::parseAdvert(msg, heard));
}

// --- The overloading guard, both directions ---

void test_aSetReconciliationMessageMayNotCarryChainFields()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildAdvert(aScope(), summaryOf(0, 4), msg));

    Advert heard;
    msg.commit_hash.size = 16;
    TEST_ASSERT_FALSE(sfppsr::parseAdvert(msg, heard));

    msg.commit_hash.size = 0;
    msg.message_hash.size = 16;
    TEST_ASSERT_FALSE(sfppsr::parseAdvert(msg, heard));
}

void test_anItemProvideMayCarryTheChainsOwnFields()
{
    // The one exemption: this type moves an object in the chain protocol's encoding, so a message
    // hash on it is correct rather than suspicious.
    meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_default;
    msg.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_PROVIDE;
    msg.sr_version = sfppsr::kDerivationVersion;
    msg.root_hash.size = 4;
    msg.chain_count = 5;
    msg.message_hash.size = 16;

    Scope scope;
    TEST_ASSERT_TRUE(sfppsr::parseScope(msg, scope));
    TEST_ASSERT_EQUAL_UINT32(5, scope.bucket);
}

void test_aChainMessageCarryingSetFieldsIsRecognisable()
{
    meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_default;
    msg.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_REQUEST;
    msg.chain_count = 12;
    TEST_ASSERT_FALSE(sfppsr::hasSetReconciliationFields(msg));
    TEST_ASSERT_FALSE(sfppsr::isSetReconciliationType(msg.sfpp_message_type));

    // chain_count is shared, so it alone proves nothing; sr_checksum is ours and cannot be innocent.
    msg.sr_checksum = 1;
    TEST_ASSERT_TRUE(sfppsr::hasSetReconciliationFields(msg));
}

void test_theWrongTypeIsNeverParsedAsAnother()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildItemRequest(aScope(), someIds(3), msg));

    Advert heard;
    TEST_ASSERT_FALSE(sfppsr::parseAdvert(msg, heard));

    TEST_ASSERT_TRUE(sfppsr::buildAdvert(aScope(), summaryOf(0, 4), msg));
    ShortIdList page;
    TEST_ASSERT_FALSE(sfppsr::parseShortIds(msg, page));
}

// --- Malformed input ---

void test_aTruncatedSketchIsRefusedRatherThanDecoded()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildAdvert(aScope(), summaryOf(0, 10), msg));

    Advert heard;
    msg.message.size -= 1; // no longer a whole number of elements
    TEST_ASSERT_FALSE(sfppsr::parseAdvert(msg, heard));

    msg.message.size = 0;
    TEST_ASSERT_FALSE(sfppsr::parseAdvert(msg, heard));
}

void test_aSketchOverTheCapacityBoundIsRefused()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildAdvert(aScope(), summaryOf(0, 10), msg));

    Advert heard;
    msg.message.size = sfppsr::kMaxSketchBytes + 4;
    TEST_ASSERT_FALSE(sfppsr::parseAdvert(msg, heard));
}

void test_anOversizedSketchIsNeverBuilt()
{
    // Capacity 40 is 160 bytes, past the frame budget the bound exists to keep.
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_FALSE(sfppsr::buildAdvert(aScope(), summaryOf(0, 10, 40), msg));
}

void test_aZeroShortIdIsRefusedBothWays()
{
    std::vector<Element> ids = someIds(3);
    ids[1] = 0;

    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_FALSE(sfppsr::buildItemRequest(aScope(), ids, msg));

    TEST_ASSERT_TRUE(sfppsr::buildItemRequest(aScope(), someIds(3), msg));
    msg.sr_short_ids[1] = 0;
    ShortIdList wanted;
    TEST_ASSERT_FALSE(sfppsr::parseShortIds(msg, wanted));
}

void test_anEmptyOrOverfullIdListIsRefused()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_FALSE(sfppsr::buildItemRequest(aScope(), std::vector<Element>(), msg));
    TEST_ASSERT_FALSE(sfppsr::buildItemRequest(aScope(), someIds(sfppsr::kMaxShortIds + 1), msg));
}

void test_aFragmentPastItsTotalIsRefused()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_FALSE(sfppsr::buildEnumProvide(aScope(), someIds(3), 5, 5, msg));
    TEST_ASSERT_FALSE(sfppsr::buildEnumProvide(aScope(), someIds(3), 0, 0, msg));

    TEST_ASSERT_TRUE(sfppsr::buildEnumProvide(aScope(), someIds(3), 1, 3, msg));
    msg.sr_frame = 3 | 3 << 8; // index == total
    ShortIdList page;
    TEST_ASSERT_FALSE(sfppsr::parseShortIds(msg, page));
}

void test_aScopeTooShortToResolveIsRefused()
{
    Scope shortScope = aScope();
    shortScope.rootPrefixLen = sfppsr::kRootPrefixBytes - 1;

    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_FALSE(sfppsr::buildAdvert(shortScope, summaryOf(0, 4), msg));

    TEST_ASSERT_TRUE(sfppsr::buildAdvert(aScope(), summaryOf(0, 4), msg));
    msg.root_hash.size = sfppsr::kRootPrefixBytes - 1;
    Advert heard;
    TEST_ASSERT_FALSE(sfppsr::parseAdvert(msg, heard));
}

void test_fieldsBelongingToAnotherTypeAreRefused()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildAdvert(aScope(), summaryOf(0, 4), msg));
    msg.sr_short_ids_count = 1;
    msg.sr_short_ids[0] = 5;
    Advert heard;
    TEST_ASSERT_FALSE(sfppsr::parseAdvert(msg, heard));

    TEST_ASSERT_TRUE(sfppsr::buildItemRequest(aScope(), someIds(2), msg));
    msg.sr_checksum = 99;
    ShortIdList wanted;
    TEST_ASSERT_FALSE(sfppsr::parseShortIds(msg, wanted));
}

void setup()
{
    UNITY_BEGIN();

    // Round trips
    RUN_TEST(test_anAdvertCarriesTheWholeSummary);
    RUN_TEST(test_anAdvertResolvesADifferenceAfterTheRoundTrip);
    RUN_TEST(test_anEnumProvideCarriesItsFramePosition);
    RUN_TEST(test_aSingleFrameSpendsNoFrameField);
    RUN_TEST(test_anEnumRequestIsScopeAndBucketOnly);

    // Version
    RUN_TEST(test_anUnimplementedVersionIsDiscarded);

    // Overloading guard
    RUN_TEST(test_aSetReconciliationMessageMayNotCarryChainFields);
    RUN_TEST(test_anItemProvideMayCarryTheChainsOwnFields);
    RUN_TEST(test_aChainMessageCarryingSetFieldsIsRecognisable);
    RUN_TEST(test_theWrongTypeIsNeverParsedAsAnother);

    // Malformed input
    RUN_TEST(test_aTruncatedSketchIsRefusedRatherThanDecoded);
    RUN_TEST(test_aSketchOverTheCapacityBoundIsRefused);
    RUN_TEST(test_anOversizedSketchIsNeverBuilt);
    RUN_TEST(test_aZeroShortIdIsRefusedBothWays);
    RUN_TEST(test_anEmptyOrOverfullIdListIsRefused);
    RUN_TEST(test_aFragmentPastItsTotalIsRefused);
    RUN_TEST(test_aScopeTooShortToResolveIsRefused);
    RUN_TEST(test_fieldsBelongingToAnotherTypeAreRefused);

    exit(UNITY_END());
}

void loop() {}
