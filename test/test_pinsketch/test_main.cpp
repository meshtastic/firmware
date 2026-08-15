#include "configuration.h" // for the extern "C" setup()/loop() the portduino runner links against
#include "modules/Native/PinSketch.h"

#include <set>
#include <unity.h>
#include <vector>

using pinsketch::Element;
using pinsketch::Sketch;

void setUp(void) {}
void tearDown(void) {}

// Deterministic non-zero element stream, so a failure is always reproducible.
static Element nthElement(uint32_t n)
{
    uint32_t x = n * 2654435761u + 0x9E3779B9u;
    x ^= x >> 15;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    return x ? x : 1u;
}

static Sketch sketchOf(const std::vector<Element> &members, size_t capacity)
{
    Sketch s(capacity);
    for (Element e : members)
        s.add(e);
    return s;
}

// --- Field arithmetic ---

void test_multiplicationIdentityAndZero()
{
    TEST_ASSERT_EQUAL_UINT32(0x1234ABCD, pinsketch::mul(0x1234ABCD, 1));
    TEST_ASSERT_EQUAL_UINT32(0x1234ABCD, pinsketch::mul(1, 0x1234ABCD));
    TEST_ASSERT_EQUAL_UINT32(0, pinsketch::mul(0x1234ABCD, 0));
}

void test_multiplicationIsCommutativeAndAssociative()
{
    const Element a = 0xDEADBEEF, b = 0x0BADF00D, c = 0x13579BDF;
    TEST_ASSERT_EQUAL_UINT32(pinsketch::mul(a, b), pinsketch::mul(b, a));
    TEST_ASSERT_EQUAL_UINT32(pinsketch::mul(pinsketch::mul(a, b), c), pinsketch::mul(a, pinsketch::mul(b, c)));
}

void test_everyElementHasAnInverse()
{
    for (uint32_t i = 1; i < 400; i++) {
        const Element e = nthElement(i);
        TEST_ASSERT_EQUAL_UINT32(1, pinsketch::mul(e, pinsketch::inv(e)));
    }
}

void test_squaringMatchesMultiplyingByItself()
{
    for (uint32_t i = 1; i < 200; i++) {
        const Element e = nthElement(i);
        TEST_ASSERT_EQUAL_UINT32(pinsketch::mul(e, e), pinsketch::sqr(e));
    }
}

// --- Sketch basics ---

void test_emptySketchDecodesToNothing()
{
    Sketch s(8);
    std::vector<Element> out;
    TEST_ASSERT_TRUE(s.decode(out));
    TEST_ASSERT_EQUAL_UINT32(0, out.size());
}

void test_zeroIsNotARepresentableMember()
{
    Sketch s(8);
    TEST_ASSERT_FALSE(s.add(0));
    TEST_ASSERT_TRUE(s.empty());
}

void test_addingTwiceRemovesTheElement()
{
    Sketch s(8);
    s.add(0xABCDEF01);
    TEST_ASSERT_FALSE(s.empty());
    s.add(0xABCDEF01);
    TEST_ASSERT_TRUE(s.empty());
}

void test_singleMemberRoundTrips()
{
    Sketch s(4);
    s.add(0xC0FFEE42);
    std::vector<Element> out;
    TEST_ASSERT_TRUE(s.decode(out));
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_UINT32(0xC0FFEE42, out[0]);
}

void test_identicalSetsShowNoDifference()
{
    std::vector<Element> members;
    for (uint32_t i = 0; i < 30; i++)
        members.push_back(nthElement(i));

    Sketch a = sketchOf(members, 10);
    Sketch b = sketchOf(members, 10);
    TEST_ASSERT_TRUE(a.merge(b));

    std::vector<Element> out;
    TEST_ASSERT_TRUE(a.decode(out));
    TEST_ASSERT_EQUAL_UINT32(0, out.size());
}

// --- What the protocol actually asks of it ---

void test_symmetricDifferenceIsRecoveredBothWays()
{
    // A holds 0..19, B holds 15..34. The difference is 0..14 plus 20..34, in both directions.
    std::vector<Element> aMembers, bMembers;
    for (uint32_t i = 0; i < 20; i++)
        aMembers.push_back(nthElement(i));
    for (uint32_t i = 15; i < 35; i++)
        bMembers.push_back(nthElement(i));

    Sketch a = sketchOf(aMembers, 40);
    TEST_ASSERT_TRUE(a.merge(sketchOf(bMembers, 40)));

    std::vector<Element> out;
    TEST_ASSERT_TRUE(a.decode(out));
    TEST_ASSERT_EQUAL_UINT32(30, out.size());

    std::set<Element> recovered(out.begin(), out.end());
    for (uint32_t i = 0; i < 15; i++)
        TEST_ASSERT_TRUE(recovered.count(nthElement(i)) == 1);
    for (uint32_t i = 20; i < 35; i++)
        TEST_ASSERT_TRUE(recovered.count(nthElement(i)) == 1);
    for (uint32_t i = 15; i < 20; i++)
        TEST_ASSERT_TRUE(recovered.count(nthElement(i)) == 0);
}

