#pragma once
#ifdef EXCLUDE_PY
#include "pinyin_trie_exclude.h"
#else
#include "pinyin_trie_static.h"
#endif
#include <climits>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

std::string lastPinyin = "";
std::vector<std::string> lastresult = {};

// Platform-aware accessors for WORDS_DATA, WORDS_OFFSETS, WORDS_WEIGHT
// On NRF52, WORDS_DATA/WORDS_OFFSETS/WORDS_WEIGHT are in PROGMEM and must be read via pgm_read_*.
#if ARCH_NRF52
static inline void fill_word_buf_from_progmem(uint32_t off, char *buf, size_t buflen)
{
    const unsigned char *p = WORDS_DATA + off;
    size_t i = 0;
    while (i + 1 < buflen) {
        unsigned char c = pgm_read_byte(p + i);
        buf[i] = (char)c;
        if (c == 0)
            break;
        i++;
    }
    buf[buflen - 1] = 0;
}
static inline const char *get_word_ptr_buf(uint32_t idx, char *buf, size_t buflen)
{
    uint32_t off = (uint32_t)pgm_read_dword(&(WORDS_OFFSETS[idx]));
    fill_word_buf_from_progmem(off, buf, buflen);
    return buf;
}
static inline uint32_t get_word_weight(uint32_t idx)
{
    return (uint32_t)pgm_read_dword(&(WORDS_WEIGHT[idx]));
}
// Accessor for WORD_CHAR_COUNT (number of characters in stored word)
static inline uint8_t get_word_char_count(uint32_t idx)
{
    return (uint8_t)pgm_read_byte(&(WORD_CHAR_COUNT[idx]));
}
#else
static inline const char *get_word_ptr_buf(uint32_t idx, char *buf, size_t buflen)
{
    uint32_t off = WORDS_OFFSETS[idx];
    const unsigned char *p = WORDS_DATA + off;
    size_t i = 0;
    while (i + 1 < buflen) {
        unsigned char c = p[i];
        buf[i] = (char)c;
        if (c == 0)
            break;
        i++;
    }
    buf[buflen - 1] = 0;
    return buf;
}
static inline uint32_t get_word_weight(uint32_t idx)
{
    return WORDS_WEIGHT[idx];
}
static inline uint8_t get_word_char_count(uint32_t idx)
{
    return WORD_CHAR_COUNT[idx];
}
#endif

// Precomputed match table used by multi_mixed_search DFS. Internal linkage per TU.
static std::vector<std::vector<int>> __pinyin_match_at_global;

// 4b) multi-mixed search: allow each token to be specified either by its full pinyin or by its initial letter.
// Examples:
//  - "nhao" -> first token initial 'n', second token full "hao"
//  - "nih"  -> first token full "ni", second token initial 'h'
// We traverse the trie edges and at each edge accept the token if the remaining input either starts with the token (full match)
// or has a single letter equal to token[0] (initial match). Depth limited to 4.
static inline void multi_mixed_dfs_walk(const char *full, int len, int pos, int node, int depth,
                                        const std::vector<int> &token_len, uint32_t *out_word_idx, int *wrote, int max_out)
{
    if (pos == len) {
        if (depth == 0)
            return;
        auto wr = NODE_WORD_RANGE[node];
        for (uint32_t j = 0; j < wr[1]; j++) {
            uint32_t idx = wr[0] + j;
            // check character count equals depth
            if (get_word_char_count(idx) != (uint8_t)depth)
                continue;
            bool found = false;
            for (int k = 0; k < *wrote; k++)
                if (out_word_idx[k] == idx) {
                    found = true;
                    break;
                }
            if (!found && *wrote < max_out)
                out_word_idx[(*wrote)++] = idx;
        }
        return;
    }
    if (depth >= 4)
        return;
    auto er = NODE_EDGE_RANGE[node];
    // compute current worst accepted weight for pruning
    int current_worst = INT_MIN;
    if (*wrote >= max_out) {
        current_worst = INT_MAX;
        for (int i = 0; i < *wrote; i++) {
            int w = get_word_weight(out_word_idx[i]);
            if (w < current_worst)
                current_worst = w;
        }
    }
    // next_has_full helps decide initial-first preference
    bool next_has_full =
        (pos + 1 < len) && ((size_t)(pos + 1) < __pinyin_match_at_global.size()) && !__pinyin_match_at_global[pos + 1].empty();
    for (uint32_t e = er[0]; e < er[0] + er[1]; e++) {
        uint16_t tid = NODE_EDGES[e].token_id;
        const char *tk = PINYIN_TOKENS[tid];
        int tlen = token_len[tid];
        int child = NODE_EDGES[e].child_node;

        // quick reject: if neither an initial nor a full match at this pos, skip
        char c = full[pos];
        if (c >= 'A' && c <= 'Z')
            c = c - 'A' + 'a';
        bool init_match = (tk[0] == c);
        bool full_match = false;
        if ((size_t)pos < __pinyin_match_at_global.size()) {
            for (int mid : __pinyin_match_at_global[pos]) {
                if (mid == (int)tid) {
                    full_match = true;
                    break;
                }
            }
        }
        if (!init_match && !full_match)
            continue;

        // subtree pruning: if this child's subtree max weight can't beat current worst, skip
        if (current_worst != INT_MIN && (size_t)child < WORDS_MAX_WEIGHT_SIZE && WORDS_MAX_WEIGHT[child] <= current_worst)
            continue;

        // prefer trying initial-first when helpful (e.g., 'nhao' => 'n' + 'hao')
        if (init_match && next_has_full) {
            // initial first
            multi_mixed_dfs_walk(full, len, pos + 1, child, depth + 1, token_len, out_word_idx, wrote, max_out);
            if (full_match)
                multi_mixed_dfs_walk(full, len, pos + tlen, child, depth + 1, token_len, out_word_idx, wrote, max_out);
        } else {
            if (full_match)
                multi_mixed_dfs_walk(full, len, pos + tlen, child, depth + 1, token_len, out_word_idx, wrote, max_out);
            if (init_match)
                multi_mixed_dfs_walk(full, len, pos + 1, child, depth + 1, token_len, out_word_idx, wrote, max_out);
        }
    }
}

