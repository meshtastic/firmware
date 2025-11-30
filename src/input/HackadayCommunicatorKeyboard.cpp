#if defined(HACKADAY_COMMUNICATOR)

#include "HackadayCommunicatorKeyboard.h"
#include "main.h"

#define _TCA8418_COLS 10
#define _TCA8418_ROWS 8
#define _TCA8418_NUM_KEYS 80

#define _TCA8418_MULTI_TAP_THRESHOLD 1500

using Key = TCA8418KeyboardBase::TCA8418Key;

constexpr uint8_t modifierRightShiftKey = 30;
constexpr uint8_t modifierRightShift = 0b0001;
constexpr uint8_t modifierLeftShiftKey = 76; // keynum -1
constexpr uint8_t modifierLeftShift = 0b0001;
// constexpr uint8_t modifierSymKey = 42;
// constexpr uint8_t modifierSym = 0b0010;

// Num chars per key, Modulus for rotating through characters
static uint8_t HackadayCommunicatorTapMod[_TCA8418_NUM_KEYS] = {
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    0, 0, 0, 1, 1, 0, 1, 1, 1, 0, 0, 0, 1, 2, 2, 2, 2, 2, 2, 0, 0, 0, 1, 2, 2, 2, 1, 2, 2, 0, 0, 0, 2, 1, 2, 2, 0, 1, 1, 0,
};

static unsigned char HackadayCommunicatorTapMap[_TCA8418_NUM_KEYS][2] = {{},
                                                                         {},
                                                                         {'+'},
                                                                         {'9'},
                                                                         {'8'},
                                                                         {'7'},
                                                                         {'2'},
                                                                         {'3'},
                                                                         {'4'},
                                                                         {'5'},
                                                                         {Key::ESC},
                                                                         {'q', 'Q'},
                                                                         {'w', 'W'},
                                                                         {'e', 'E'},
                                                                         {'r', 'R'},
                                                                         {'t', 'T'},
                                                                         {'y', 'Y'},
                                                                         {'u', 'U'},
                                                                         {'i', 'I'},
                                                                         {'o', 'O'},
                                                                         {Key::TAB},
                                                                         {'a', 'A'},
                                                                         {'s', 'S'},
                                                                         {'d', 'D'},
                                                                         {'f', 'F'},
                                                                         {'g', 'G'},
                                                                         {'h', 'H'},
                                                                         {'j', 'J'},
                                                                         {'k', 'K'},
                                                                         {'l', 'L'},
                                                                         {},
                                                                         {'z', 'Z'},
                                                                         {'x', 'X'},
                                                                         {'c', 'C'},
                                                                         {'v', 'V'},
                                                                         {'b', 'B'},
                                                                         {'n', 'N'},
                                                                         {'m', 'M'},
                                                                         {',', '<'},
                                                                         {'.', '>'},
                                                                         {},
                                                                         {},
                                                                         {},
                                                                         {'\\'},
                                                                         {' '},
                                                                         {},
                                                                         {Key::RIGHT},
                                                                         {Key::DOWN},
                                                                         {Key::LEFT},
                                                                         {},
                                                                         {},
                                                                         {},
                                                                         {'-'},
                                                                         {'6', '^'},
                                                                         {'5', '%'},
                                                                         {'4', '$'},
                                                                         {'[', '{'},
                                                                         {']', '}'},
                                                                         {'p', 'P'},
                                                                         {},
                                                                         {},
                                                                         {},
                                                                         {'*'},
                                                                         {'3', '#'},
                                                                         {'2', '@'},
                                                                         {'1', '!'},
                                                                         {Key::SELECT},
                                                                         {'\'', '"'},
                                                                         {';', ':'},
                                                                         {},
                                                                         {},
                                                                         {},
                                                                         {'/', '?'},
                                                                         {'='},
                                                                         {'.', '>'},
                                                                         {'0', ')'},
                                                                         {},
                                                                         {Key::UP},
                                                                         {Key::BSP},
                                                                         {}};

HackadayCommunicatorKeyboard::HackadayCommunicatorKeyboard()
    : TCA8418KeyboardBase(_TCA8418_ROWS, _TCA8418_COLS), modifierFlag(0), last_modifier_time(0), last_key(-1), next_key(-1),
      last_tap(0L), char_idx(0), tap_interval(0)
{
    reset();
}

void HackadayCommunicatorKeyboard::reset(void)
{
    TCA8418KeyboardBase::reset();
    enableInterrupts();
}

// handle multi-key presses (shift and alt)
void HackadayCommunicatorKeyboard::trigger()
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

void HackadayCommunicatorKeyboard::pressed(uint8_t key)
{
    if (state == Init || state == Busy) {
        return;
    }

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

void HackadayCommunicatorKeyboard::released()
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
    if (HackadayCommunicatorTapMod[last_key])
        queueEvent(HackadayCommunicatorTapMap[last_key][modifierFlag % HackadayCommunicatorTapMod[last_key]]);
    if (isModifierKey(last_key) == false)
        modifierFlag = 0;
}

void HackadayCommunicatorKeyboard::updateModifierFlag(uint8_t key)
{
    if (key == modifierRightShiftKey) {
        modifierFlag ^= modifierRightShift;
    } else if (key == modifierLeftShiftKey) {
        modifierFlag ^= modifierLeftShift;
    }
}

bool HackadayCommunicatorKeyboard::isModifierKey(uint8_t key)
{
    return (key == modifierRightShiftKey || key == modifierLeftShiftKey);
}

#endif