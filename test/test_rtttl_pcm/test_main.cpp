// Arduino.h must come first: it declares setup()/loop() with the linkage portduino's
// main.cpp expects. Without it this file's definitions get C++ mangling and the
// coverage build fails to link (RtttlPcm.h itself is deliberately Arduino-free).
#include <Arduino.h>

#include "audio/RtttlPcm.h"
#include <cstring>
#include <string>
#include <unity.h>
#include <vector>

void setUp(void) {}
void tearDown(void) {}

// Drain a generator completely and return every frame's left-channel sample.
static std::vector<int16_t> drain(RtttlPcm &gen, size_t chunkFrames = 512)
{
    std::vector<int16_t> left;
    std::vector<int16_t> buf(chunkFrames * 2);
    // Bound the loop so a generator bug shows up as a failure rather than a hang.
    for (int guard = 0; guard < 100000 && !gen.done(); guard++) {
        size_t got = gen.generate(buf.data(), chunkFrames);
        for (size_t i = 0; i < got; i++) {
            TEST_ASSERT_EQUAL_INT16(buf[2 * i], buf[2 * i + 1]); // must be mono-duplicated
            left.push_back(buf[2 * i]);
        }
        if (got == 0)
            break;
    }
    return left;
}

// --- Header parsing ---

void test_header_and_single_note_length()
{
    // b=120 -> whole note 2000ms, d=4 -> 500ms -> 22050 * 500 / 1000 frames.
    const char *song = "t:d=4,o=5,b=120:c";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(11025, pcm.size());
    TEST_ASSERT_TRUE(gen.done());
}

void test_buzz_style_header_parses()
{
    // The header buzz.cpp used to synthesize: d/o/b in order, whole note 1200ms.
    const char *song = "tone:d=32,o=4,b=200:c";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));
    auto pcm = drain(gen);
    // 1200/32 = 37ms (not 37.5 - truncated), 22050 * 37 / 1000 = 815 frames.
    TEST_ASSERT_EQUAL_UINT32(815, pcm.size());
}

void test_missing_header_rejected()
{
    const char *song = "no colon here";
    RtttlPcm gen;
    TEST_ASSERT_FALSE(gen.begin(song, strlen(song)));
    TEST_ASSERT_TRUE(gen.done());
}

void test_out_of_order_header_rejected()
{
    // RTTTL requires d, then o, then b.
    const char *song = "t:o=5,d=4,b=120:c";
    RtttlPcm gen;
    TEST_ASSERT_FALSE(gen.begin(song, strlen(song)));
}

void test_zero_bpm_rejected_not_divide_by_zero()
{
    // Upstream divided by bpm unguarded and crashed here.
    const char *song = "t:d=4,o=5,b=0:c";
    RtttlPcm gen;
    TEST_ASSERT_FALSE(gen.begin(song, strlen(song)));
}

void test_zero_default_duration_rejected()
{
    const char *song = "t:d=0,o=5,b=120:c";
    RtttlPcm gen;
    TEST_ASSERT_FALSE(gen.begin(song, strlen(song)));
}

void test_explicit_zero_note_duration_falls_back()
{
    // "0c" must not divide by zero; it falls back to the default duration.
    const char *song = "t:d=4,o=5,b=120:0c";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(11025, pcm.size());
}

void test_empty_body_reports_done()
{
    const char *song = "t:d=4,o=5,b=120:";
    RtttlPcm gen;
    TEST_ASSERT_FALSE(gen.begin(song, strlen(song)));
    TEST_ASSERT_TRUE(gen.done());
}

// --- Parser robustness (upstream read out of bounds on these) ---

void test_song_ending_in_digit_does_not_overrun()
{
    // Upstream malloc'd without a NUL and its digit loop had no bounds check, so
    // this read past the allocation. 1200... b=120 -> 2000/16 = 125ms.
    const char *song = "t:d=4,o=5,b=120:16e6";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(2756, pcm.size());
}

void test_b_sharp_top_octave_does_not_read_past_table()
{
    // "b#7" computes index 49 into a 49-entry table upstream. Must be clamped.
    const char *song = "t:d=4,o=5,b=120:b#7";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(11025, pcm.size());
    // Still a real tone, not silence.
    bool sawTone = false;
    for (int16_t s : pcm)
        if (s != 0)
            sawTone = true;
    TEST_ASSERT_TRUE(sawTone);
}

void test_truncated_song_is_not_an_error()
{
    // A song cut off mid-note simply ends.
    const char *song = "t:d=4,o=5,b=120:c,";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(11025, pcm.size());
}

// --- Note semantics that existing ringtones depend on ---

