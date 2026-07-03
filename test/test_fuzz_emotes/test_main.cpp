// Adversarial fuzzing of the emote/UTF-8 width+truncation walkers - the render path a TEXT_MESSAGE
// payload reaches AFTER decode. Text payloads are opaque protobuf `bytes`, so PB_VALIDATE_UTF8 never
// screens them: invalid UTF-8, control bytes and truncated multi-byte lead bytes reach these walkers
// verbatim. The walkers advance by utf8CharLen(lead), which for a 0xC0/0xE0/0xF0 lead near the end of
// the buffer claims more bytes than remain - so an unclamped copy reads past the string.
//
// Each input is placed in an EXACT-sized heap buffer (content + NUL, no slack) so any read past the
// content is a hard heap-buffer-overflow that AddressSanitizer flags. Runs under the coverage env.

#include "TestUtil.h"
#include "configuration.h"
#include <unity.h>

#if HAS_SCREEN

#include "graphics/EmoteRenderer.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayFonts.h>
#include <cstdlib>
#include <cstring>

#include "support/DeterministicRng.h"
static constexpr uint64_t BASE_SEED = 0x00E3070EULL;

// Headless display with a REAL font, so the fuzz exercises the production width path end to end:
// EmoteRenderer's getUtf8ChunkWidth memcpy (guarded by the utf8CharLen clamp) AND the getStringWidth
// helper's byte sanitizer (which keeps the stock library's signed-char font indexing in bounds for
// bytes outside printable ASCII). getStringWidth only walks the font jump table, never a frame buffer.
class FakeDisplay : public OLEDDisplay
{
  public:
    FakeDisplay() { setFont(ArialMT_Plain_10); }
    void display() override {}
    int getBufferOffset() override { return 0; }
    size_t write(uint8_t) override { return 1; }
};

// Drive both walkers over a NUL-terminated exact-sized copy of [bytes, bytes+len).
static void exercise(FakeDisplay &d, const uint8_t *bytes, size_t len)
{
    char *buf = (char *)malloc(len + 1); // exactly content + NUL: reads past index len are OOB
    memcpy(buf, bytes, len);
    buf[len] = '\0';

    (void)graphics::EmoteRenderer::measureStringWithEmotes(&d, buf); // analyzeLineInternal walk
    char out[64];
    (void)graphics::EmoteRenderer::truncateToWidth(&d, buf, out, sizeof(out), 40); // cut-loop walk

    free(buf);
}

// A byte that begins a multi-byte UTF-8 sequence (so utf8CharLen returns 2/3/4).
static uint8_t multibyteLead()
{
    static const uint8_t leads[] = {0xC0, 0xE0, 0xF0, 0xE2}; // 0xE2/0xF0 are also emote leads
    return leads[rngRange(sizeof(leads))];
}

void test_emote_utf8_fuzz(void)
{
    printf("  seed=0x%llx\n", (unsigned long long)BASE_SEED);
    rngSeed(BASE_SEED);

    FakeDisplay d;

    for (unsigned k = 0; k < 20000; k++) {
        uint8_t bytes[300];
        size_t len = 1 + rngRange(sizeof(bytes)); // 1..300, includes >MAX_MESSAGE_SIZE

        // Non-zero fill so strlen() == len (an embedded NUL would just shorten the effective string).
        for (size_t i = 0; i < len; i++)
            bytes[i] = (uint8_t)(1 + rngRange(255));

        // Sprinkle multi-byte leads (some truncated) through the body to reach findEmoteAt / the
        // modifier skippers as well as the plain width path.
        unsigned sprinkles = rngRange(6);
        for (unsigned s = 0; s < sprinkles; s++)
            bytes[rngRange(len)] = multibyteLead();

        // Half the time, force the LAST byte to a lead: a truncated sequence with no continuation left,
        // the exact shape that makes an unclamped walker step past the buffer end.
        if (rngRange(2))
            bytes[len - 1] = multibyteLead();

        exercise(d, bytes, len);
    }
    TEST_ASSERT_TRUE(true); // reaching here = no ASan fault across all iterations
}

// Fixed regressions for the truncated-lead shape, independent of the RNG.
void test_emote_truncated_lead_edges(void)
{
    FakeDisplay d;
    const uint8_t loneF0[] = {0xF0};
    const uint8_t tailE0[] = {'h', 'i', 0xE0};
    const uint8_t tailC0[] = {'a', 'b', 'c', 0xC0};
    uint8_t allF0[64];
    memset(allF0, 0xF0, sizeof(allF0));

    exercise(d, loneF0, sizeof(loneF0));
    exercise(d, tailE0, sizeof(tailE0));
    exercise(d, tailC0, sizeof(tailC0));
    exercise(d, allF0, sizeof(allF0));
    TEST_ASSERT_TRUE(true);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_emote_truncated_lead_edges);
    RUN_TEST(test_emote_utf8_fuzz);
    exit(UNITY_END());
}

void loop() {}

#else // !HAS_SCREEN

void setUp(void) {}
void tearDown(void) {}
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}
void loop() {}

#endif
