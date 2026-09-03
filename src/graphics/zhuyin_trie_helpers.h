#pragma once
// Flash-limited targets (nRF52840) use a single-character-only dictionary that
// fits internal flash; larger-flash targets get the full multi-word trie. Both
// files declare the same BPMF_* arrays, so nothing below changes.
#if defined(ZHUYIN_SINGLE_CHAR)
#include "zhuyin_single_static.h"
#else
#include "zhuyin_trie_static.h"
#endif
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// Everything written by hand lives in namespace bpmf so this layer can be
// dropped into another project without colliding with it. The generated
// dictionary stays at global scope: it brings its own standard headers (which
// must not be pulled into a namespace) and part of its interface is macros,
// which no namespace would contain anyway - the BPMF_ prefix carries that side.
//
// The functions are `inline`, not `static inline`: with internal linkage each
// translation unit that included this file would get its own copy of the walk
// and of every template it instantiates. Only one unit includes it today, so
// nothing has been paid for yet, but the cost would appear silently.
//
// Nothing here holds state. The search caches, and the composition they belong
// to, live in bpmf::Engine (bpmf_engine.h), which is what makes this layer safe
// to call from more than one input session.

// The dictionary is a lightweight trie whose nodes carry no per-word metadata:
// each node owns a slice of BPMF_CAND_DATA holding its candidates already in
// frequency order. A record is
//     header_byte  code_point_lo code_point_hi  (once per character)
// where the header holds the character count and the tone bitmask (see
// BPMF_REC_COUNT / BPMF_REC_TONE), the tone bits being zero unless the generator
// ran with --tones (BPMF_HAS_WORDS_TONE). Ranking therefore comes for free from
// the byte order - there are no weight/offset/char-count arrays to walk.
//
// The structure itself is implied rather than stored. Nodes are numbered
// breadth-first, so a node's children occupy one consecutive run and
//     children(n) = [BPMF_FIRST_CHILD[n], BPMF_FIRST_CHILD[n + 1])
// describes the whole trie; a leaf repeats its successor's value so the range
// comes out empty. That single column replaces what used to be four (an edge
// list of token/child pairs plus a per-node start and count), and it means an
// edge index and a node id are the same number. BPMF_NODE_TOKEN then says which
// token reaches each node, indexed by the node rather than by an edge slot.
//
// Candidate offsets are elided the same way: BPMF_CAND_LEN gives every node's
// run length and the offset is their running sum, so only every
// (1 << BPMF_CKPT_SHIFT)th one is stored (see cand_off).
//
// The index arrays are typed by the generator to whatever each column actually
// needs, and this file names only those typedefs. That is what lets the same
// code serve both dictionaries: the single-character build indexes ~5.5k nodes
// and a 44 KB blob with 16-bit columns, while the full word list needs 32-bit
// candidate offsets for its 340 KB blob. Local variables below stay 32-bit on
// purpose - the flash saving is in the tables, and narrow locals would only risk
// truncating an intermediate sum.
static_assert(BPMF_MAX_TOKEN_ID <= (uint32_t)(bpmf_token_t)-1, "bpmf_token_t too narrow");
static_assert(BPMF_MAX_NODE_ID <= (uint32_t)(bpmf_node_t)-1, "bpmf_node_t too narrow");
static_assert(BPMF_MAX_CAND_OFF <= (uint32_t)(bpmf_off_t)-1, "bpmf_off_t too narrow");
static_assert(BPMF_MAX_CAND_LEN <= (uint32_t)(bpmf_len_t)-1, "bpmf_len_t too narrow");
static_assert(sizeof(BPMF_FIRST_CHILD) / sizeof(BPMF_FIRST_CHILD[0]) == BPMF_NODE_COUNT + 1,
              "BPMF_FIRST_CHILD must carry its closing sentinel");

