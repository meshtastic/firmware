// Adversarial fuzzing of the protobuf decoders and the UTF-8 sanitizer - the "crash a node with a
// crafted packet" and "character-encoding crash" attack surface.
//
// These are the FIXTURE-FREE fuzzers: they exercise pure functions (pb_decode_from_bytes and the
// meshUtils UTF-8 helpers) with no NodeDB / channel / crypto bring-up. The heavier packet-path
// fuzzers live in test/test_fuzz_packets.
//
// The suite runs under the default `coverage` env (AddressSanitizer + LeakSanitizer); any
// out-of-bounds read/write, use-after-free, or leak on adversarial input turns the run RED. Inputs
// come from a deterministic seeded LCG so a failure always reproduces from the printed seed.
//
//   Group D1  protobuf decode fuzz - every mesh-facing message type, random + protobuf-shaped bytes
//   Group D2  UTF-8 sanitizer fuzz - sanitizeUtf8 / clampLongName / pb_string_length
//
// Note on PB_VALIDATE_UTF8: the global build flag makes nanopb reject a malformed-UTF-8 `string`
// field at decode, so decode of e.g. a User/Waypoint with a bad name returns *false*. That is a
// PASS here - the contract under test is crash-freedom, not decode success.

#include "MeshTypes.h" // include BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#include "mesh-pb-constants.h"
#include "mesh/generated/meshtastic/admin.pb.h"
#include "mesh/generated/meshtastic/channel.pb.h"
#include "mesh/generated/meshtastic/config.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/generated/meshtastic/mesh_beacon.pb.h"
#include "mesh/generated/meshtastic/module_config.pb.h"
#include "mesh/generated/meshtastic/mqtt.pb.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include "meshUtils.h"
#include <cstdio>
#include <cstring>
#include <pb_decode.h>

// Deterministic RNG (rngSeed/rngNext/rngByte/rngRange) - shared seeded LCG.
#include "support/DeterministicRng.h"

static constexpr uint64_t BASE_SEED = 0x00C0FFEEULL;
static constexpr unsigned DECODE_ITERS = 3000; // per type, per pass (ASan-instrumented, keep bounded)

// ---------------------------------------------------------------------------
// Group D1 - protobuf decode fuzz
// ---------------------------------------------------------------------------

// Union so one buffer holds any decoded type with correct size/alignment; pb_release walks it with
// the matching descriptor after each iteration (a no-op for STATIC-only messages, but correct if a
// build ever compiles a malloc-backed field via PB_ENABLE_MALLOC).
union AnyMsg {
    meshtastic_Data data;
    meshtastic_MeshPacket meshPacket;
    meshtastic_User user;
    meshtastic_Position position;
    meshtastic_Telemetry telemetry;
    meshtastic_RouteDiscovery routeDiscovery;
    meshtastic_Waypoint waypoint;
    meshtastic_NeighborInfo neighborInfo;
    meshtastic_Routing routing;
    meshtastic_AdminMessage adminMessage;
    meshtastic_StoreAndForward storeAndForward;
    meshtastic_MeshBeacon meshBeacon;
    meshtastic_ServiceEnvelope serviceEnvelope;
    meshtastic_ModuleConfig moduleConfig;
    meshtastic_Config config;
    meshtastic_Channel channel;
    meshtastic_ChannelSettings channelSettings;
    meshtastic_KeyVerification keyVerification;
};

struct FuzzType {
    const char *name;
    const pb_msgdesc_t *fields;
};