void test_setSizeIsIrrelevantOnlyTheDifferenceCounts()
{
    // 500 shared members against a capacity-4 sketch: the sketch never sees the set size.
    std::vector<Element> shared;
    for (uint32_t i = 0; i < 500; i++)
        shared.push_back(nthElement(i));

    Sketch a = sketchOf(shared, 4);
    Sketch b = sketchOf(shared, 4);
    b.add(nthElement(9001));
    b.add(nthElement(9002));
    TEST_ASSERT_TRUE(a.merge(b));

    std::vector<Element> out;
    TEST_ASSERT_TRUE(a.decode(out));
    TEST_ASSERT_EQUAL_UINT32(2, out.size());
}

void test_differenceExactlyAtCapacityDecodes()
{
    std::vector<Element> members;
    for (uint32_t i = 0; i < 12; i++)
        members.push_back(nthElement(100 + i));

    Sketch s = sketchOf(members, 12);
    std::vector<Element> out;
    TEST_ASSERT_TRUE(s.decode(out));
    TEST_ASSERT_EQUAL_UINT32(12, out.size());
}

void test_differenceOverCapacityIsRejectedAtUsefulCapacities()
{
    for (size_t capacity = 8; capacity <= 20; capacity++) {
        std::vector<Element> members;
        for (uint32_t i = 0; i <= capacity; i++)
            members.push_back(nthElement(200 + i));

        Sketch s = sketchOf(members, capacity);
        std::vector<Element> out;
        TEST_ASSERT_FALSE(s.decode(out));
        TEST_ASSERT_EQUAL_UINT32(0, out.size());
    }
}

void test_tinyCapacityMisdecodesOverCapacityAboutOneInFactorialC()
{
    // Not a defect - about 1/c! of the syndrome space belongs to a set small enough to decode, so an
    // over-capacity difference lands on a wrong set at that rate. It pins two things: the set
    // checksum is load-bearing, and a capacity-2 sketch cannot be used as a "too different" detector.
    const size_t trials = 200;
    size_t misdecoded = 0;
    for (size_t t = 0; t < trials; t++) {
        Sketch s(2);
        for (uint32_t i = 0; i < 4; i++)
            s.add(nthElement((uint32_t)t * 977 + i + 1));
        std::vector<Element> out;
        if (s.decode(out)) {
            misdecoded++;
            TEST_ASSERT_TRUE(out.size() <= 2); // never the true 4-element set
        }
    }
    TEST_ASSERT_TRUE(misdecoded > trials / 5);
}

void test_farOverCapacityFailsCleanly()
{
    std::vector<Element> members;
    for (uint32_t i = 0; i < 200; i++)
        members.push_back(nthElement(300 + i));

    Sketch s = sketchOf(members, 8);
    std::vector<Element> out;
    TEST_ASSERT_FALSE(s.decode(out));
}

void test_arbitraryBytesDoNotDecodeToASet()
{
    // A corrupted or truncated frame must be rejected, not turned into invented members.
    uint32_t rejected = 0;
    for (uint32_t seed = 1; seed <= 50; seed++) {
        Sketch s(8);
        uint8_t buf[32];
        for (size_t i = 0; i < sizeof(buf); i++)
            buf[i] = (uint8_t)(nthElement(seed * 31 + (uint32_t)i) & 0xFF);
        TEST_ASSERT_TRUE(s.deserialize(buf, sizeof(buf)));

        std::vector<Element> out;
        if (!s.decode(out))
            rejected++;
        else
            TEST_ASSERT_TRUE(out.size() <= 8); // if it did decode, it must be a set the sketch reproduces
    }
    TEST_ASSERT_TRUE(rejected > 40);
}

void test_largeDifferenceWithinCapacity()
{
    std::vector<Element> members;
    for (uint32_t i = 0; i < 45; i++)
        members.push_back(nthElement(500 + i));

    Sketch s = sketchOf(members, 50);
    std::vector<Element> out;
    TEST_ASSERT_TRUE(s.decode(out));
    TEST_ASSERT_EQUAL_UINT32(45, out.size());

    std::set<Element> recovered(out.begin(), out.end());
    for (Element e : members)
        TEST_ASSERT_TRUE(recovered.count(e) == 1);
}

// --- Wire handling ---

