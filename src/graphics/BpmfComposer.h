#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ═══════════════════════════════════════════════════════════════════
//  BpmfComposer.h  -  Bopomofo keyboard mapping + composition state
//
//  Everything here lives in namespace bpmf and depends on nothing but
//  the C++ standard library, so this file travels to other firmware or
//  to a host build unchanged.
//
//  Compile-time layout selection:
//    -D BOPOMOFO_LAYOUT_YITIAN   Eten ET41 (libchewing LAYOUT_ETEN)
//    (default)                   Daqian
//
//  Cardputer note:
//    Key 44 produces '_' (tap[0]) and '-' (tap[1]/Shift) on the
//    CardputerAdv.  In both layouts '_' is added as an alias for '-'
//    so ㄦ (Daqian) / ㄥ (Eten) can be typed without Shift.
//
//  Usage sketch:
//    bpmf::Composer comp;
//    comp.addKey('s');            // ㄋ  (Daqian)
//    comp.addKey('u');            // ㄧ
//    comp.addKey('6');            // ˊ  → syllable closed, returns true
//    std::string query   = comp.searchString();  // "ㄋㄧ" (tone-stripped)
//    std::string display = comp.displayString(); // "ㄋㄧˊ"
//    comp.consumeSyllables(1);
// ═══════════════════════════════════════════════════════════════════

