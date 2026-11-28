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

static sfppsr::AdvertScope aStoreScope()
{
    sfppsr::AdvertScope s;
    s.retentionHours = 48;
    s.windowEighths = 4;
    s.storeSize = 4;
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

void test_anAdvertResolvesADifferenceAfterTheRoundTrip()
{
    // The point of the whole exchange: what the peer sends is enough to name what this node lacks.
    const BucketSummary peer = summaryOf(0, 15);
    const BucketSummary mine = summaryOf(0, 13);

    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildWindowAdvert(aScope(), aStoreScope(), peer, msg));

    Advert heard;
    TEST_ASSERT_TRUE(sfppsr::parseWindowAdvert(msg, heard));
    TEST_ASSERT_EQUAL_UINT(4, heard.scope.rootPrefixLen);
    TEST_ASSERT_EQUAL_UINT32(peer.count(), heard.count);
    TEST_ASSERT_EQUAL_UINT32(sfppsr::kWholeSetBucket, heard.scope.bucket);

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
    TEST_ASSERT_EQUAL_UINT32(41, msg.bucket);
    TEST_ASSERT_EQUAL_UINT(0, msg.message.size);
    TEST_ASSERT_EQUAL_UINT(0, msg.sr_short_ids_count);

    Scope scope;
    TEST_ASSERT_TRUE(sfppsr::parseScope(msg, scope));
    TEST_ASSERT_EQUAL_UINT32(41, scope.bucket);
}

// --- The overloading guard, both directions ---

void test_aSetReconciliationMessageMayNotCarryAnObjectHash()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildWindowAdvert(aScope(), aStoreScope(), summaryOf(0, 4), msg));

    Advert heard;
    msg.message_hash.size = 16;
    TEST_ASSERT_FALSE(sfppsr::parseWindowAdvert(msg, heard));
}

void test_anItemProvideMayCarryTheChainsOwnFields()
{
    // The one exemption: this type moves an object in the chain protocol's encoding, so a message
    // hash on it is correct rather than suspicious.
    meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_default;
    msg.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_PROVIDE;
    msg.sr_version = sfppsr::kDerivationVersion;
    msg.root_hash.size = 4;
    msg.bucket = 5;
    msg.message_hash.size = 16;

    Scope scope;
    TEST_ASSERT_TRUE(sfppsr::parseScope(msg, scope));
    TEST_ASSERT_EQUAL_UINT32(5, scope.bucket);
}

void test_aChainMessageCarryingSetFieldsIsRecognisable()
{
    meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_default;
    msg.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_SFPP_MESSAGE_TYPE_UNSPECIFIED;
    msg.bucket = 12;
    TEST_ASSERT_FALSE(sfppsr::hasSetReconciliationFields(msg));
    TEST_ASSERT_FALSE(sfppsr::isSetReconciliationType(msg.sfpp_message_type));

    // bucket is shared, so it alone proves nothing; sr_checksum is ours and cannot be innocent.
    msg.sr_checksum = 1;
    TEST_ASSERT_TRUE(sfppsr::hasSetReconciliationFields(msg));
}

void test_theWrongTypeIsNeverParsedAsAnother()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildItemRequest(aScope(), someIds(3), msg));

    Advert heard;
    TEST_ASSERT_FALSE(sfppsr::parseWindowAdvert(msg, heard));

    TEST_ASSERT_TRUE(sfppsr::buildWindowAdvert(aScope(), aStoreScope(), summaryOf(0, 4), msg));
    ShortIdList page;
    TEST_ASSERT_FALSE(sfppsr::parseShortIds(msg, page));
}

// --- Malformed input ---

void test_aTruncatedSketchIsRefusedRatherThanDecoded()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildWindowAdvert(aScope(), aStoreScope(), summaryOf(0, 10), msg));

    Advert heard;
    msg.message.size -= 1; // no longer a whole number of elements
    TEST_ASSERT_FALSE(sfppsr::parseWindowAdvert(msg, heard));

    msg.message.size = 0;
    TEST_ASSERT_FALSE(sfppsr::parseWindowAdvert(msg, heard));
}

void test_aSketchOverTheCapacityBoundIsRefused()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildWindowAdvert(aScope(), aStoreScope(), summaryOf(0, 10), msg));

    Advert heard;
    msg.message.size = sfppsr::kMaxSketchBytes + 4;
    TEST_ASSERT_FALSE(sfppsr::parseWindowAdvert(msg, heard));
}

void test_anOversizedSketchIsNeverBuilt()
{
    // One past the whole-window capacity is 196 bytes, past the frame budget the bound keeps.
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_FALSE(sfppsr::buildWindowAdvert(aScope(), aStoreScope(), summaryOf(0, 10, sfppsr::kMaxSketchCapacity + 1), msg));
}

// --- Packed advert scope ---