static const FuzzType FUZZ_TYPES[] = {
    {"Data", &meshtastic_Data_msg},
    {"MeshPacket", &meshtastic_MeshPacket_msg},
    {"User", &meshtastic_User_msg},
    {"Position", &meshtastic_Position_msg},
    {"Telemetry", &meshtastic_Telemetry_msg},
    {"RouteDiscovery", &meshtastic_RouteDiscovery_msg},
    {"Waypoint", &meshtastic_Waypoint_msg},
    {"NeighborInfo", &meshtastic_NeighborInfo_msg},
    {"Routing", &meshtastic_Routing_msg},
    {"AdminMessage", &meshtastic_AdminMessage_msg},
    {"StoreAndForward", &meshtastic_StoreAndForward_msg},
    {"MeshBeacon", &meshtastic_MeshBeacon_msg},           // beacon offer: char[101] message + PSK-bearing ChannelSettings
    {"ServiceEnvelope", &meshtastic_ServiceEnvelope_msg}, // MQTT downlink wrapper - unusual (non-RF) ingress
    {"ModuleConfig", &meshtastic_ModuleConfig_msg},       // admin set_module_config payload union
    {"Config", &meshtastic_Config_msg},                   // admin set_config payload union
    {"Channel", &meshtastic_Channel_msg},                 // admin set_channel payload
    {"ChannelSettings", &meshtastic_ChannelSettings_msg},
    {"KeyVerification", &meshtastic_KeyVerification_msg}, // PKI key-verification handshake payload
};
static const size_t NUM_FUZZ_TYPES = sizeof(FUZZ_TYPES) / sizeof(FUZZ_TYPES[0]);

static size_t writeVarint(uint8_t *buf, size_t cap, size_t n, uint64_t v)
{
    do {
        if (n >= cap)
            break;
        uint8_t byte = v & 0x7F;
        v >>= 7;
        if (v)
            byte |= 0x80;
        buf[n++] = byte;
    } while (v);
    return n;
}

// Generate protobuf-*shaped* noise: a run of (tag, payload) pairs with valid and invalid wire types.
// Reaches decoder states (submessage length prefixes, packed fields, bad wire types) that pure random
// bytes rarely hit, so it stresses the parser far deeper than raw noise alone.
static size_t genProtoish(uint8_t *buf, size_t cap)
{
    size_t n = 0;
    int fields = (int)rngRange(14);
    for (int f = 0; f < fields && n + 32 < cap; f++) {
        uint32_t fieldnum = 1 + rngRange(48);
        uint32_t wire = rngRange(8); // 0,1,2,5 are valid; 3,4,6,7 exercise wire-type rejection
        uint32_t tag = (fieldnum << 3) | (wire & 7);
        n = writeVarint(buf, cap, n, tag);
        switch (wire & 7) {
        case 0: // varint
            n = writeVarint(buf, cap, n, ((uint64_t)rngNext() << 32) | rngNext());
            break;
        case 1: // 64-bit
            for (int i = 0; i < 8 && n < cap; i++)
                buf[n++] = rngByte();
            break;
        case 2: { // length-delimited (string/bytes/submessage) - random and sometimes lying length
            uint32_t L = rngRange(24);
            n = writeVarint(buf, cap, n, L);
            for (uint32_t i = 0; i < L && n < cap; i++)
                buf[n++] = rngByte();
            break;
        }
        case 5: // 32-bit
            for (int i = 0; i < 4 && n < cap; i++)
                buf[n++] = rngByte();
            break;
        default: // invalid wire type - decoder must reject cleanly
            break;
        }
    }
    return n;
}

// Feed every type `iters` random buffers; the only contract is crash-freedom / ASan-clean.
static void decodeFuzzPass(bool protoish, uint64_t seed)
{
    rngSeed(seed);
    AnyMsg out;
    uint8_t buf[512];
    unsigned long total = 0;

    for (size_t ti = 0; ti < NUM_FUZZ_TYPES; ti++) {
        for (unsigned k = 0; k < DECODE_ITERS; k++) {
            size_t len;
            if (protoish) {
                len = genProtoish(buf, sizeof(buf));
            } else {
                len = rngRange(sizeof(buf) + 1);
                rngFill(buf, len);
            }
            memset(&out, 0, sizeof(out));
            // Return value intentionally ignored: true or false are both acceptable. What must never
            // happen is an out-of-bounds access, and ASan is watching for exactly that.
            (void)pb_decode_from_bytes(buf, len, FUZZ_TYPES[ti].fields, &out);
            pb_release(FUZZ_TYPES[ti].fields, &out);
            total++;
        }
    }
    // Reaching here means no ASan fault fired across every iteration.
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(NUM_FUZZ_TYPES * DECODE_ITERS), total);
}

