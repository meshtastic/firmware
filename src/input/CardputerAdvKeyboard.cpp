#if defined(M5STACK_CARDPUTER_ADV)

#include "CardputerAdvKeyboard.h"
#include "main.h"

#define _TCA8418_COLS 8
#define _TCA8418_ROWS 7
#define _TCA8418_NUM_KEYS 56

#define _TCA8418_MULTI_TAP_THRESHOLD 1500

using Key = TCA8418KeyboardBase::TCA8418Key;

constexpr uint8_t modifierShiftKey = 7 - 1; // keynum -1
constexpr uint8_t modifierRightShift = 0b0001;

constexpr uint8_t modifierFnKey = 3 - 1;
constexpr uint8_t modifierFn = 0b0010;

constexpr uint8_t modifierCtrlKey = 4 - 1;

constexpr uint8_t modifierOptKey = 8 - 1;

constexpr uint8_t modifierAltKey = 12 - 1;

// Num chars per key, Modulus for rotating through characters
// https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Sch_M5CardputerAdv_v1.0_2025_06_20_17_19_58_page_02.png
static uint8_t CardputerAdvTapMod[_TCA8418_NUM_KEYS] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                                                        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                                                        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                                                        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};

static unsigned char CardputerAdvTapMap[_TCA8418_NUM_KEYS][3] = {{'`', '~', Key::ESC},
                                                               {Key::TAB, 0x00, 0x00},
                                                               {0x00, 0x00, 0x00},          // Fn
                                                               {0x00, 0x00, 0x00},          // ctrl
                                                               {'1', '!', 0x00},
                                                               {'q', 'Q', Key::REBOOT},
                                                               {0x00, 0x00, 0x00},          // shift
                                                               {0x00, 0x00, 0x00},          // opt
                                                               {'2', '@', 0x00},
                                                               {'w', 'W', 0x00},
                                                               {'a', 'A', 0x00},
                                                               {0x00, 0x00, 0x00},          // alt
                                                               {'3', '#', 0x00},
                                                               {'e', 'E', 0x00},
                                                               {'s', 'S', 0x00},
                                                               {'z', 'Z', 0x00},
                                                               {'4', '$', 0x00},
                                                               {'r', 'R', 0x00},
                                                               {'d', 'D', 0x00},
                                                               {'x', 'X', 0x00},
                                                               {'5', '%', 0x00},
                                                               {'t', 'T', 0x00},
                                                               {'f', 'F', 0x00},
                                                               {'c', 'C', 0x00},
                                                               {'6', '^', 0x00},
                                                               {'y', 'Y', 0x00},
                                                               {'g', 'G', Key::GPS_TOGGLE},
                                                               {'v', 'V', 0x00},
                                                               {'7', '&', 0x00},
                                                               {'u', 'U', 0x00},
                                                               {'h', 'H', 0x00},
                                                               {'b', 'B', Key::BT_TOGGLE},
                                                               {'8', '*', 0x00},
                                                               {'i', 'I', 0x00},
                                                               {'j', 'J', 0x00},
                                                               {'n', 'n', 0x00},
                                                               {'9', '(', 0x00},
                                                               {'o', 'o', 0x00},
                                                               {'k', 'k', 0x00},
                                                               {'m', 'M', Key::MUTE_TOGGLE},
                                                               {'0', ')', 0x00},
                                                               {'p', 'P', Key::SEND_PING},
                                                               {'l', 'L', 0x00},
                                                               {',', '<', Key::LEFT},
                                                               {'_', '-', 0x00},
                                                               {'[', '{', 0x00},
                                                               {';', ':', Key::UP},
                                                               {'.', '>', Key::DOWN},
                                                               {'=', '+', 0x00},
                                                               {']', '}', 0x00},
                                                               {'\'', '"', 0x00},
                                                               {'/', '?', Key::RIGHT},
                                                               {Key::BSP, 0x00, 0x00},
                                                               {'\\', '|', 0x00},
                                                               {Key::SELECT, 0x00, 0x00},   // Enter
                                                               {' ', ' ', ' '}};            // Space

CardputerAdvKeyboard::CardputerAdvKeyboard()
    : TCA8418KeyboardBase(_TCA8418_ROWS, _TCA8418_COLS), modifierFlag(0), last_modifier_time(0), last_key(-1), next_key(-1),
      last_tap(0L), char_idx(0), tap_interval(0)
{
    reset();
}

void CardputerAdvKeyboard::reset(void)
{
    TCA8418KeyboardBase::reset();
}

// handle multi-key presses (shift and alt)
void CardputerAdvKeyboard::trigger()
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

void CardputerAdvKeyboard::pressed(uint8_t key)
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

void CardputerAdvKeyboard::released()
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

    if (CardputerAdvTapMap[last_key][modifierFlag % CardputerAdvTapMod[last_key]] == Key::BL_TOGGLE) {
        return;
    }

    queueEvent(CardputerAdvTapMap[last_key][modifierFlag % CardputerAdvTapMod[last_key]]);
    if (isModifierKey(last_key) == false)
        modifierFlag = 0;
}

void CardputerAdvKeyboard::updateModifierFlag(uint8_t key)
{
    if (key == modifierShiftKey) {
        modifierFlag ^= modifierRightShift;
    } else if (key == modifierFnKey) {
        modifierFlag ^= modifierFn;
    } else if (key == modifierCtrlKey) {
        //modifierFlag ^= modifierCtrl;
    } else if (key == modifierOptKey) {
        //modifierFlag ^= modifierOpt;
    } else if (key == modifierAltKey) {
        //modifierFlag ^= modifierAlt;
    }
}

bool CardputerAdvKeyboard::isModifierKey(uint8_t key)
{
    return (key == modifierShiftKey || key == modifierFnKey);
}

#endif