static sfppsr::AdvertScope someScope()
{
    sfppsr::AdvertScope s;
    s.version = sfppsr::kAdvertSchemaVersion;
    s.retentionHours = 48;
    s.windowEighths = 4;
    s.storeSize = 192;
    return s;
}

void test_scope_packs_the_19_bit_layout()
{
    const uint32_t packed = sfppsr::packAdvertScope(someScope());

    // Version 1 in bits 0-2, retention 48 in 3-9, window 4 in 10-12, store size code 44 in 13-18.
    TEST_ASSERT_EQUAL_HEX32(0x59181u, packed);

    sfppsr::AdvertScope heard;
    TEST_ASSERT_TRUE(sfppsr::parseAdvertScope(packed, heard));
    TEST_ASSERT_EQUAL_UINT8(1, heard.version);
    TEST_ASSERT_EQUAL_UINT8(48, heard.retentionHours);
    TEST_ASSERT_EQUAL_UINT8(4, heard.windowEighths);
    TEST_ASSERT_EQUAL_UINT16(192, heard.storeSize);
}

void test_scope_version_zero_is_invalid()
{
    sfppsr::AdvertScope scope = someScope();
    scope.version = 0;

    sfppsr::AdvertScope heard;
    TEST_ASSERT_FALSE(sfppsr::parseAdvertScope(sfppsr::packAdvertScope(scope), heard));

    scope.version = 7;
    TEST_ASSERT_TRUE(sfppsr::parseAdvertScope(sfppsr::packAdvertScope(scope), heard));
    TEST_ASSERT_EQUAL_UINT8(7, heard.version);
}

void test_scope_retention_and_window_ranges()
{
    sfppsr::AdvertScope scope = someScope();
    sfppsr::AdvertScope heard;

    scope.retentionHours = 1;
    scope.windowEighths = 0; // the whole retention
    TEST_ASSERT_TRUE(sfppsr::parseAdvertScope(sfppsr::packAdvertScope(scope), heard));
    TEST_ASSERT_EQUAL_UINT8(1, heard.retentionHours);
    TEST_ASSERT_EQUAL_UINT8(0, heard.windowEighths);

    scope.retentionHours = 127;
    scope.windowEighths = 7;
    TEST_ASSERT_TRUE(sfppsr::parseAdvertScope(sfppsr::packAdvertScope(scope), heard));
    TEST_ASSERT_EQUAL_UINT8(127, heard.retentionHours);
    TEST_ASSERT_EQUAL_UINT8(7, heard.windowEighths);

    // Retention zero is not an archive scope.
    scope.retentionHours = 0;
    TEST_ASSERT_FALSE(sfppsr::parseAdvertScope(sfppsr::packAdvertScope(scope), heard));

    // Nothing above the nineteen bits belongs to the schema.
    const uint32_t packed = sfppsr::packAdvertScope(someScope());
    TEST_ASSERT_FALSE(sfppsr::parseAdvertScope(packed | (1u << 19), heard));
    TEST_ASSERT_FALSE(sfppsr::parseAdvertScope(packed | (1u << 31), heard));
}

void test_store_size_minifloat_is_exact_to_fifteen()
{
    for (uint32_t size = 0; size <= 15; size++)
        TEST_ASSERT_EQUAL_UINT16(size, sfppsr::codeToStoreSize(sfppsr::storeSizeToCode(size)));
}

void test_store_size_minifloat_steps_to_nine_sixty()
{
    TEST_ASSERT_EQUAL_UINT16(960, sfppsr::codeToStoreSize(63));
    TEST_ASSERT_EQUAL_UINT8(63, sfppsr::storeSizeToCode(960));
    TEST_ASSERT_EQUAL_UINT8(63, sfppsr::storeSizeToCode(2000)); // clamped to the largest code

    uint16_t previous = sfppsr::codeToStoreSize(0);
    for (uint8_t code = 1; code < 64; code++) {
        const uint16_t size = sfppsr::codeToStoreSize(code);
        TEST_ASSERT_GREATER_THAN_UINT16(previous, size);
        previous = size;
    }
}

// An advert carrying the old per-bucket shape sets sr_version and puts a content count in field 9.
void test_legacy_bucket_advert_is_not_parseable()
{
    meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_default;
    msg.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ADVERT;
    msg.sr_version = sfppsr::kDerivationVersion;
    msg.root_hash.size = 4;
    memset(msg.root_hash.bytes, 0x42, msg.root_hash.size);
    msg.bucket = 9;
    msg.sr_count = 16;
    msg.sr_checksum = 0x1234;
    msg.message.size = 16; // a capacity-4 sketch of zeros
    memset(msg.message.bytes, 0, msg.message.size);

    Advert heard;
    TEST_ASSERT_FALSE(sfppsr::parseWindowAdvert(msg, heard));
}