namespace bpmf
{

// ── Symbol types ─────────────────────────────────────────────────
enum Type : uint8_t {
    INITIAL = 0,  // initial: ㄅ … ㄙ
    MEDIAL  = 1,  // medial: ㄧ ㄨ ㄩ
    FINAL   = 2,  // final: ㄚ … ㄦ
    TONE2   = 3,  // ˊ rising
    TONE3   = 4,  // ˇ low
    TONE4   = 5,  // ˋ falling
    TONE5   = 6,  // ˙ neutral
    NO_TYPE = 0xFF
};

struct Symbol {
    const char *utf8; // null-terminated UTF-8
    Type        type;
};

// ── Symbol table ──────────────────────────────────────────────────
//  0-20  : initials
// 21-23  : medials
// 24-36  : finals
// 37-40  : tones 2-5
inline const Symbol SYMS[] = {
    {"ㄅ", INITIAL},   // 0
    {"ㄆ", INITIAL},   // 1
    {"ㄇ", INITIAL},   // 2
    {"ㄈ", INITIAL},   // 3
    {"ㄉ", INITIAL},   // 4
    {"ㄊ", INITIAL},   // 5
    {"ㄋ", INITIAL},   // 6
    {"ㄌ", INITIAL},   // 7
    {"ㄍ", INITIAL},   // 8
    {"ㄎ", INITIAL},   // 9
    {"ㄏ", INITIAL},   // 10
    {"ㄐ", INITIAL},   // 11
    {"ㄑ", INITIAL},   // 12
    {"ㄒ", INITIAL},   // 13
    {"ㄓ", INITIAL},   // 14
    {"ㄔ", INITIAL},   // 15
    {"ㄕ", INITIAL},   // 16
    {"ㄖ", INITIAL},   // 17
    {"ㄗ", INITIAL},   // 18
    {"ㄘ", INITIAL},   // 19
    {"ㄙ", INITIAL},   // 20
    {"ㄧ", MEDIAL},    // 21
    {"ㄨ", MEDIAL},    // 22
    {"ㄩ", MEDIAL},    // 23
    {"ㄚ", FINAL},     // 24
    {"ㄛ", FINAL},     // 25
    {"ㄜ", FINAL},     // 26
    {"ㄝ", FINAL},     // 27
    {"ㄞ", FINAL},     // 28
    {"ㄟ", FINAL},     // 29
    {"ㄠ", FINAL},     // 30
    {"ㄡ", FINAL},     // 31
    {"ㄢ", FINAL},     // 32
    {"ㄣ", FINAL},     // 33
    {"ㄤ", FINAL},     // 34
    {"ㄥ", FINAL},     // 35
    {"ㄦ", FINAL},     // 36
    {"ˊ",  TONE2},     // 37
    {"ˇ",  TONE3},     // 38
    {"ˋ",  TONE4},     // 39
    {"˙",  TONE5},     // 40
};
inline constexpr int    SYM_COUNT = 41;
inline constexpr int8_t KEY_NONE  = -1;
inline constexpr int8_t SYM_TONE5 = 40; // ˙ neutral

// The on-screen keyboard has 4x10 = 40 character cells, but Bopomofo needs 37 symbols
// plus 4 tones = 41. One short by construction, so the neutral tone gets no key of its
// own and is typed with Space while composing (see VirtualKeyboard::insertCharacter).
inline const Symbol *tone5_symbol()
{
    return &SYMS[SYM_TONE5];
}

// ── Keyboard layouts ──────────────────────────────────────────────
// Each table: printable ASCII 0x21 ('!') … 0x7E ('~') → SYMS index.
// Array index = ascii - 0x21.   Size = 94.
// Space → first-tone commit (handled by Composer::addSpace, not here).
// Backspace / Enter handled by caller.

// ── Daqian, 26 keys (standard) ──
// Source: RIME bopomofo.schema.yaml / zhuyin.yaml xlit rule (verified).
// xlit: "bpmfdtnlgkhjqxZCSrzcsiuvaoeEAIOUMNKGR12345"
//    →  "1qaz2wsxedcrfv5tgbyhnujm8ik,9ol.0p;/- 6347"
//
// Number row:  1=ㄅ  2=ㄉ  3=ˇ  4=ˋ  5=ㄓ  6=ˊ  7=˙  8=ㄚ  9=ㄞ  0=ㄢ
// Q row    :   q=ㄆ  w=ㄊ  e=ㄍ  r=ㄐ  t=ㄔ  y=ㄗ  u=ㄧ  i=ㄛ  o=ㄟ  p=ㄣ
// A row    :   a=ㄇ  s=ㄋ  d=ㄎ  f=ㄑ  g=ㄕ  h=ㄘ  j=ㄨ  k=ㄜ  l=ㄠ
// Z row    :   z=ㄈ  x=ㄌ  c=ㄏ  v=ㄒ  b=ㄖ  n=ㄙ  m=ㄩ  ,=ㄝ  .=ㄡ  /=ㄥ
// Punct    :   -=ㄦ  ;=ㄤ
// Tones    :   3=ˇ  4=ˋ  6=ˊ  7=˙  Space=first tone (implicit)
// Cardputer: '_' (key44 tap[0]) aliased to '-' → ㄦ
inline const int8_t KEYMAP_DAQIAN[94] = {
//  !    "    #    $    %    &    '    (    )    *
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   // 0x21-0x2A
//  +    ,    -    .    /    0    1    2    3    4
   -1,  27,  36,  31,  35,  32,   0,   4,  38,  39,   // 0x2B-0x34 (,=ㄝ -=ㄦ .=ㄡ /=ㄥ 0=ㄢ 1=ㄅ 2=ㄉ 3=ˇ 4=ˋ)
//  5    6    7    8    9    :    ;    <    =    >
   14,  37,  40,  24,  28,  -1,  34,  -1,  -1,  -1,   // 0x35-0x3E (5=ㄓ 6=ˊ 7=˙ 8=ㄚ 9=ㄞ ;=ㄤ)
//  ?    @    A    B    C    D    E    F    G    H
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   // 0x3F-0x48
//  I    J    K    L    M    N    O    P    Q    R
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   // 0x49-0x52
//  S    T    U    V    W    X    Y    Z   (backslash)
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   // 0x53-0x5C
//  ]    ^    _    `    a    b    c    d    e    f
   -1,  -1,  36,  -1,   2,  17,  10,   9,   8,  12,   // 0x5D-0x66 (_=ㄦ(alias) a=ㄇ b=ㄖ c=ㄏ d=ㄎ e=ㄍ f=ㄑ)
//  g    h    i    j    k    l    m    n    o    p
   16,  19,  25,  22,  26,  30,  23,  20,  29,  33,   // 0x67-0x70 (g=ㄕ h=ㄘ i=ㄛ j=ㄨ k=ㄜ l=ㄠ m=ㄩ n=ㄙ o=ㄟ p=ㄣ)
//  q    r    s    t    u    v    w    x    y    z
    1,  11,   6,  15,  21,  13,   5,   7,  18,   3,   // 0x71-0x7A (q=ㄆ r=ㄐ s=ㄋ t=ㄔ u=ㄧ v=ㄒ w=ㄊ x=ㄌ y=ㄗ z=ㄈ)
//  {    |    }    ~
   -1,  -1,  -1,  -1,                                  // 0x7B-0x7E
};

// ── Eten ET41 (libchewing LAYOUT_ETEN) ──
// Source: libchewing/src/editor/zhuyin_layout/et.rs (verified).
// Each key has a unique Bopomofo symbol - no disambiguation needed.
//
// Number row: 1=˙  2=ˊ  3=ˇ  4=ˋ  7=ㄑ  8=ㄢ  9=ㄣ  0=ㄤ
// Letters  : b=ㄅ  p=ㄆ  m=ㄇ  f=ㄈ  d=ㄉ  t=ㄊ  n=ㄋ  l=ㄌ
//            v=ㄍ  k=ㄎ  h=ㄏ  g=ㄐ  c=ㄒ  j=ㄖ  s=ㄙ
//            e=ㄧ  x=ㄨ  u=ㄩ
//            a=ㄚ  o=ㄛ  r=ㄜ  w=ㄝ  i=ㄞ  q=ㄟ  z=ㄠ  y=ㄡ
// Punct    : ,=ㄓ  .=ㄔ  /=ㄕ  ;=ㄗ  '=ㄘ  -=ㄥ  ==ㄦ
// Tones    : 1=˙  2=ˊ  3=ˇ  4=ˋ  Space=first tone (implicit)
// Note: 5, 6, [, ] are unused in ET41 (free for other functions).
// Cardputer: '_' (key44 tap[0]) aliased to '-' → ㄥ
inline const int8_t KEYMAP_YITIAN[94] = {
//  !    "    #    $    %    &    '    (    )    *
   -1,  -1,  -1,  -1,  -1,  -1,  19,  -1,  -1,  -1,   // 0x21-0x2A ('=ㄘ)
//  +    ,    -    .    /    0    1    2    3    4
   -1,  14,  35,  15,  16,  34,  40,  37,  38,  39,   // 0x2B-0x34 (,=ㄓ -=ㄥ .=ㄔ /=ㄕ 0=ㄤ 1=˙ 2=ˊ 3=ˇ 4=ˋ)
//  5    6    7    8    9    :    ;    <    =    >
   -1,  -1,  12,  32,  33,  -1,  18,  -1,  36,  -1,   // 0x35-0x3E (7=ㄑ 8=ㄢ 9=ㄣ ;=ㄗ ==ㄦ)
//  ?    @    A    B    C    D    E    F    G    H
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   // 0x3F-0x48
//  I    J    K    L    M    N    O    P    Q    R
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   // 0x49-0x52
//  S    T    U    V    W    X    Y    Z   (backslash)
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   // 0x53-0x5C
//  ]    ^    _    `    a    b    c    d    e    f
   -1,  -1,  35,  -1,  24,   0,  13,   4,  21,   3,   // 0x5D-0x66 (_=ㄥ(alias) a=ㄚ b=ㄅ c=ㄒ d=ㄉ e=ㄧ f=ㄈ)
//  g    h    i    j    k    l    m    n    o    p
   11,  10,  28,  17,   9,   7,   2,   6,  25,   1,   // 0x67-0x70 (g=ㄐ h=ㄏ i=ㄞ j=ㄖ k=ㄎ l=ㄌ m=ㄇ n=ㄋ o=ㄛ p=ㄆ)
//  q    r    s    t    u    v    w    x    y    z
   29,  26,  20,   5,  23,   8,  27,  22,  31,  30,   // 0x71-0x7A (q=ㄟ r=ㄜ s=ㄙ t=ㄊ u=ㄩ v=ㄍ w=ㄝ x=ㄨ y=ㄡ z=ㄠ)
//  {    |    }    ~
   -1,  -1,  -1,  -1,                                  // 0x7B-0x7E
};

// ── Active key map (compile-time) ────────────────────────────────
#if defined(BOPOMOFO_LAYOUT_YITIAN)
inline const int8_t *const KEYMAP = KEYMAP_YITIAN;
inline const char LAYOUT_NAME[] = "Eten(ET41)";
#else
inline const int8_t *const KEYMAP = KEYMAP_DAQIAN;
inline const char LAYOUT_NAME[] = "Daqian26";
#endif

// ── Key lookup ───────────────────────────────────────────────────
inline const Symbol *lookup_key(char ascii)
{
    int idx = (uint8_t)ascii - 0x21;
    if (idx < 0 || idx >= 94) return nullptr;
    int8_t sym_idx = KEYMAP[idx];
    if (sym_idx == KEY_NONE) return nullptr;
    return &SYMS[sym_idx];
}

// ── Screen grid layout (joystick on-screen keyboard) ─────────────
// Joystick boards use the stock 4x11 on-screen keyboard with Bopomofo on the first ten
// columns. Differences from the physical Daqian layout: the tones keep their physical
// Daqian positions (3=ˇ 4=ˋ 6=ˊ) so there is nothing to relearn, while the neutral tone
// has no cell and is typed with Space while composing (see
// VirtualKeyboard::insertCharacter). ㄦ, which has no grid position in Daqian, moves to
// the end of the number row (pushing ㄚㄞㄢ one cell left) and ㄥ goes to the bottom
// right, on the "?" key. The physical table KEYMAP_DAQIAN is untouched, so Cardputer
// hardware keyboards behave exactly as before. On the lookup side, the tone bits in the
// toned dictionary reorder the candidates against whatever tone was typed (see
// zhuyin_trie_helpers.h).
//
// Grid positions (key -> symbol):
//   1=ㄅ 2=ㄉ 3=ˇ 4=ˋ 5=ㄓ 6=ˊ 7=ㄚ 8=ㄞ 9=ㄢ 0=ㄦ
//   q=ㄆ w=ㄊ e=ㄍ r=ㄐ t=ㄔ y=ㄗ u=ㄧ i=ㄛ o=ㄟ p=ㄣ
//   a=ㄇ s=ㄋ d=ㄎ f=ㄑ g=ㄕ h=ㄘ j=ㄨ k=ㄜ l=ㄠ ;=ㄤ
//   z=ㄈ x=ㄌ c=ㄏ v=ㄒ b=ㄖ n=ㄙ m=ㄩ .=ㄝ ,=ㄡ ?=ㄥ
inline const int8_t KEYMAP_DAQIAN_SCREEN[94] = {
//  !    "    #    $    %    &    '    (    )    *
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   // 0x21-0x2A
//  +    ,    -    .    /    0    1    2    3    4
   -1,  31,  -1,  27,  -1,  36,   0,   4,  38,  39,   // 0x2B-0x34 (,=ㄡ .=ㄝ 0=ㄦ 1=ㄅ 2=ㄉ 3=ˇ 4=ˋ; - and / unused)
//  5    6    7    8    9    :    ;    <    =    >
   14,  37,  24,  28,  32,  -1,  34,  -1,  -1,  -1,   // 0x35-0x3E (5=ㄓ 6=ˊ 7=ㄚ 8=ㄞ 9=ㄢ ;=ㄤ)
//  ?    @    A    B    C    D    E    F    G    H
   35,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   // 0x3F-0x48 (?=ㄥ)
//  I    J    K    L    M    N    O    P    Q    R
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   // 0x49-0x52
//  S    T    U    V    W    X    Y    Z   (backslash)
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   // 0x53-0x5C
//  ]    ^    _    `    a    b    c    d    e    f
   -1,  -1,  -1,  -1,   2,  17,  10,   9,   8,  12,   // 0x5D-0x66 (a=ㄇ b=ㄖ c=ㄏ d=ㄎ e=ㄍ f=ㄑ)
//  g    h    i    j    k    l    m    n    o    p
   16,  19,  25,  22,  26,  30,  23,  20,  29,  33,   // 0x67-0x70 (g=ㄕ h=ㄘ i=ㄛ j=ㄨ k=ㄜ l=ㄠ m=ㄩ n=ㄙ o=ㄟ p=ㄣ)
//  q    r    s    t    u    v    w    x    y    z
    1,  11,   6,  15,  21,  13,   5,   7,  18,   3,   // 0x71-0x7A (q=ㄆ r=ㄐ s=ㄋ t=ㄔ u=ㄧ v=ㄒ w=ㄊ x=ㄌ y=ㄗ z=ㄈ)
//  {    |    }    ~
   -1,  -1,  -1,  -1,                                  // 0x7B-0x7E
};

// Screen layout lookup: only Daqian has a grid variant. Eten is not a joystick target,
// so it falls back to its physical table.
#if defined(BOPOMOFO_LAYOUT_YITIAN)
inline const int8_t *const KEYMAP_SCREEN = KEYMAP_YITIAN;
#else
inline const int8_t *const KEYMAP_SCREEN = KEYMAP_DAQIAN_SCREEN;
#endif

inline const Symbol *screen_symbol(char ascii)
{
    int idx = (uint8_t)ascii - 0x21;
    if (idx < 0 || idx >= 94) return nullptr;
    int8_t sym_idx = KEYMAP_SCREEN[idx];
    if (sym_idx == KEY_NONE) return nullptr;
    return &SYMS[sym_idx];
}

// ── Composer ─────────────────────────────────────────────────────
class Composer
{
  public:
    Composer() { clear(); }