void test_D1a_decode_fuzz_random(void)
{
    printf("  seed=0x%llx\n", (unsigned long long)(BASE_SEED ^ 0x1111));
    decodeFuzzPass(/*protoish=*/false, BASE_SEED ^ 0x1111);
}

void test_D1b_decode_fuzz_protobuf_shaped(void)
{
    printf("  seed=0x%llx\n", (unsigned long long)(BASE_SEED ^ 0x2222));
    decodeFuzzPass(/*protoish=*/true, BASE_SEED ^ 0x2222);
}

// ---------------------------------------------------------------------------
// Group D2 - UTF-8 sanitizer fuzz
// ---------------------------------------------------------------------------

// Independent strict UTF-8 validator - deliberately NOT sanitizeUtf8, so a bug in sanitizeUtf8 can't
// mask itself. Validates [s, first NUL) exactly as a strict decoder would (rejects overlong,
// surrogates, > U+10FFFF, truncated, stray continuation/lead bytes).
static bool isValidUtf8(const char *s)
{
    const uint8_t *p = (const uint8_t *)s;
    while (*p) {
        uint8_t c = *p;
        int seqLen;
        uint32_t cp, minCp;
        if (c < 0x80) {
            p++;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            seqLen = 2;
            cp = c & 0x1F;
            minCp = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            seqLen = 3;
            cp = c & 0x0F;
            minCp = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            seqLen = 4;
            cp = c & 0x07;
            minCp = 0x10000;
        } else {
            return false; // invalid lead (continuation byte as lead, or 0xF8+)
        }
        for (int i = 1; i < seqLen; i++) {
            if ((p[i] & 0xC0) != 0x80) // truncated / bad continuation (embedded NUL ends the loop above)
                return false;
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        if (cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        p += seqLen;
    }
    return true;
}

// Run the full sanitizeUtf8 contract against a raw buffer of size `cap`.
static void assertSanitizeContract(char *buf, size_t cap)
{
    char before[512];
    TEST_ASSERT_TRUE(cap <= sizeof(before));

    sanitizeUtf8(buf, cap);

    // 1. Always NUL-terminated within the buffer.
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, (uint8_t)buf[cap - 1], "sanitizeUtf8 must force a trailing NUL");
    // 2. Length never exceeds bufSize-1.
    TEST_ASSERT_TRUE_MESSAGE(strlen(buf) <= cap - 1, "sanitized string overran the buffer");
    // 3. Output re-validates as UTF-8 by an independent validator.
    TEST_ASSERT_TRUE_MESSAGE(isValidUtf8(buf), "sanitizeUtf8 left invalid UTF-8 behind");

    // 4. Idempotent: a second pass changes nothing and reports no replacement.
    memcpy(before, buf, cap);
    bool replacedAgain = sanitizeUtf8(buf, cap);
    TEST_ASSERT_FALSE_MESSAGE(replacedAgain, "sanitizeUtf8 is not idempotent");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(before, buf, cap, "second sanitize mutated an already-clean buffer");
}

// Every single byte value as a 1-char "string" in a 2-byte buffer.
void test_D2a_utf8_exhaustive_single_byte(void)
{
    for (int b = 0; b < 256; b++) {
        char buf[2] = {(char)b, (char)b}; // deliberately not NUL-terminated
        assertSanitizeContract(buf, sizeof(buf));
    }
}

// Every 2-byte lead+continuation pair (covers overlong C0/C1, valid, and bad continuations).
void test_D2b_utf8_exhaustive_two_byte(void)
{
    for (int lead = 0xC0; lead <= 0xFF; lead++) {
        for (int cont = 0x00; cont <= 0xFF; cont++) {
            char buf[4] = {(char)lead, (char)cont, (char)lead, (char)cont};
            assertSanitizeContract(buf, sizeof(buf));
        }
    }
}

// Tiny buffers (cap 1..4) exercise the truncated-sequence-at-end paths.
void test_D2c_utf8_tiny_buffers(void)
{
    rngSeed(BASE_SEED ^ 0x3333);
    for (size_t cap = 1; cap <= 4; cap++) {
        for (unsigned k = 0; k < 4000; k++) {
            char buf[8];
            for (size_t i = 0; i < cap; i++)
                buf[i] = (char)rngByte();
            assertSanitizeContract(buf, cap);
        }
    }
}

// Randomized buffers of random size, biased toward high bytes to stress multibyte paths.
void test_D2d_utf8_random(void)
{
    printf("  seed=0x%llx\n", (unsigned long long)(BASE_SEED ^ 0x4444));
    rngSeed(BASE_SEED ^ 0x4444);
    for (unsigned k = 0; k < 40000; k++) {
        char buf[128];
        size_t cap = 1 + rngRange(sizeof(buf));
        for (size_t i = 0; i < cap; i++) {
            // ~60% high bytes so multibyte lead/continuation logic gets hammered.
            buf[i] = (rngRange(100) < 60) ? (char)(0x80 + rngRange(0x80)) : (char)rngByte();
        }
        assertSanitizeContract(buf, cap);
    }
}

// clampLongName: a 25-byte buffer with random content and emoji straddling the 24-byte cut.
void test_D2e_clamp_long_name(void)
{
    rngSeed(BASE_SEED ^ 0x5555);
    for (unsigned k = 0; k < 20000; k++) {
        char buf[MAX_LONG_NAME_BYTES + 1 + 8]; // extra slack; clampLongName only touches [0, 24]
        for (size_t i = 0; i < sizeof(buf); i++)
            buf[i] = (char)rngByte();
        clampLongName(buf);
        TEST_ASSERT_TRUE_MESSAGE(strlen(buf) <= MAX_LONG_NAME_BYTES, "clampLongName exceeded the byte cap");
        TEST_ASSERT_TRUE_MESSAGE(isValidUtf8(buf), "clampLongName left invalid UTF-8");
    }
}

// pb_string_length: never over-reads, result is a valid content length within max_len.
void test_D2f_pb_string_length(void)
{
    rngSeed(BASE_SEED ^ 0x6666);
    for (unsigned k = 0; k < 20000; k++) {
        uint8_t buf[64];
        size_t maxLen = 1 + rngRange(sizeof(buf));
        rngFill(buf, maxLen);
        size_t len = pb_string_length((const char *)buf, maxLen);
        TEST_ASSERT_TRUE_MESSAGE(len <= maxLen, "pb_string_length returned > max_len");
        if (len > 0)
            TEST_ASSERT_NOT_EQUAL_MESSAGE(0, buf[len - 1], "pb_string_length pointed past the last content byte");
    }
}

// ---------------------------------------------------------------------------
void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    printf("\n=== Group D1: protobuf decode fuzz ===\n");
    RUN_TEST(test_D1a_decode_fuzz_random);
    RUN_TEST(test_D1b_decode_fuzz_protobuf_shaped);

    printf("\n=== Group D2: UTF-8 sanitizer fuzz ===\n");
    RUN_TEST(test_D2a_utf8_exhaustive_single_byte);
    RUN_TEST(test_D2b_utf8_exhaustive_two_byte);
    RUN_TEST(test_D2c_utf8_tiny_buffers);
    RUN_TEST(test_D2d_utf8_random);
    RUN_TEST(test_D2e_clamp_long_name);
    RUN_TEST(test_D2f_pb_string_length);

    exit(UNITY_END());
}

void loop() {}