// The scope carries its own schema version; a version this build does not know is not read as v1.
void test_anUnknownScopeVersionIsRefused()
{
    sfppsr::AdvertScope store = someScope();
    store.version = sfppsr::kAdvertSchemaVersion + 1;
    const BucketSummary summary = summaryOf(0, 9, 12);

    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildWindowAdvert(aScope(), store, summary, msg));

    Advert heard;
    TEST_ASSERT_FALSE(sfppsr::parseWindowAdvert(msg, heard));

    store.version = 0;
    TEST_ASSERT_FALSE(sfppsr::buildWindowAdvert(aScope(), store, summary, msg));
}

void test_largest_advert_sketch_fits_one_payload()
{
    const BucketSummary largest = summaryOf(0, 64, sfppsr::kMaxSketchCapacity);
    TEST_ASSERT_EQUAL_UINT(sfppsr::kMaxSketchBytes, largest.sketch().serializedSize());

    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildWindowAdvert(aScope(), aStoreScope(), largest, msg));
    TEST_ASSERT_EQUAL_UINT(sfppsr::kMaxSketchBytes, msg.message.size);

    Advert heard;
    TEST_ASSERT_TRUE(sfppsr::parseWindowAdvert(msg, heard));
    TEST_ASSERT_EQUAL_UINT(largest.sketch().serializedSize(), heard.sketch.serializedSize());
}

void test_advert_scope_round_trips_in_field_nine()
{
    const sfppsr::AdvertScope store = someScope();
    const BucketSummary summary = summaryOf(0, 9, 12);

    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildWindowAdvert(aScope(), store, summary, msg));
    TEST_ASSERT_EQUAL_UINT(0, msg.sr_version);
    TEST_ASSERT_EQUAL_UINT32(sfppsr::packAdvertScope(store), msg.scope);
    TEST_ASSERT_EQUAL_UINT32(0, msg.bucket);

    Advert heard;
    TEST_ASSERT_TRUE(sfppsr::parseWindowAdvert(msg, heard));
    TEST_ASSERT_EQUAL_UINT8(store.version, heard.storeScope.version);
    TEST_ASSERT_EQUAL_UINT8(store.retentionHours, heard.storeScope.retentionHours);
    TEST_ASSERT_EQUAL_UINT8(store.windowEighths, heard.storeScope.windowEighths);
    TEST_ASSERT_EQUAL_UINT16(store.storeSize, heard.storeScope.storeSize);
    TEST_ASSERT_EQUAL_UINT32(summary.count(), heard.count);
    TEST_ASSERT_TRUE(summary.checksum() == heard.checksum);
    TEST_ASSERT_EQUAL_UINT(summary.sketch().serializedSize(), heard.sketch.serializedSize());
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
    TEST_ASSERT_FALSE(sfppsr::buildWindowAdvert(shortScope, aStoreScope(), summaryOf(0, 4), msg));

    TEST_ASSERT_TRUE(sfppsr::buildWindowAdvert(aScope(), aStoreScope(), summaryOf(0, 4), msg));
    msg.root_hash.size = sfppsr::kRootPrefixBytes - 1;
    Advert heard;
    TEST_ASSERT_FALSE(sfppsr::parseWindowAdvert(msg, heard));
}

void test_fieldsBelongingToAnotherTypeAreRefused()
{
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildWindowAdvert(aScope(), aStoreScope(), summaryOf(0, 4), msg));
    msg.sr_short_ids_count = 1;
    msg.sr_short_ids[0] = 5;
    Advert heard;
    TEST_ASSERT_FALSE(sfppsr::parseWindowAdvert(msg, heard));

    TEST_ASSERT_TRUE(sfppsr::buildItemRequest(aScope(), someIds(2), msg));
    msg.sr_checksum = 99;
    ShortIdList wanted;
    TEST_ASSERT_FALSE(sfppsr::parseShortIds(msg, wanted));
}

void setup()
{
    UNITY_BEGIN();

    // Round trips
    RUN_TEST(test_anAdvertResolvesADifferenceAfterTheRoundTrip);
    RUN_TEST(test_anEnumProvideCarriesItsFramePosition);
    RUN_TEST(test_aSingleFrameSpendsNoFrameField);
    RUN_TEST(test_anEnumRequestIsScopeAndBucketOnly);

    // Overloading guard
    RUN_TEST(test_aSetReconciliationMessageMayNotCarryAnObjectHash);
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

    // Packed advert scope
    RUN_TEST(test_scope_packs_the_19_bit_layout);
    RUN_TEST(test_scope_version_zero_is_invalid);
    RUN_TEST(test_scope_retention_and_window_ranges);
    RUN_TEST(test_anUnknownScopeVersionIsRefused);
    RUN_TEST(test_store_size_minifloat_is_exact_to_fifteen);
    RUN_TEST(test_store_size_minifloat_steps_to_nine_sixty);
    RUN_TEST(test_largest_advert_sketch_fits_one_payload);
    RUN_TEST(test_advert_scope_round_trips_in_field_nine);
    RUN_TEST(test_legacy_bucket_advert_is_not_parseable);

    exit(UNITY_END());
}

void loop() {}