void test_octave_clamped_low_and_high()
{
    // o=1 clamps up to 4, o=8 clamps down to 7; both must still sound.
    const char *low = "t:d=4,o=1,b=120:c";
    const char *high = "t:d=4,o=8,b=120:c";
    RtttlPcm a, b;
    TEST_ASSERT_TRUE(a.begin(low, strlen(low)));
    TEST_ASSERT_TRUE(b.begin(high, strlen(high)));
    auto pa = drain(a);
    auto pb = drain(b);
    TEST_ASSERT_EQUAL_UINT32(11025, pa.size());
    TEST_ASSERT_EQUAL_UINT32(11025, pb.size());
}

void test_dotted_note_after_octave()
{
    // 500ms + 250ms = 750ms -> 16537 frames (truncated).
    const char *song = "t:d=4,o=5,b=120:4c5.";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(16537, pcm.size());
}

void test_dotted_note_before_octave()
{
    // Spec-legal placement that upstream silently desynced on.
    const char *song = "t:d=4,o=5,b=120:4c.5";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(16537, pcm.size());
}

void test_rest_is_silence()
{
    const char *song = "t:d=4,o=5,b=120:4p";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(11025, pcm.size());
    for (int16_t s : pcm)
        TEST_ASSERT_EQUAL_INT16(0, s);
}

void test_multiple_notes_sum_durations()
{
    // Three quarter notes at b=120 -> 3 * 11025 frames.
    const char *song = "t:d=4,o=5,b=120:c,d,e";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(3 * 11025, pcm.size());
}

// --- Waveform ---

void test_amplitude_is_attenuated_like_setgain()
{
    // AudioOutputI2S::SetGain(0.2) attenuated +/-8192 to +/-1536; ESP32I2SAudio has
    // no gain stage so the generator must emit the attenuated value directly.
    const char *song = "t:d=4,o=5,b=120:c";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));
    auto pcm = drain(gen);
    for (int16_t s : pcm)
        TEST_ASSERT_TRUE((s == RtttlPcm::kAmplitude) || (s == -RtttlPcm::kAmplitude));
    // Phase starts at 0, which is the low half of the square wave.
    TEST_ASSERT_EQUAL_INT16(-RtttlPcm::kAmplitude, pcm[0]);
}

void test_square_wave_frequency_matches_note()
{
    // A 2000ms C5 (523Hz) must contain ~1046 cycles.
    const char *song = "t:d=1,o=5,b=120:c";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(44100, pcm.size());

    int rising = 0;
    for (size_t i = 1; i < pcm.size(); i++)
        if (pcm[i - 1] < 0 && pcm[i] > 0)
            rising++;
    // 523Hz * 2.0s = 1046 cycles; allow a couple for edge truncation.
    TEST_ASSERT_INT_WITHIN(3, 1046, rising);
}

void test_phase_resets_each_note()
{
    // Every note starts in the low half, so sample 0 of note 2 is also negative.
    const char *song = "t:d=4,o=5,b=120:c,c";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(2 * 11025, pcm.size());
    TEST_ASSERT_EQUAL_INT16(-RtttlPcm::kAmplitude, pcm[0]);
    TEST_ASSERT_EQUAL_INT16(-RtttlPcm::kAmplitude, pcm[11025]);
}

// --- Chunking behaviour the AudioThread pump relies on ---

void test_short_read_only_at_end_of_song()
{
    const char *song = "t:d=4,o=5,b=120:c";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));

    int16_t buf[256 * 2];
    size_t total = 0;
    while (!gen.done()) {
        size_t got = gen.generate(buf, 256);
        total += got;
        if (got < 256) {
            // Only legal on the final chunk.
            TEST_ASSERT_TRUE(gen.done());
        }
        if (got == 0)
            break;
    }
    TEST_ASSERT_EQUAL_UINT32(11025, total);
}

void test_generate_after_done_returns_zero()
{
    const char *song = "t:d=4,o=5,b=120:c";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));
    drain(gen);
    TEST_ASSERT_TRUE(gen.done());
    int16_t buf[16];
    TEST_ASSERT_EQUAL_UINT32(0, gen.generate(buf, 8));
}

void test_reset_abandons_song()
{
    const char *song = "t:d=4,o=5,b=120:c";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song, strlen(song)));
    gen.reset();
    TEST_ASSERT_TRUE(gen.done());
    int16_t buf[16];
    TEST_ASSERT_EQUAL_UINT32(0, gen.generate(buf, 8));
}

void test_oversized_song_is_truncated_not_overflowed()
{
    // Longer than the internal 256-byte buffer; must clamp rather than overrun.
    std::string song = "t:d=4,o=5,b=120:";
    for (int i = 0; i < 200; i++)
        song += "c,";
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.begin(song.c_str(), song.size()));
    auto pcm = drain(gen);
    TEST_ASSERT_TRUE(pcm.size() > 0);
}

