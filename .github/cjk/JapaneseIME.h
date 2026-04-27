#pragma once

// Japanese IME for Meshtastic — Wapuro romaji-to-kana conversion.
//
// Coverage: hiragana + katakana (toggleable). Matches the behavior of every
// mainstream Japanese IME (Microsoft IME, Mozc, macOS Japanese): user types
// ASCII in romaji, kana commit automatically as soon as a syllable is
// complete, no candidate selection needed.
//
// What is intentionally NOT supported:
//   - Kana → kanji conversion. That requires a multi-megabyte morphological
//     dictionary (mecab-ipadic etc.) that does not fit in MCU flash. Output
//     is pure kana, which is valid Japanese (children's books, signage,
//     furigana) and round-trips through any reader.
//   - The very rare wi/we/wo voicing and historical kana.
//
// Table is ~180 entries; per lookup is O(table) which is trivial on a 240MHz
// MCU. Keep this file self-contained — it does not include Arduino headers
// so the test harness can compile it on the host.

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace japanese_ime
{

enum class Mode : uint8_t {
    HIRAGANA,
    KATAKANA,
};

struct ConvertResult {
    // Bytes of the input tail that were consumed and replaced by `kana`.
    size_t consumed_bytes;
    // The kana bytes to append (UTF-8). Empty if no conversion happened.
    // `kana` is owned by the table (static storage); do not free.
    std::string kana;
};

// Try to convert as much of `tail` as possible, starting at offset 0.
// Returns the largest greedy match. If no syllable boundary is reachable
// (e.g., user typed "k" and we don't know if "ka" or "kya" is coming),
// returns {0, ""}.
//
// Special-cased rules:
//   - "nn"     -> ん     (consume 2)
//   - "n'"     -> ん     (consume 2)
//   - "n" + (non-vowel/non-y/non-n char) -> ん (consume 1, leave the next char)
//   - Doubled non-vowel/non-n consonant (e.g. "kk", "tt", "pp", "ssh"):
//     emit っ, consume 1 byte, leave the rest for the next call.
ConvertResult convert_one(const char *tail, size_t tail_len, Mode mode);

// Repeatedly call convert_one until no more progress can be made.
// Each successful conversion is appended to `out_kana`; consumed bytes are
// removed from `inout_tail`. `inout_tail` shrinks; `out_kana` grows.
// Caller is responsible for splicing `out_kana` into the input buffer.
void convert_greedy(std::string &inout_tail, std::string &out_kana, Mode mode);

} // namespace japanese_ime