static inline int multi_mixed_search(const char *full, uint32_t *out_word_idx, int max_out)
{
    int len = (int)strlen(full);
    int wrote = 0;
    int token_count = (int)(sizeof(PINYIN_TOKENS) / sizeof(PINYIN_TOKENS[0]));
    std::vector<int> token_len(token_count);
    for (int i = 0; i < token_count; i++)
        token_len[i] = (int)strlen(PINYIN_TOKENS[i]);
    __pinyin_match_at_global.clear();
    __pinyin_match_at_global.resize((len > 0) ? len : 1);
    for (int pos = 0; pos < len; pos++) {
        for (int tid = 0; tid < token_count; tid++) {
            int tlen = token_len[tid];
            if (tlen <= len - pos && strncmp(full + pos, PINYIN_TOKENS[tid], tlen) == 0) {
                __pinyin_match_at_global[pos].push_back(tid);
            }
        }
    }
    multi_mixed_dfs_walk(full, len, 0, 0, 0, token_len, out_word_idx, &wrote, max_out);
    // sort by weight desc
    for (int a = 0; a < wrote; a++)
        for (int b = a + 1; b < wrote; b++)
            if (get_word_weight(out_word_idx[b]) > get_word_weight(out_word_idx[a])) {
                uint32_t t = out_word_idx[a];
                out_word_idx[a] = out_word_idx[b];
                out_word_idx[b] = t;
            }
    return wrote;
}

// Unified search: accept arbitrary input string and return up to max_out WORDS indices.
// Strategy (same as demo):
// 1) only do multi_mixed search
static inline int unified_search(const char *input, uint32_t *out_word_idx, int max_out)
{
    std::vector<uint32_t> tmp;
    tmp.resize(1024); // can handle 64 4-word chinese chengyu,don't exceed this value
    int got = 0;
    int total = 0;
    got = multi_mixed_search(input, tmp.data(), (int)tmp.size());
    for (int i = 0; i < got && total < max_out; i++) {
        uint32_t v = tmp[i];
        bool found = false;
        for (int k = 0; k < total; k++)
            if (out_word_idx[k] == v) {
                found = true;
                break;
            }
        if (!found)
            out_word_idx[total++] = v;
    }
    return total;
}

// C++-friendly overload: accept std::string and return vector<string> of top matches (words only)
static inline std::vector<std::string> unified_search(const std::string &input, int max_out = 20)
{
    if (input == lastPinyin)
        return lastresult;
    lastresult.clear();
    std::vector<uint32_t> idxs;
    idxs.resize(max_out > 0 ? max_out : 1);
    int got = unified_search(input.c_str(), idxs.data(), max_out);
    for (int i = 0; i < got; i++) {
        std::vector<char> tmp_buf(64);
        get_word_ptr_buf(idxs[i], tmp_buf.data(), tmp_buf.size());
        lastresult.emplace_back(std::string(tmp_buf.data()));
    }
    lastPinyin = input;
    return lastresult;
}
