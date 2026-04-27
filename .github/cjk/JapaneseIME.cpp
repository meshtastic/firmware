#include "JapaneseIME.h"

#include <algorithm>
#include <cstring>

namespace japanese_ime
{

// ---------- Romaji → Hiragana table ------------------------------------
//
// We store HIRAGANA only. Katakana is generated on the fly by adding 0x60
// to each U+30xx codepoint, which works because Hiragana (U+3041..U+3096)
// and Katakana (U+30A1..U+30F6) are codepoint-aligned.
//
// Sorted longest-first within length classes, but the lookup walks
// match_len = min(4, tail_len) downward, so order within a length class
// doesn't matter. Keep entries grouped by base consonant for readability.

struct Entry {
    const char *romaji;
    const char *hiragana;
};

// IMPORTANT: every entry here must be a complete syllable. Partial prefixes
// like "k" or "sh" must NOT appear — they are handled implicitly (the
// convert function returns {0,""} when no match exists, which signals
// "wait for more input").
//
// Entries are ordered by length descending so a linear scan finds the
// longest match first.
static const Entry TABLE[] = {
    // ---- 4-char (digraphs with leading "ts"/"ch"/"sh") ----
    // (none — these collapse to 3-char forms in Wapuro)

    // ---- 3-char ----
    // sa-row alt spellings
    {"shi", "し"},
    {"chi", "ち"},
    {"tsu", "つ"},
    // contracted yōon — k
    {"kya", "きゃ"}, {"kyi", "きぃ"}, {"kyu", "きゅ"}, {"kye", "きぇ"}, {"kyo", "きょ"},
    {"gya", "ぎゃ"}, {"gyi", "ぎぃ"}, {"gyu", "ぎゅ"}, {"gye", "ぎぇ"}, {"gyo", "ぎょ"},
    // contracted yōon — s
    {"sya", "しゃ"}, {"syi", "しぃ"}, {"syu", "しゅ"}, {"sye", "しぇ"}, {"syo", "しょ"},
    {"sha", "しゃ"}, {"shu", "しゅ"}, {"she", "しぇ"}, {"sho", "しょ"},
    {"zya", "じゃ"}, {"zyi", "じぃ"}, {"zyu", "じゅ"}, {"zye", "じぇ"}, {"zyo", "じょ"},
    {"jya", "じゃ"}, {"jyu", "じゅ"}, {"jyo", "じょ"},
    // contracted yōon — t
    {"tya", "ちゃ"}, {"tyi", "ちぃ"}, {"tyu", "ちゅ"}, {"tye", "ちぇ"}, {"tyo", "ちょ"},
    {"cha", "ちゃ"}, {"chu", "ちゅ"}, {"che", "ちぇ"}, {"cho", "ちょ"},
    {"dya", "ぢゃ"}, {"dyi", "ぢぃ"}, {"dyu", "ぢゅ"}, {"dye", "ぢぇ"}, {"dyo", "ぢょ"},
    // tsa/tse/tso  — katakana-only forms used in foreign words
    {"tsa", "つぁ"}, {"tsi", "つぃ"}, {"tse", "つぇ"}, {"tso", "つぉ"},
    // contracted yōon — n h m r p b
    {"nya", "にゃ"}, {"nyi", "にぃ"}, {"nyu", "にゅ"}, {"nye", "にぇ"}, {"nyo", "にょ"},
    {"hya", "ひゃ"}, {"hyi", "ひぃ"}, {"hyu", "ひゅ"}, {"hye", "ひぇ"}, {"hyo", "ひょ"},
    {"bya", "びゃ"}, {"byi", "びぃ"}, {"byu", "びゅ"}, {"bye", "びぇ"}, {"byo", "びょ"},
    {"pya", "ぴゃ"}, {"pyi", "ぴぃ"}, {"pyu", "ぴゅ"}, {"pye", "ぴぇ"}, {"pyo", "ぴょ"},
    {"mya", "みゃ"}, {"myi", "みぃ"}, {"myu", "みゅ"}, {"mye", "みぇ"}, {"myo", "みょ"},
    {"rya", "りゃ"}, {"ryi", "りぃ"}, {"ryu", "りゅ"}, {"rye", "りぇ"}, {"ryo", "りょ"},
    // f-row (foreign loans)
    {"fya", "ふゃ"}, {"fyu", "ふゅ"}, {"fyo", "ふょ"},
    // v-row (loans, ヴ etc.)
    {"vya", "ゔゃ"}, {"vyu", "ゔゅ"}, {"vyo", "ゔょ"},
    // explicit small kana (xa/la prefix is the wapuro convention)
    {"xtu", "っ"}, {"ltu", "っ"}, {"xtsu", "っ"},
    {"xya", "ゃ"}, {"lya", "ゃ"},
    {"xyu", "ゅ"}, {"lyu", "ゅ"},
    {"xyo", "ょ"}, {"lyo", "ょ"},
    {"xwa", "ゎ"}, {"lwa", "ゎ"},
    // k+w (kwa, kwi etc — rare; covered as ka+wa via 2-char usually)
    {"kwa", "くぁ"}, {"kwi", "くぃ"}, {"kwe", "くぇ"}, {"kwo", "くぉ"},
    {"gwa", "ぐぁ"}, {"gwi", "ぐぃ"}, {"gwe", "ぐぇ"}, {"gwo", "ぐぉ"},

    // ---- 2-char ----
    {"ka", "か"}, {"ki", "き"}, {"ku", "く"}, {"ke", "け"}, {"ko", "こ"},
    {"ga", "が"}, {"gi", "ぎ"}, {"gu", "ぐ"}, {"ge", "げ"}, {"go", "ご"},
    {"sa", "さ"}, {"si", "し"}, {"su", "す"}, {"se", "せ"}, {"so", "そ"},
    {"za", "ざ"}, {"zi", "じ"}, {"zu", "ず"}, {"ze", "ぜ"}, {"zo", "ぞ"},
    {"ji", "じ"}, {"ja", "じゃ"}, {"ju", "じゅ"}, {"jo", "じょ"}, {"je", "じぇ"},
    {"ta", "た"}, {"ti", "ち"}, {"tu", "つ"}, {"te", "て"}, {"to", "と"},
    {"da", "だ"}, {"di", "ぢ"}, {"du", "づ"}, {"de", "で"}, {"do", "ど"},
    {"na", "な"}, {"ni", "に"}, {"nu", "ぬ"}, {"ne", "ね"}, {"no", "の"},
    {"ha", "は"}, {"hi", "ひ"}, {"hu", "ふ"}, {"he", "へ"}, {"ho", "ほ"},
    {"fu", "ふ"}, {"fa", "ふぁ"}, {"fi", "ふぃ"}, {"fe", "ふぇ"}, {"fo", "ふぉ"},
    {"ba", "ば"}, {"bi", "び"}, {"bu", "ぶ"}, {"be", "べ"}, {"bo", "ぼ"},
    {"pa", "ぱ"}, {"pi", "ぴ"}, {"pu", "ぷ"}, {"pe", "ぺ"}, {"po", "ぽ"},
    {"ma", "ま"}, {"mi", "み"}, {"mu", "む"}, {"me", "め"}, {"mo", "も"},
    {"ya", "や"}, {"yi", "い"}, {"yu", "ゆ"}, {"ye", "いぇ"}, {"yo", "よ"},
    {"ra", "ら"}, {"ri", "り"}, {"ru", "る"}, {"re", "れ"}, {"ro", "ろ"},
    {"wa", "わ"}, {"wi", "うぃ"}, {"wu", "う"}, {"we", "うぇ"}, {"wo", "を"},
    // explicit ん
    {"nn", "ん"},
    // small kana 2-char
    {"xa", "ぁ"}, {"la", "ぁ"}, {"xi", "ぃ"}, {"li", "ぃ"},
    {"xu", "ぅ"}, {"lu", "ぅ"}, {"xe", "ぇ"}, {"le", "ぇ"},
    {"xo", "ぉ"}, {"lo", "ぉ"},
    // v (foreign)
    {"va", "ゔぁ"}, {"vi", "ゔぃ"}, {"vu", "ゔ"}, {"ve", "ゔぇ"}, {"vo", "ゔぉ"},

    // ---- 1-char ----
    {"a", "あ"}, {"i", "い"}, {"u", "う"}, {"e", "え"}, {"o", "お"},
    // (intentionally NOT including "n" alone here — handled by special rules
    //  below since "n" is ambiguous with "na", "ni", "nya", etc.)

    {nullptr, nullptr},
};

// Linear scan; returns the entry whose romaji matches the first match_len
// bytes of tail, or nullptr if no entry of that length exists.
static const char *table_lookup(const char *tail, size_t match_len)
{
    for (const Entry *e = TABLE; e->romaji; ++e) {
        if (std::strlen(e->romaji) == match_len &&
            std::strncmp(e->romaji, tail, match_len) == 0) {
            return e->hiragana;
        }
    }
    return nullptr;
}

// Convert a hiragana UTF-8 string to katakana UTF-8 in place.
// Hiragana U+3041..U+3096 -> Katakana U+30A1..U+30F6 by +0x60 codepoint.
// Other bytes (small chōonpu? whitespace?) pass through unchanged.
static std::string hira_to_kata(const std::string &hira)
{
    std::string out;
    out.reserve(hira.size());
    const uint8_t *p = reinterpret_cast<const uint8_t *>(hira.data());
    const uint8_t *end = p + hira.size();
    while (p < end) {
        // 3-byte UTF-8 sequence covering U+3000..U+FFFF
        if ((p[0] & 0xF0) == 0xE0 && p + 2 < end + 1 && p + 2 <= end) {
            uint32_t cp = ((uint32_t)(p[0] & 0x0F) << 12) |
                          ((uint32_t)(p[1] & 0x3F) << 6) |
                          (uint32_t)(p[2] & 0x3F);
            if (cp >= 0x3041 && cp <= 0x3096) {
                cp += 0x60;
            }
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
            p += 3;
        } else {
            out.push_back((char)*p++);
        }
    }
    return out;
}

static bool is_vowel(char c) { return c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o'; }

ConvertResult convert_one(const char *tail, size_t tail_len, Mode mode)
{
    if (tail_len == 0) return {0, ""};

    // ============ "n" handling — needs 1-char lookahead ============
    // "n" is the only ambiguous mora. We resolve it by looking one char
    // ahead. Behavior matches Microsoft IME / Mozc / macOS.
    //
    //   "n" alone                  -> wait
    //   "n'"                       -> ん, consume 2
    //   "nn" alone                 -> wait (need to see what's next)
    //   "nn" + vowel/y             -> ん, consume only 1 (second n starts new mora)
    //   "nn" + anything else       -> ん, consume 2
    //   "n" + vowel/y              -> fall through to table (na/ni/nu/ne/no/nya...)
    //   "n" + non-vowel-non-y      -> ん, consume 1 (the consonant stays)
    if (tail[0] == 'n') {
        if (tail_len == 1) {
            return {0, ""}; // wait
        }
        char c1 = tail[1];
        if (c1 == '\'') {
            std::string k = "ん";
            if (mode == Mode::KATAKANA) k = hira_to_kata(k);
            return {2, k};
        }
        if (c1 == 'n') {
            if (tail_len == 2) {
                return {0, ""}; // wait for lookahead
            }
            char c2 = tail[2];
            std::string k = "ん";
            if (mode == Mode::KATAKANA) k = hira_to_kata(k);
            if (is_vowel(c2) || c2 == 'y') {
                return {1, k}; // consume one n; second n stays for next syllable
            }
            return {2, k}; // "nn" + non-vowel/y/end => ん
        }
        // "n" + something other than another n / apostrophe.
        if (!is_vowel(c1) && c1 != 'y') {
            std::string k = "ん";
            if (mode == Mode::KATAKANA) k = hira_to_kata(k);
            return {1, k};
        }
        // "n" + vowel-or-y -> fall through to the table (e.g. "na", "nya").
    }

    // ============ "m" before b/p -> ん (assimilation) ============
    // Real IMEs accept "kombanwa" as well as "konbanwa". Note: we deliberately
    // do NOT match "mm" here — "samma" (秋刀魚) uses doubled m as sokuon, which
    // is handled by the doubled-consonant rule below.
    if (tail[0] == 'm' && tail_len >= 2 && (tail[1] == 'b' || tail[1] == 'p')) {
        std::string k = "ん";
        if (mode == Mode::KATAKANA) k = hira_to_kata(k);
        return {1, k};
    }

    // ============ Doubled non-vowel consonant -> sokuon っ ============
    // "kk", "tt", "pp", "ss", "mm", etc. Excludes "nn" (handled above) and
    // explicit-small markers x/l.
    if (tail_len >= 2 && tail[0] == tail[1] && !is_vowel(tail[0]) &&
        tail[0] != 'n' && tail[0] != 'y' && tail[0] != '\'' &&
        tail[0] != 'x' && tail[0] != 'l') {
        std::string k = "っ";
        if (mode == Mode::KATAKANA) k = hira_to_kata(k);
        return {1, k};
    }

    // ============ Normal table lookup: longest match wins ============
    size_t max_len = std::min((size_t)4, tail_len);
    for (size_t L = max_len; L >= 1; --L) {
        const char *hira = table_lookup(tail, L);
        if (hira) {
            std::string k = hira;
            if (mode == Mode::KATAKANA) k = hira_to_kata(k);
            return {L, k};
        }
    }

    // No match. Caller's choice whether this is "wait for more input" or
    // "commit literally as ASCII" — we just signal "no progress made".
    return {0, ""};
}

void convert_greedy(std::string &inout_tail, std::string &out_kana, Mode mode)
{
    while (!inout_tail.empty()) {
        ConvertResult r = convert_one(inout_tail.c_str(), inout_tail.size(), mode);
        if (r.consumed_bytes == 0) {
            break; // wait for more input
        }
        out_kana.append(r.kana);
        inout_tail.erase(0, r.consumed_bytes);
    }
}

} // namespace japanese_ime
