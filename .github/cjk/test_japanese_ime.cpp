// Host-compile test for the Japanese IME.
// Build:    g++ -std=c++17 -O2 -o test_ime test_japanese_ime.cpp JapaneseIME.cpp
// Run:      ./test_ime

#include "JapaneseIME.h"

#include <cstdio>
#include <cstring>
#include <string>

using japanese_ime::Mode;

// Simulate what the VirtualKeyboard does: feed romaji one char at a time,
// running greedy conversion after each insertion. Returns the final committed
// kana plus any unconverted romaji tail.
struct SimResult {
    std::string committed;
    std::string tail;
};

static SimResult simulate_typing(const char *romaji, Mode mode)
{
    SimResult r;
    for (size_t i = 0; romaji[i]; ++i) {
        r.tail.push_back(romaji[i]);
        japanese_ime::convert_greedy(r.tail, r.committed, mode);
    }
    return r;
}

// A "commit on submit" finisher: when the user hits ENTER, any leftover tail
// like a trailing solo "n" should resolve to ん.
static void finalize(SimResult &r, Mode mode)
{
    // Append a sentinel space to force terminal-n resolution, then strip it.
    r.tail.push_back(' ');
    japanese_ime::convert_greedy(r.tail, r.committed, mode);
    if (!r.tail.empty() && r.tail.front() == ' ') r.tail.erase(0, 1);
}

struct Case {
    const char *romaji;
    const char *expected; // expected committed (after finalize)
};

static int g_pass = 0, g_fail = 0;

static void check(const char *desc, const std::string &got, const char *expected)
{
    if (got == expected) {
        g_pass++;
        // printf("  OK   %-22s = %s\n", desc, got.c_str());
    } else {
        g_fail++;
        printf("  FAIL %-22s\n         got:    \"%s\"\n         wanted: \"%s\"\n",
               desc, got.c_str(), expected);
    }
}

int main()
{
    printf("=== Hiragana cases ===\n");
    Case hira[] = {
        // simple 5 vowels
        {"a",       "あ"},
        {"aiueo",   "あいうえお"},

        // basic syllables
        {"ka",      "か"},
        {"ki",      "き"},
        {"konnichiha", "こんにちは"},

        // hepburn alts
        {"shi",     "し"},
        {"si",      "し"},
        {"chi",     "ち"},
        {"ti",      "ち"},
        {"tsu",     "つ"},
        {"tu",      "つ"},
        {"fu",      "ふ"},
        {"hu",      "ふ"},
        {"ji",      "じ"},
        {"zi",      "じ"},

        // yōon
        {"kyou",    "きょう"},
        {"shashin", "しゃしん"},
        {"chairo",  "ちゃいろ"},
        {"jya",     "じゃ"},
        {"ja",      "じゃ"},
        {"sho",     "しょ"},
        {"cho",     "ちょ"},
        {"nyu",     "にゅ"},

        // sokuon (doubled consonants)
        {"kitte",   "きって"},
        {"motto",   "もっと"},
        {"asatte",  "あさって"},
        {"gakkou",  "がっこう"},
        {"happa",   "はっぱ"},
        {"icchi",   "いっち"},     // ic+chi -> い + っ + ち
        {"issho",   "いっしょ"},

        // terminal n
        {"hon",     "ほん"},        // n at end of word
        {"shinbun", "しんぶん"},    // n+b -> ん + ぶ
        {"konnichi","こんにち"},    // nn -> ん, then ni+chi
        {"hon'ya",  "ほんや"},      // explicit n' before y
        {"konya",   "こにゃ"},      // <-- actually konya = ko+nya = こ+にゃ. Watch.
        {"kanji",   "かんじ"},

        // small kana / explicit
        {"xtu",     "っ"},
        {"xa",      "ぁ"},
        {"xya",     "ゃ"},

        // mixed with ASCII passthrough — punctuation that's not a romaji letter
        // (handled by caller in real code; here we just verify pure romaji cases)

        // m + b/p assimilation (real-IME compatibility)
        {"konbanwa","こんばんわ"},
        {"kombanwa","こんばんわ"},
        {"sampo",   "さんぽ"},
        {"shimbun", "しんぶん"},

        // long words
        {"arigatou","ありがとう"},
        {"sayounara","さようなら"},
        {"watashi", "わたし"},
        {"sensei",  "せんせい"},
        {"benkyou", "べんきょう"},
        {"nihongo", "にほんご"},

        // foreign loan-ish (still hiragana)
        {"fa",      "ふぁ"},
        {"vi",      "ゔぃ"},
        {"tsa",     "つぁ"},

        // edge: just "n" — should commit to ん on finalize
        {"n",       "ん"},

        {nullptr, nullptr},
    };

    for (Case *c = hira; c->romaji; ++c) {
        SimResult r = simulate_typing(c->romaji, Mode::HIRAGANA);
        finalize(r, Mode::HIRAGANA);
        check(c->romaji, r.committed, c->expected);
    }

    printf("\n=== Katakana cases ===\n");
    Case kata[] = {
        {"a",       "ア"},
        {"katakana","カタカナ"},
        {"sushi",   "スシ"},
        {"sapporo", "サッポロ"},
        {"chiizu",  "チイズ"},
        {"vu",      "ヴ"},
        {nullptr, nullptr},
    };
    for (Case *c = kata; c->romaji; ++c) {
        SimResult r = simulate_typing(c->romaji, Mode::KATAKANA);
        finalize(r, Mode::KATAKANA);
        check(c->romaji, r.committed, c->expected);
    }

    printf("\n=== Streaming behavior ===\n");
    // Verify that intermediate states are reasonable: typing "k" should leave
    // "k" in the tail and commit nothing; typing "ka" should commit か.
    {
        SimResult r;
        r.tail = "k";
        japanese_ime::convert_greedy(r.tail, r.committed, Mode::HIRAGANA);
        check("after 'k'   committed", r.committed, "");
        check("after 'k'   tail",      r.tail,      "k");

        r.tail += "a";
        japanese_ime::convert_greedy(r.tail, r.committed, Mode::HIRAGANA);
        check("after 'ka'  committed", r.committed, "か");
        check("after 'ka'  tail",      r.tail,      "");
    }

    printf("\n=== Summary: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