void test_serializeRoundTripsAndIsFourBytesPerCapacity()
{
    Sketch s(6);
    for (uint32_t i = 0; i < 5; i++)
        s.add(nthElement(700 + i));
    TEST_ASSERT_EQUAL_UINT32(24, s.serializedSize());

    uint8_t buf[24];
    s.serialize(buf);

    Sketch restored;
    TEST_ASSERT_TRUE(restored.deserialize(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT32(6, restored.capacity());

    std::vector<Element> out;
    TEST_ASSERT_TRUE(restored.decode(out));
    TEST_ASSERT_EQUAL_UINT32(5, out.size());
}

void test_serializationIsLittleEndian()
{
    Sketch s(1);
    s.add(0x04030201);
    uint8_t buf[4];
    s.serialize(buf);
    TEST_ASSERT_EQUAL_UINT8(0x01, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x04, buf[3]);
}

void test_partialFrameIsRejected()
{
    Sketch s;
    uint8_t buf[7] = {0};
    TEST_ASSERT_FALSE(s.deserialize(buf, sizeof(buf)));
}

void test_mergeRefusesMismatchedCapacity()
{
    Sketch a(8), b(4);
    TEST_ASSERT_FALSE(a.merge(b));
}

void test_truncatedSketchIsThePrefixOfALargerOne()
{
    // Prefix streaming depends on this: a capacity-8 sketch is byte-identical to the first 8
    // syndromes of a capacity-32 one over the same set, so a sender can transmit a prefix and grow it.
    std::vector<Element> members;
    for (uint32_t i = 0; i < 60; i++)
        members.push_back(nthElement(800 + i));

    Sketch wide = sketchOf(members, 32);
    wide.truncate(8);
    Sketch narrow = sketchOf(members, 8);

    uint8_t wideBuf[32], narrowBuf[32];
    TEST_ASSERT_EQUAL_UINT32(narrow.serializedSize(), wide.serializedSize());
    wide.serialize(wideBuf);
    narrow.serialize(narrowBuf);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(narrowBuf, wideBuf, 32);
}

void test_truncateNeverGrows()
{
    Sketch s(4);
    s.truncate(16);
    TEST_ASSERT_EQUAL_UINT32(4, s.capacity());
}

void test_grownCapacityResolvesWhatAPrefixCouldNot()
{
    // The escalation path: decode fails on the prefix, succeeds once the sender sends more.
    std::vector<Element> members;
    for (uint32_t i = 0; i < 20; i++)
        members.push_back(nthElement(900 + i));

    Sketch prefix = sketchOf(members, 32);
    prefix.truncate(8);
    std::vector<Element> out;
    TEST_ASSERT_FALSE(prefix.decode(out));

    Sketch full = sketchOf(members, 32);
    TEST_ASSERT_TRUE(full.decode(out));
    TEST_ASSERT_EQUAL_UINT32(20, out.size());
}

void setup()
{
    UNITY_BEGIN();

    // Field arithmetic
    RUN_TEST(test_multiplicationIdentityAndZero);
    RUN_TEST(test_multiplicationIsCommutativeAndAssociative);
    RUN_TEST(test_everyElementHasAnInverse);
    RUN_TEST(test_squaringMatchesMultiplyingByItself);

    // Sketch basics
    RUN_TEST(test_emptySketchDecodesToNothing);
    RUN_TEST(test_zeroIsNotARepresentableMember);
    RUN_TEST(test_addingTwiceRemovesTheElement);
    RUN_TEST(test_singleMemberRoundTrips);
    RUN_TEST(test_identicalSetsShowNoDifference);

    // Reconciliation
    RUN_TEST(test_symmetricDifferenceIsRecoveredBothWays);
    RUN_TEST(test_setSizeIsIrrelevantOnlyTheDifferenceCounts);
    RUN_TEST(test_differenceExactlyAtCapacityDecodes);
    RUN_TEST(test_differenceOverCapacityIsRejectedAtUsefulCapacities);
    RUN_TEST(test_tinyCapacityMisdecodesOverCapacityAboutOneInFactorialC);
    RUN_TEST(test_farOverCapacityFailsCleanly);
    RUN_TEST(test_arbitraryBytesDoNotDecodeToASet);
    RUN_TEST(test_largeDifferenceWithinCapacity);

    // Wire handling
    RUN_TEST(test_serializeRoundTripsAndIsFourBytesPerCapacity);
    RUN_TEST(test_serializationIsLittleEndian);
    RUN_TEST(test_partialFrameIsRejected);
    RUN_TEST(test_mergeRefusesMismatchedCapacity);
    RUN_TEST(test_truncatedSketchIsThePrefixOfALargerOne);
    RUN_TEST(test_truncateNeverGrows);
    RUN_TEST(test_grownCapacityResolvesWhatAPrefixCouldNot);

    exit(UNITY_END());
}

void loop() {}
