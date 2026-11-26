#if defined(T_DECK_PRO)

#include "TDeckProKeyboard.h"

#define _TCA8418_COLS 10
#define _TCA8418_ROWS 4
#define _TCA8418_NUM_KEYS 35

#include "Throttle.h"
#include "UptimeClock.h"

using Key = TCA8418KeyboardBase::TCA8418Key;

static constexpr uint32_t MODIFIER_TIMEOUT_MS = 1500;

static constexpr unsigned char TDeckProTapMap[_TCA8418_NUM_KEYS][4] = {
    {'p', 'P', '@', Key::SEND_PING},
    {'o', 'O', '+'},
    {'i', 'I', '-'},
    {'u', 'U', '_'},
    {'y', 'Y', ')'},
    {'t', 'T', '(', Key::TAB},
    {'r', 'R', '3'},
    {'e', 'E', '2'},
    {'w', 'W', '1'},
    {'q', 'Q', '#', Key::ESC}, // p, o, i, u, y, t, r, e, w, q
    {Key::BSP, 0x00, 0x00},
    {'l', 'L', '"'},
    {'k', 'K', '\''},
    {'j', 'J', ';'},
    {'h', 'H', ':'},
    {'g', 'G', '/', Key::GPS_TOGGLE},
    {'f', 'F', '6'},
    {'d', 'D', '5'},
    {'s', 'S', '4'},
    {'a', 'A', '*'}, // bsp, l, k, j, h, g, f, d, s, a
    {0x0d, 0x00, 0x00},
    {'$', 0x00, 0x00},
    {'m', 'M', '.', Key::MUTE_TOGGLE},
    {'n', 'N', ','},
    {'b', 'B', '!', Key::BL_TOGGLE},
    {'v', 'V', '?'},
    {'c', 'C', '9'},
    {'x', 'X', '8'},
    {'z', 'Z', '7'},
    {0x00, 0x00, 0x00}, // Ent, $, m, n, b, v, c, x, z, alt
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0x20, 0x00, 0x00},
    {0x00, 0x00, '0'},
    {0x00, 0x00, 0x00} // R_Shift, sym, space, mic, L_Shift
};

TDeckProKeyboard::TDeckProKeyboard() : TCA8418KeyboardBase(_TCA8418_ROWS, _TCA8418_COLS) {}

void TDeckProKeyboard::reset()
{
    TCA8418KeyboardBase::reset();
    heldModifiers = latchedModifiers = usedModifiers = 0;
    ::pinMode(KB_BL_PIN, OUTPUT);
    setBacklight(false);
#ifdef KB_INT
    ::pinMode(KB_INT, INPUT_PULLUP);
    enableInterrupts();
#endif
}

void TDeckProKeyboard::trigger()
{
    const uint8_t count = keyCount();
    for (uint8_t i = 0; i < count; ++i) {
        // Each read pops the oldest event; advancing the register skips queued
        // keys.
        const uint8_t event = readRegister(TCA8418_REG_KEY_EVENT_A);
        const uint8_t key = event & 0x7F;
        if (event & 0x80)
            pressed(key);
        else
            releaseKey(key);
    }
}

uint8_t TDeckProKeyboard::modifierForKey(uint8_t key)
{
    switch (key) {
    case 31:
        return 1;
    case 35:
        return 2;
    case 32:
        return 4;
    case 30:
        return 8;
    default:
        return 0;
    }
}

void TDeckProKeyboard::pressed(uint8_t key)
{
    if (key == 0 || _TCA8418_NUM_KEYS < key || state == Init)
        return;
    if (Throttle::hasElapsed(lastModifierMs, MODIFIER_TIMEOUT_MS))
        latchedModifiers = 0;
    const uint8_t modifier = modifierForKey(key);
    if (modifier) {
        heldModifiers |= modifier;
        usedModifiers &= ~modifier;
        return;
    }

    const uint8_t modifiers = heldModifiers | latchedModifiers;
    const uint8_t column = (modifiers & 8) ? 3 : (modifiers & 4) ? 2 : (modifiers & 3) ? 1 : 0;
    uint8_t character = TDeckProTapMap[key - 1][column];
    // Enter, space and backspace remain usable while a modifier is held.
    if (key == 11 || key == 21 || key == 33)
        character = TDeckProTapMap[key - 1][0];
    usedModifiers |= heldModifiers;
    latchedModifiers = 0;
#ifndef MESHTASTIC_INCLUDE_INKHUD
    if (character == Key::BL_TOGGLE) {
        toggleBacklight();
        return;
    }
#endif
    queueEvent(character);
}

void TDeckProKeyboard::releaseKey(uint8_t key)
{
    const uint8_t modifier = modifierForKey(key);
    if (!modifier || !(heldModifiers & modifier))
        return;
    // A tapped modifier applies to the next key; a held modifier applies until
    // release.
    if (!(usedModifiers & modifier)) {
        latchedModifiers ^= modifier;
        lastModifierMs = Time::getMillis();
    }
    heldModifiers &= ~modifier;
    usedModifiers &= ~modifier;
}

void TDeckProKeyboard::setBacklight(bool on)
{
    ::digitalWrite(KB_BL_PIN, on ? HIGH : LOW);
}

void TDeckProKeyboard::toggleBacklight()
{
    ::digitalWrite(KB_BL_PIN, !::digitalRead(KB_BL_PIN));
}

#endif // T_DECK_PRO