// --- beginTones: the system-melody path that replaces the RTTTL round-trip ---

void test_begin_tones_uses_exact_durations()
{
    // 100ms + 50ms at 22050Hz -> 2205 + 1102 frames (second one truncated).
    const ToneDuration melody[] = {{440, 100}, {880, 50}};
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.beginTones(melody, 2));
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(2205 + 1102, pcm.size());
}

void test_begin_tones_silent_note_is_silence()
{
    // NOTE_SILENT is 1Hz; as a square wave that would be an audible thump.
    const ToneDuration melody[] = {{1, 100}};
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.beginTones(melody, 1));
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(2205, pcm.size());
    for (int16_t s : pcm)
        TEST_ASSERT_EQUAL_INT16(0, s);
}

void test_begin_tones_copies_input()
{
    // playTones() passes a stack array and, now that playback is non-blocking,
    // returns while the tone is still sounding. The generator must not alias it.
    ToneDuration melody[] = {{440, 100}};
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.beginTones(melody, 1));
    melody[0].frequency_khz = 12345;
    melody[0].duration_ms = 9999;
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(2205, pcm.size());
}

void test_begin_tones_frequency_matches()
{
    // 1000ms of 1000Hz -> 1000 cycles.
    const ToneDuration melody[] = {{1000, 1000}};
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.beginTones(melody, 1));
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(22050, pcm.size());
    int rising = 0;
    for (size_t i = 1; i < pcm.size(); i++)
        if (pcm[i - 1] < 0 && pcm[i] > 0)
            rising++;
    TEST_ASSERT_INT_WITHIN(3, 1000, rising);
}

void test_begin_tones_clamps_count()
{
    ToneDuration melody[64];
    for (auto &t : melody) {
        t.frequency_khz = 440;
        t.duration_ms = 10;
    }
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.beginTones(melody, 64));
    auto pcm = drain(gen);
    // Clamped to kMaxTones notes of 10ms -> 220 frames each.
    TEST_ASSERT_EQUAL_UINT32(RtttlPcm::kMaxTones * 220, pcm.size());
}

void test_begin_tones_rejects_empty()
{
    RtttlPcm gen;
    TEST_ASSERT_FALSE(gen.beginTones(nullptr, 0));
    const ToneDuration melody[] = {{440, 100}};
    TEST_ASSERT_FALSE(gen.beginTones(melody, 0));
}

void test_begin_tones_zero_duration_is_skipped()
{
    // A zero-length tone must not spin the generator.
    const ToneDuration melody[] = {{440, 0}, {440, 100}};
    RtttlPcm gen;
    TEST_ASSERT_TRUE(gen.beginTones(melody, 2));
    auto pcm = drain(gen);
    TEST_ASSERT_EQUAL_UINT32(2205, pcm.size());
}

void setup()
{
    UNITY_BEGIN();

    RUN_TEST(test_header_and_single_note_length);
    RUN_TEST(test_buzz_style_header_parses);
    RUN_TEST(test_missing_header_rejected);
    RUN_TEST(test_out_of_order_header_rejected);
    RUN_TEST(test_zero_bpm_rejected_not_divide_by_zero);
    RUN_TEST(test_zero_default_duration_rejected);
    RUN_TEST(test_explicit_zero_note_duration_falls_back);
    RUN_TEST(test_empty_body_reports_done);

    RUN_TEST(test_song_ending_in_digit_does_not_overrun);
    RUN_TEST(test_b_sharp_top_octave_does_not_read_past_table);
    RUN_TEST(test_truncated_song_is_not_an_error);

    RUN_TEST(test_octave_clamped_low_and_high);
    RUN_TEST(test_dotted_note_after_octave);
    RUN_TEST(test_dotted_note_before_octave);
    RUN_TEST(test_rest_is_silence);
    RUN_TEST(test_multiple_notes_sum_durations);

    RUN_TEST(test_amplitude_is_attenuated_like_setgain);
    RUN_TEST(test_square_wave_frequency_matches_note);
    RUN_TEST(test_phase_resets_each_note);

    RUN_TEST(test_short_read_only_at_end_of_song);
    RUN_TEST(test_generate_after_done_returns_zero);
    RUN_TEST(test_reset_abandons_song);
    RUN_TEST(test_oversized_song_is_truncated_not_overflowed);

    RUN_TEST(test_begin_tones_uses_exact_durations);
    RUN_TEST(test_begin_tones_silent_note_is_silence);
    RUN_TEST(test_begin_tones_copies_input);
    RUN_TEST(test_begin_tones_frequency_matches);
    RUN_TEST(test_begin_tones_clamps_count);
    RUN_TEST(test_begin_tones_rejects_empty);
    RUN_TEST(test_begin_tones_zero_duration_is_skipped);

    exit(UNITY_END());
}

void loop() {}
