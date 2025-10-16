#if defined(T_DECK_PRO)

#include "TDeckProKeyboard.h"

#define _TCA8418_COLS 10
#define _TCA8418_ROWS 4
#define _TCA8418_NUM_KEYS 35

using Key = TCA8418KeyboardBase::TCA8418Key;

constexpr uint8_t modifierRightShiftKey = 31 - 1; // keynum -1
constexpr uint8_t modifierRightShift = 0b0001;
constexpr uint8_t modifierLeftShiftKey = 35 - 1;
constexpr uint8_t modifierLeftShift = 0b0001;
constexpr uint8_t modifierSymKey = 32 - 1;
constexpr uint8_t modifierSym = 0b0010;
constexpr uint8_t modifierAltKey = 30 - 1;
constexpr uint8_t modifierAlt = 0b0100;

// Num chars per key, Modulus for rotating through characters
static const uint8_t TDeckProTapMod[_TCA8418_NUM_KEYS] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
                                                          5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};

static const uint8_t TDeckProTapMap[_TCA8418_NUM_KEYS][5] = {
    {'p', 'P', '@', 0x00, Key::SEND_PING},
    {'o', 'O', '+'},
    {'i', 'I', '-'},
    {'u', 'U', '_'},
    {'y', 'Y', ')'},
    {'t', 'T', '(', 0x00, Key::TAB},
    {'r', 'R', '3'},
    {'e', 'E', '2', 0x00, Key::UP},
    {'w', 'W', '1'},
    {'q', 'Q', '#', 0x00, Key::ESC}, // p, o, i, u, y, t, r, e, w, q
    {Key::BSP, 0x00, 0x00},
    {'l', 'L', '"'},
    {'k', 'K', '\''},
    {'j', 'J', ';'},
    {'h', 'H', ':'},
    {'g', 'G', '/', 0x00, Key::GPS_TOGGLE},
    {'f', 'F', '6', 0x00, Key::RIGHT},
    {'d', 'D', '5'},
    {'s', 'S', '4', 0x00, Key::LEFT},
    {'a', 'A', '*'}, // bsp, l, k, j, h, g, f, d, s, a
    {0x0d, 0x00, 0x00},
    {'$', 0x00, 0x00},
    {'m', 'M', '.', 0x00, Key::MUTE_TOGGLE},
    {'n', 'N', ','},
    {'b', 'B', '!', 0x00, Key::BL_TOGGLE},
    {'v', 'V', '?'},
    {'c', 'C', '9'},
    {'x', 'X', '8', 0x00, Key::DOWN},
    {'z', 'Z', '7'},
    {0x00, 0x00, 0x00}, // Ent, $, m, n, b, v, c, x, z, alt
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0x20, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00} // R_Shift, sym, space, mic, L_Shift
};

static bool TDeckProHeldMap[_TCA8418_NUM_KEYS] = {};

TDeckProKeyboard::TDeckProKeyboard()
    : TCA8418KeyboardBase(_TCA8418_ROWS, _TCA8418_COLS), modifierFlag(0), pressedKeysCount(0), onlyOneModifierPressed(false),
      persistedPreviousModifier(false)
{
}

void TDeckProKeyboard::reset()
{
    TCA8418KeyboardBase::reset();
    pinMode(KB_BL_PIN, OUTPUT);
    setBacklight(false);
}

int8_t TDeckProKeyboard::keyToIndex(uint8_t key)
{
    uint8_t key_index = 0;
    int row = (key - 1) / 10;
    int col = (key - 1) % 10;

    if (row >= _TCA8418_ROWS || col >= _TCA8418_COLS) {
        return -1; // Invalid key
    }

    key_index = row * _TCA8418_COLS + col;
    return key_index;
}

void TDeckProKeyboard::pressed(uint8_t key)
{
    int8_t key_index = keyToIndex(key);
    if (key_index < 0)
        return;

    if (TDeckProHeldMap[key_index]) {
        return;
    }

    TDeckProHeldMap[key_index] = true;
    pressedKeysCount++;

    uint8_t key_modifier = keyToModifierFlag(key_index);
    if (key_modifier && pressedKeysCount == 1) {
        onlyOneModifierPressed = true;
    } else {
        onlyOneModifierPressed = false;
    }
    modifierFlag |= key_modifier;
}

void TDeckProKeyboard::released(uint8_t key)
{
    int8_t key_index = keyToIndex(key);
    if (key_index < 0)
        return;

    if (!TDeckProHeldMap[key_index]) {
        return;
    }

    if (TDeckProTapMap[key_index][modifierFlag % TDeckProTapMod[key_index]] == Key::BL_TOGGLE) {
        toggleBacklight();
    } else {
        queueEvent(TDeckProTapMap[key_index][modifierFlag % TDeckProTapMod[key_index]]);
    }

    TDeckProHeldMap[key_index] = false;
    pressedKeysCount--;

    if (onlyOneModifierPressed) {
        onlyOneModifierPressed = false;
        if (persistedPreviousModifier) {
            modifierFlag = 0;
        }
        persistedPreviousModifier = !persistedPreviousModifier;
    } else if (persistedPreviousModifier && pressedKeysCount == 0) {
        modifierFlag = 0;
        persistedPreviousModifier = false;
    } else {
        modifierFlag &= ~keyToModifierFlag(key_index);
    }
}

void TDeckProKeyboard::setBacklight(bool on)
{
    if (on) {
        digitalWrite(KB_BL_PIN, HIGH);
    } else {
        digitalWrite(KB_BL_PIN, LOW);
    }
}

void TDeckProKeyboard::toggleBacklight(void)
{
    digitalWrite(KB_BL_PIN, !digitalRead(KB_BL_PIN));
}

uint8_t TDeckProKeyboard::keyToModifierFlag(uint8_t key)
{
    if (key == modifierRightShiftKey) {
        return modifierRightShift;
    } else if (key == modifierLeftShiftKey) {
        return modifierLeftShift;
    } else if (key == modifierSymKey) {
        return modifierSym;
    } else if (key == modifierAltKey) {
        return modifierAlt;
    }
    return 0;
}

bool TDeckProKeyboard::isModifierKey(uint8_t key)
{
    return keyToModifierFlag(key) != 0;
}

#endif // T_DECK_PRO