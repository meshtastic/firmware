#if defined(T_LORA_PAGER)

#include "TLoraPagerKeyboard.h"
#include "main.h"

#define _TCA8418_COLS 10
#define _TCA8418_ROWS 4
#define _TCA8418_NUM_KEYS 31

#define _TCA8418_MULTI_TAP_THRESHOLD 1500

using Key = TCA8418KeyboardBase::TCA8418Key;

constexpr uint8_t modifierRightShiftKey = 29 - 1; // keynum -1
constexpr uint8_t modifierRightShift = 0b0001;
constexpr uint8_t modifierAltKey = 21 - 1;
constexpr uint8_t modifierAlt = 0b0010;

// Num chars per key, Modulus for rotating through characters
static uint8_t TLoraPagerTapMod[_TCA8418_NUM_KEYS] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                                                      3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};

static unsigned char TLoraPagerTapMap[_TCA8418_NUM_KEYS][3] = {
    {'q', 'Q', '1'},    {'w', 'W', '2'},  {'e', 'E', '3'}, {'r', 'R', '4'},    {'t', 'T', '5'},
    {'y', 'Y', '6'},    {'u', 'U', '7'},  {'i', 'I', '8'}, {'o', 'O', '9'},    {'p', 'P', '0'},
    {'a', 'A', '*'},    {'s', 'S', '/'},  {'d', 'D', '+'}, {'f', 'F', '-'},    {'g', 'G', '='},
    {'h', 'H', ':'},    {'j', 'J', '\''}, {'k', 'K', '"'}, {'l', 'L', '@'},    {Key::SELECT, 0x00, Key::TAB},
    {0x00, 0x00, 0x00}, {'z', 'Z', '_'},  {'x', 'X', '$'}, {'c', 'C', ';'},    {'v', 'V', '?'},
    {'b', 'B', '!'},    {'n', 'N', ','},  {'m', 'M', '.'}, {0x00, 0x00, 0x00}, {Key::BSP, 0x00, Key::ESC},
    {' ', 0x00, 0x00}};

TLoraPagerKeyboard::TLoraPagerKeyboard()
    : TCA8418KeyboardBase(_TCA8418_ROWS, _TCA8418_COLS), modifierFlag(0), last_modifier_time(0), last_key(-1), next_key(-1),
      last_tap(0L), char_idx(0), tap_interval(0)
{
}

void TLoraPagerKeyboard::reset(void)
{
    TCA8418KeyboardBase::reset();
    pinMode(KB_BL_PIN, OUTPUT);
    setBacklight(false);
}

// handle multi-key presses (shift and alt)
void TLoraPagerKeyboard::trigger()
{
    uint8_t count = keyCount();
    if (count == 0)
        return;
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t k = readRegister(TCA8418_REG_KEY_EVENT_A + i);
        uint8_t key = k & 0x7F;
        if (k & 0x80) {
            pressed(key);
        } else {
            released();
            state = Idle;
        }
    }
}

void TLoraPagerKeyboard::pressed(uint8_t key)
{
    if (state == Init || state == Busy) {
        return;
    }
    hapticFeedback();

    if (modifierFlag && (millis() - last_modifier_time > _TCA8418_MULTI_TAP_THRESHOLD)) {
        modifierFlag = 0;
    }

    uint8_t next_key = 0;
    int row = (key - 1) / 10;
    int col = (key - 1) % 10;

    if (row >= _TCA8418_ROWS || col >= _TCA8418_COLS) {
        return; // Invalid key
    }

    next_key = row * _TCA8418_COLS + col;
    state = Held;

    uint32_t now = millis();
    tap_interval = now - last_tap;

    updateModifierFlag(next_key);
    if (isModifierKey(next_key)) {
        last_modifier_time = now;
    }

    if (tap_interval < 0) {
        last_tap = 0;
        state = Busy;
        return;
    }

    if (next_key != last_key || tap_interval > _TCA8418_MULTI_TAP_THRESHOLD) {
        char_idx = 0;
    } else {
        char_idx += 1;
    }

    last_key = next_key;
    last_tap = now;
}

void TLoraPagerKeyboard::released()
{
    if (state != Held) {
        return;
    }

    if (last_key < 0 || last_key >= _TCA8418_NUM_KEYS) {
        last_key = -1;
        state = Idle;
        return;
    }

    uint32_t now = millis();
    last_tap = now;

    if (TLoraPagerTapMap[last_key][modifierFlag % TLoraPagerTapMod[last_key]] == Key::BL_TOGGLE) {
        toggleBacklight();
        return;
    }

    queueEvent(TLoraPagerTapMap[last_key][modifierFlag % TLoraPagerTapMod[last_key]]);
    if (isModifierKey(last_key) == false)
        modifierFlag = 0;
}

void TLoraPagerKeyboard::hapticFeedback()
{
    drv.setWaveform(0, 14); // strong buzz 100%
    drv.setWaveform(1, 0);  // end waveform
    drv.go();
}

void TLoraPagerKeyboard::toggleBacklight(void)
{
    digitalWrite(KB_BL_PIN, !digitalRead(KB_BL_PIN));
}

void TLoraPagerKeyboard::updateModifierFlag(uint8_t key)
{
    if (key == modifierRightShiftKey) {
        modifierFlag ^= modifierRightShift;
    } else if (key == modifierAltKey) {
        modifierFlag ^= modifierAlt;
    }
}

bool TLoraPagerKeyboard::isModifierKey(uint8_t key)
{
    return (key == modifierRightShiftKey || key == modifierAltKey);
}

#endif