    // ── Key press → Bopomofo symbol ──────────────────────────────
    // Returns true if a syllable was just closed (tone entered).
    // Returns false if the key has no Bopomofo mapping (caller may
    // treat it as English pass-through).
    bool addKey(char ascii)
    {
        const Symbol *sym = lookup_key(ascii);
        if (!sym) return false;
        return addSymbol(*sym);
    }

    // ── Add a Bopomofo symbol directly ───────────────────────────
    bool addSymbol(const Symbol &sym)
    {
        bool is_tone = (sym.type >= TONE2 && sym.type <= TONE5);
        if (is_tone) {
            if (!buffer_.empty()) {
                buffer_.push_back(&sym);
                syllable_boundaries_.push_back((uint8_t)buffer_.size());
                return true;
            }
            return false;
        }
        buffer_.push_back(&sym);
        return false;
    }

    // ── Space → first-tone commit ─────────────────────────────────
    bool addSpace()
    {
        if (buffer_.empty()) return false;
        syllable_boundaries_.push_back((uint8_t)buffer_.size());
        return true;
    }

    // ── Backspace ────────────────────────────────────────────────
    void backspace()
    {
        if (buffer_.empty()) return;
        buffer_.pop_back();
        while (!syllable_boundaries_.empty() &&
               syllable_boundaries_.back() > (uint8_t)buffer_.size())
            syllable_boundaries_.pop_back();
    }