namespace bpmf
{

// Token identifiers that produce a whole-token match at each byte position of
// the input. Built once per search and handed down the walk.
using MatchTable = std::vector<std::vector<int>>;

// Offset of a node's candidate run: the nearest checkpoint at or before it, plus
// the lengths of the nodes in between. At most (1 << BPMF_CKPT_SHIFT) - 1 byte
// additions, which is immaterial next to the full-blob scan predict_next
// already performs on every keystroke.
inline uint32_t cand_off(int node)
{
    uint32_t base = (uint32_t)node >> BPMF_CKPT_SHIFT;
    uint32_t off = BPMF_CAND_CKPT[base];
    for (uint32_t i = base << BPMF_CKPT_SHIFT; i < (uint32_t)node; i++)
        off += BPMF_CAND_LEN[i];
    return off;
}

// ── Tone stripping ────────────────────────────────────────────────────────
// Bopomofo tone marks are 2-byte UTF-8 sequences, all starting with 0xCB:
//   ˊ U+02CA → CB 8A   ˇ U+02C7 → CB 87
//   ˋ U+02CB → CB 8B   ˙ U+02D9 → CB 99
// Strip them so the search operates on tone-stripped token sequences only.
// (Tone-based post-filtering is the responsibility of the search below.)
inline std::string strip_tones(const std::string &input)
{
    std::string out;
    out.reserve(input.size());
    size_t i = 0;
    while (i < input.size()) {
        uint8_t b = (uint8_t)input[i];
        if (b == 0xCB && i + 1 < input.size()) {
            uint8_t b2 = (uint8_t)input[i + 1];
            if (b2 == 0x8A || b2 == 0x87 || b2 == 0x8B || b2 == 0x99) {
                i += 2; // skip 2-byte tone mark
                continue;
            }
        }
        out += (char)b;
        i++;
    }
    return out;
}

#if defined(BPMF_HAS_WORDS_TONE)
// Tone bit of the last tone mark typed (matches the inline tone byte in
// BPMF_CAND_DATA):  ˊ二 0x02  ˇ三 0x04  ˋ四 0x08  ˙輕 0x10.  一聲 has no mark, so
// it reads as 0 (no tone typed) - first-tone and untoned input are
// indistinguishable and both fall back to frequency-only ranking, which is the
// intended behaviour.
inline uint8_t input_tone(const std::string &s)
{
    uint8_t t = 0;
    for (size_t i = 0; i + 1 < s.size(); i++) {
        if ((uint8_t)s[i] == 0xCB) {
            switch ((uint8_t)s[i + 1]) {
            case 0x8A: t = 0x02; break; // ˊ
            case 0x87: t = 0x04; break; // ˇ
            case 0x8B: t = 0x08; break; // ˋ
            case 0x99: t = 0x10; break; // ˙
            }
        }
    }
    return t;
}
#endif

// ── Collected candidate + collector ───────────────────────────────────────
struct Cand {
    std::string surface;
    uint8_t     tone;       // matching bit from BPMF_HAS_WORDS_TONE data; 0 otherwise
    bool        completion; // word longer than what was typed (see collect_subtree)
};

// ── Candidate blob decoding ───────────────────────────────────────────────
// Surfaces are stored as 16-bit code points, so every candidate is re-encoded to
// UTF-8 on the way out. Nothing in the dictionary lies outside the BMP (the CJK
// font is built BMP-only, so such a character could not be drawn anyway), which
// is what makes two bytes per character enough and spares the build a code point
// table on the side.
inline void append_utf8(std::string &out, uint16_t cp)
{
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

// Code points sit at odd byte offsets (the header byte precedes them), so they
// are read a byte at a time rather than through a uint16_t cast: the blob then
// needs no alignment and the encoding stays little-endian on every target.
inline uint16_t cp_at(const unsigned char *d, uint32_t i)
{
    return (uint16_t)(d[i] | (d[i + 1] << 8));
}

// Decode the `n` code points starting at `d` into a UTF-8 string.
inline std::string decode_surface(const unsigned char *d, uint8_t n)
{
    std::string s;
    s.reserve((size_t)n * 3);
    for (uint8_t i = 0; i < n; i++)
        append_utf8(s, cp_at(d, (uint32_t)i * 2));
    return s;
}

// UTF-8 character count (lead bytes only). Bopomofo dictionary surfaces are all
// 3-byte CJK, but counting by lead byte keeps this correct for any content.
inline int char_count(const std::string &s)
{
    int n = 0;
    for (unsigned char b : s)
        if ((b & 0xC0) != 0x80)
            n++;
    return n;
}

// Read one node's candidate blob and append its (not-yet-seen) surfaces to
// `out`, preserving the frequency order baked into the blob. Candidates already
// collected from an earlier node win, so a character reachable by several
// readings keeps its highest-priority position (dedup by surface).
inline void emit_node(int node, std::vector<Cand> &out, int max_out, bool completion = false)
{
    uint32_t off = cand_off(node);
    uint32_t len = BPMF_CAND_LEN[node];
    const unsigned char *d = BPMF_CAND_DATA + off;

    uint32_t p = 0;
    while (p < len) {
        if ((int)out.size() >= max_out) return;
        uint8_t header = d[p++];
        uint8_t nchars = BPMF_REC_COUNT(header);
        uint8_t tone   = BPMF_REC_TONE(header);
        std::string surface = decode_surface(d + p, nchars);
        p += (uint32_t)nchars * 2;

        bool dup = false;
        for (const auto &e : out) {
            if (e.surface == surface) { dup = true; break; }
        }
        if (dup)
            continue;
        out.push_back({std::move(surface), tone, completion});
    }
}

// Prefix completion: once the typed syllables are fully consumed, also pull in
// longer words that begin with them - e.g. the single syllable ㄗㄠ surfaces 早安
// / 早上 alongside the single characters 早找澡. Walks the node's descendants
// breadth-first so shorter completions (two-character phrases) come before longer
// ones, emitting each node's words after the exact-length candidates already
// collected. On the single-character dictionary a depth-1 node's descendants are
// exactly the two-character phrases starting with that syllable. Descendants are
// visited in token (edge) order, not frequency, since cross-node ranking has no
// weights to sort on.
inline void collect_subtree(int node, std::vector<Cand> &out, int max_out, int depth)
{
    std::vector<int> frontier{node};
    while (!frontier.empty() && (int)out.size() < max_out && depth < 4) {
        std::vector<int> next;
        for (int n : frontier) {
            uint32_t cEnd = BPMF_FIRST_CHILD[n + 1];
            for (uint32_t c = BPMF_FIRST_CHILD[n]; c < cEnd; c++) {
                int child = (int)c;
                emit_node(child, out, max_out, true);
                if ((int)out.size() >= max_out)
                    return;
                next.push_back(child);
            }
        }
        frontier.swap(next);
        depth++;
    }
}

// ── DFS walk ──────────────────────────────────────────────────────────────
// Matching mirrors the pinyin walk, adapted for 3-byte Bopomofo characters:
//   full_match : the input from `pos` matches the whole token.
//   init_match : the token's first Bopomofo char (3 bytes) matches the input at
//                `pos` - i.e. a consonant-only abbreviation (ㄋㄏ → 你好).
// On init-only match advance `pos` by 3 (one Bopomofo char); next_has_full looks
// ahead the same 3 bytes. When the input is fully consumed at a real depth, the
// node's candidate blob is emitted in frequency order.
//
// allow_init selects the phase: the search runs once with it off (whole
// syllables only → exact single characters plus their prefix-completion phrases)
// and once with it on (adds consonant-abbreviation matches), so the exact
// characters always rank ahead of abbreviations like ㄋㄧ → 難以.
inline void dfs_walk(const char *full, int len, int pos, int node, int depth,
                     const std::vector<int> &token_len, const MatchTable &match_at,
                     std::vector<Cand> &out, int max_out, bool allow_init)
{
    if ((int)out.size() >= max_out)
        return;

    if (pos == len) {
        if (depth == 0)
            return;
        emit_node(node, out, max_out);
        // Also offer longer words that start with what was typed (早安 for ㄗㄠ).
        collect_subtree(node, out, max_out, depth);
        return;
    }
    if (depth >= 4)
        return;

    uint32_t cEnd = BPMF_FIRST_CHILD[node + 1];

    // Look 3 bytes ahead (next Bopomofo char boundary) for the heuristic
    bool next_has_full =
        (pos + 3 < len) &&
        ((size_t)(pos + 3) < match_at.size()) &&
        !match_at[pos + 3].empty();

    for (uint32_t c = BPMF_FIRST_CHILD[node]; c < cEnd; c++) {
        int child      = (int)c;
        uint32_t tid   = BPMF_NODE_TOKEN[child];
        const char *tk = BPMF_TOKENS[tid];
        int tlen       = token_len[tid];

        // ── Full match: input bytes from pos match the entire token ──────
        bool full_match = false;
        if ((size_t)pos < match_at.size()) {
            for (int mid : match_at[pos]) {
                if (mid == (int)tid) { full_match = true; break; }
            }
        }

        // ── Initial match: first Bopomofo char of token == input char at pos ──
        // Requires ≥3 bytes remaining AND token ≥3 bytes long.
        bool init_match = false;
        if (allow_init && tlen >= 3 && pos + 3 <= len) {
            init_match = (memcmp(full + pos, tk, 3) == 0);
            // If token is exactly 3 bytes, init_match ≡ full_match; avoid
            // double-visiting the same child by marking init_match false here.
            if (full_match && tlen == 3) init_match = false;
        }

        if (!init_match && !full_match)
            continue;

        // Prefer initial-first when helpful (analogous to 'nhao' => 'n'+'hao')
        if (init_match && next_has_full) {
            dfs_walk(full, len, pos + 3, child, depth + 1, token_len, match_at, out, max_out, allow_init);
            if (full_match)
                dfs_walk(full, len, pos + tlen, child, depth + 1, token_len, match_at, out, max_out, allow_init);
        } else {
            if (full_match)
                dfs_walk(full, len, pos + tlen, child, depth + 1, token_len, match_at, out, max_out, allow_init);
            if (init_match)
                dfs_walk(full, len, pos + 3, child, depth + 1, token_len, match_at, out, max_out, allow_init);
        }
    }
}

// ── Next-word prediction (surface-prefix scan) ────────────────────────────
// After a character is committed with no zhuyin composing, offer words that
// continue it (中 → 中文 / 中國). The dictionary is keyed by reading, not by
// surface, so there is no surface index to walk; instead scan the candidate blob
// once for records whose surface starts with `prefix` and is longer than it. Each
// record announces its character count in the header byte, so a single linear
// pass reads every dictionary word exactly once - a few tens of KB. Callers that
// run this on every redraw should go through bpmf::Engine, which caches it.
// The prefix is converted to code points up front and compared unit by unit,
// which also means only the records that actually match pay for being decoded
// back to UTF-8. Results follow blob order (frequency-ordered within each node),
// deduped by surface. Coverage is limited to the multi-character words the
// dictionary holds.
inline std::vector<std::string> predict_next(const std::string &prefix, int max_out = 20)
{
    std::vector<std::string> out;
    if (prefix.empty())
        return out;

    std::vector<uint16_t> want;
    for (size_t i = 0; i < prefix.size();) {
        uint8_t b = (uint8_t)prefix[i];
        if (b < 0x80) {
            want.push_back(b);
            i += 1;
        } else if ((b & 0xE0) == 0xC0 && i + 1 < prefix.size()) {
            want.push_back((uint16_t)(((b & 0x1F) << 6) | ((uint8_t)prefix[i + 1] & 0x3F)));
            i += 2;
        } else if ((b & 0xF0) == 0xE0 && i + 2 < prefix.size()) {
            want.push_back((uint16_t)(((b & 0x0F) << 12) |
                                      (((uint8_t)prefix[i + 1] & 0x3F) << 6) |
                                      ((uint8_t)prefix[i + 2] & 0x3F)));
            i += 3;
        } else {
            // Malformed, or outside the BMP: nothing in the dictionary can match.
            return out;
        }
    }
    if (want.empty())
        return out;

    uint32_t total = (uint32_t)sizeof(BPMF_CAND_DATA);
    const unsigned char *d = BPMF_CAND_DATA;
    uint32_t p = 0;
    while (p < total) {
        if ((int)out.size() >= max_out)
            break;
        uint8_t nchars = BPMF_REC_COUNT(d[p++]);

        if (nchars > want.size()) {
            bool match = true;
            for (size_t i = 0; i < want.size(); i++) {
                if (cp_at(d + p, (uint32_t)i * 2) != want[i]) { match = false; break; }
            }
            if (match) {
                std::string surface = decode_surface(d + p, nchars);
                bool dup = false;
                for (const auto &e : out) {
                    if (e == surface) { dup = true; break; }
                }
                if (!dup)
                    out.push_back(std::move(surface));
            }
        }
        p += (uint32_t)nchars * 2;
    }
    return out;
}

// ── Unified search ────────────────────────────────────────────────────────
// Accepts tone-stripped OR tone-bearing input; strips tones internally and,
// where the dictionary carries tone data, floats reading-matched candidates to
// the front without ever hiding the rest. Stateless - bpmf::Engine holds the
// cache that keeps a redraw from repeating the walk.
inline std::vector<std::string> unified_search(const std::string &input, int max_out = 20)
{
    std::string stripped = strip_tones(input);
#if defined(BPMF_HAS_WORDS_TONE)
    uint8_t wantTone = input_tone(input);
#endif

    int cap = max_out > 0 ? max_out : 1;
    // Collect with headroom so consonant-abbreviation phrases (中文 for ㄓㄨ),
    // which the second DFS phase finds only after the first phase has already
    // emitted every same-syllable single character, are gathered before the cap
    // truncates. Without this a productive syllable's single characters fill the
    // cap and the phrases never surface. The list is trimmed back to `cap` after
    // reordering below.
    int internalCap = cap < 128 ? 128 : cap;
    int len = (int)stripped.size();

    // Precompute which tokens produce a full match starting at each byte pos
    int token_count = (int)(sizeof(BPMF_TOKENS) / sizeof(BPMF_TOKENS[0]));
    std::vector<int> token_len(token_count);
    for (int i = 0; i < token_count; i++)
        token_len[i] = (int)strlen(BPMF_TOKENS[i]);

    MatchTable match_at((len > 0) ? len : 1);
    for (int pos = 0; pos < len; pos++) {
        const char *base = stripped.c_str();
        for (int tid = 0; tid < token_count; tid++) {
            int tlen = token_len[tid];
            if (tlen <= len - pos && strncmp(base + pos, BPMF_TOKENS[tid], tlen) == 0)
                match_at[pos].push_back(tid);
        }
    }

    std::vector<Cand> cands;
    // Phase 1: whole-syllable matches only - exact single characters and the
    // phrases that begin with them. Phase 2: consonant abbreviations (ㄋㄏ → 你好)
    // fill in behind them, deduped against phase 1.
    dfs_walk(stripped.c_str(), len, 0, 0, 0, token_len, match_at, cands, internalCap, false);
    dfs_walk(stripped.c_str(), len, 0, 0, 0, token_len, match_at, cands, internalCap, true);

    bool noTone = true;
#if defined(BPMF_HAS_WORDS_TONE)
    // Float candidates whose reading matches the typed tone ahead of the rest,
    // preserving frequency order within each group (stable). A tone that matches
    // nothing still shows every same-syllable candidate - tone narrows, never hides.
    if (wantTone)
        std::stable_partition(cands.begin(), cands.end(),
                              [&](const Cand &c) { return (c.tone & wantTone) != 0; });
    noTone = (wantTone == 0);
#endif

    // Phrases-first: when the input carries no tone (the user is not narrowing to
    // one reading of a single character), float multi-character words ahead of
    // single characters so a syllable pair like ㄗㄢ / ㄓㄨ surfaces 早安 / 中文
    // rather than burying them under every same-syllable single character. Stable,
    // so frequency order within each group is preserved. When a tone is typed the
    // user wants a specific character, so this reordering is skipped.
    //
    // Completions are deliberately excluded: their trailing characters were never
    // typed, so ㄓㄨ would promote 主辦 / 住民 (any word starting with that syllable)
    // ahead of the single characters, burying them under words the user did not ask
    // for. They stay behind the single characters, where the collect order already
    // puts them; typing more, or the post-commit prediction, brings them up.
    if (noTone)
        std::stable_partition(cands.begin(), cands.end(), [](const Cand &c) {
            return !c.completion && char_count(c.surface) >= 2;
        });

    if ((int)cands.size() > cap)
        cands.resize(cap);

    std::vector<std::string> out;
    out.reserve(cands.size());
    for (auto &c : cands)
        out.push_back(std::move(c.surface));
    return out;
}

} // namespace bpmf