    void clear()
    {
        buffer_.clear();
        syllable_boundaries_.clear();
    }

    bool empty() const { return buffer_.empty(); }

    // ── Display string (with tone marks) ─────────────────────────
    std::string displayString() const
    {
        std::string s;
        for (const Symbol *sym : buffer_)
            s += sym->utf8;
        return s;
    }

    // ── Search string (tone-stripped, for Trie lookup) ────────────
    std::string searchString() const
    {
        std::string s;
        for (const Symbol *sym : buffer_) {
            if (sym->type < TONE2)
                s += sym->utf8;
        }
        return s;
    }

    int syllableCount() const { return (int)syllable_boundaries_.size(); }

    // ── Remove first N completed syllables ───────────────────────
    void consumeSyllables(int n)
    {
        if (n <= 0 || syllable_boundaries_.empty()) return;
        if (n > (int)syllable_boundaries_.size())
            n = (int)syllable_boundaries_.size();
        uint8_t cut = syllable_boundaries_[n - 1];
        buffer_.erase(buffer_.begin(), buffer_.begin() + cut);
        syllable_boundaries_.erase(syllable_boundaries_.begin(),
                                   syllable_boundaries_.begin() + n);
        for (auto &b : syllable_boundaries_)
            b -= cut;
    }

  private:
    std::vector<const Symbol *> buffer_;
    std::vector<uint8_t>        syllable_boundaries_;
};

} // namespace bpmf
