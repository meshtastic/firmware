#include "TCA8418Keyboard.h"
#include "modules/CannedMessageModule.h"

#define _TCA8418_COLS 3
#define _TCA8418_ROWS 4
#define _TCA8418_NUM_KEYS 12

#define _TCA8418_LONG_PRESS_THRESHOLD 1000
#define _TCA8418_LONG_PRESS_REPEAT_INTERVAL 125
#define _TCA8418_MULTI_TAP_THRESHOLD 750

using Key = TCA8418KeyboardBase::TCA8418Key;

// Num chars per key, Modulus for rotating through characters
static uint8_t TCA8418TapMod[_TCA8418_NUM_KEYS] = {13, 7, 7, 7, 7, 7, 9, 7, 9, 1, 2, 4};

static unsigned char TCA8418TapMap[_TCA8418_NUM_KEYS][13] = {
    {'.', ',', '?', '!', '1', ':', ';', '-', '_', '\\', '/', '(', ')'}, // 1
    {'a', 'b', 'c', '2', 'A', 'B', 'C'},                                // 2
    {'d', 'e', 'f', '3', 'D', 'E', 'F'},                                // 3
    {'g', 'h', 'i', '4', 'G', 'H', 'I'},                                // 4
    {'j', 'k', 'l', '5', 'J', 'K', 'L'},                                // 5
    {'m', 'n', 'o', '6', 'M', 'N', 'O'},                                // 6
    {'p', 'q', 'r', 's', '7', 'P', 'Q', 'R', 'S'},                      // 7
    {'t', 'u', 'v', '8', 'T', 'U', 'V'},                                // 8
    {'w', 'x', 'y', 'z', '9', 'W', 'X', 'Y', 'Z'},                      // 9
    {Key::BSP},                                                         // *
    {' ', '0'},                                                         // 0
    {'#', '@', '*', '+'},                                               // #
};

static unsigned char TCA8418LongPressMap[_TCA8418_NUM_KEYS] = {
    Key::ESC,   // 1
    Key::UP,    // 2
    Key::TAB,  // 3
    Key::LEFT,  // 4
    Key::SELECT,  // 5
    Key::RIGHT, // 6
    Key::NONE,  // 7
    Key::DOWN,  // 8
    Key::NONE,  // 9
    Key::BSP,   // *
    Key::NONE,   // 0
    Key::NONE  // #
};

// key assignment when not in a free text entering state
static unsigned char TCA8418NavMap[_TCA8418_NUM_KEYS] = {
    Key::ESC,   // 1
    Key::UP,    // 2
    Key::OPEN_FREETEXT,  // 3
    Key::LEFT,  // 4
    Key::SELECT,  // 5
    Key::RIGHT, // 6
    Key::NONE,  // 7
    Key::DOWN,  // 8
    Key::NONE,  // 9
    Key::BT_TOGGLE,   // *
    Key::GPS_TOGGLE,  // 0
    Key::MUTE_TOGGLE  // #
};


static bool isRepeatable(Key key)
{
    return key == Key::UP || key == Key::DOWN || key == Key::LEFT || key == Key::RIGHT || key == Key::BSP;
}

static Key getRepeatKey(uint8_t key_index, bool is_char_input_allowed)
{
    if (key_index >= _TCA8418_NUM_KEYS) {
        return Key::NONE;
    }

    Key repeat_key = static_cast<Key>(is_char_input_allowed ? TCA8418LongPressMap[key_index] : TCA8418NavMap[key_index]);
    return isRepeatable(repeat_key) ? repeat_key : Key::NONE;
}

TCA8418Keyboard::TCA8418Keyboard()
    : TCA8418KeyboardBase(_TCA8418_ROWS, _TCA8418_COLS), last_key(UINT8_MAX), next_key(UINT8_MAX), last_tap(0L), char_idx(0),
            tap_interval(0), should_backspace(false)
{
        press_started_at = 0;
        last_repeat = 0;
        is_repeating_long_press = false;
}

void TCA8418Keyboard::reset()
{
    TCA8418KeyboardBase::reset();

    // Set COL9 as GPIO output
    writeRegister(TCA8418_REG_GPIO_DIR_3, 0x02);
    // Switch off keyboard backlight (COL9 = LOW)
    writeRegister(TCA8418_REG_GPIO_DAT_OUT_3, 0x00);
}

void TCA8418Keyboard::pressed(uint8_t key)
{
    if (state == Init || state == Busy) {
        return;
    }
    int row = (key - 1) / 10;
    int col = (key - 1) % 10;

    if (row >= _TCA8418_ROWS || col >= _TCA8418_COLS) {
        return; // Invalid key
    }

    // Compute key index based on dynamic row/column
    next_key = (uint8_t)(row * _TCA8418_COLS + col);

    // LOG_DEBUG("TCA8418: Key %u -> Next Key %u", key, next_key);

    state = Held;
    uint32_t now = millis();
    tap_interval = now - last_tap;
    if (tap_interval < 0) {
        // Long running, millis has overflowed.
        last_tap = 0;
        state = Busy;
        return;
    }

    // Check if the key is the same as the last one or if the time interval has passed
    if (next_key != last_key || tap_interval > _TCA8418_MULTI_TAP_THRESHOLD) {
        char_idx = 0;             // Reset char index if new key or long press
        should_backspace = false; // don't backspace on new key
    } else {
        char_idx += 1;           // Cycle through characters if same key pressed
        should_backspace = true; // allow backspace on same key
    }

    // Store the current key as the last key
    last_key = next_key;
    last_tap = now;
    press_started_at = now;
    last_repeat = now;
    is_repeating_long_press = false;
}

void TCA8418Keyboard::held()
{
    if (state != Held || last_key >= _TCA8418_NUM_KEYS) {
        return;
    }

    bool is_char_input_allowed = cannedMessageModule && cannedMessageModule->isCharInputAllowed();
    unsigned char repeat_key = getRepeatKey(last_key, is_char_input_allowed);
    if (repeat_key == Key::NONE) {
        return;
    }

    uint32_t now = millis();
    uint32_t held_interval = now - press_started_at;
    if (held_interval <= _TCA8418_LONG_PRESS_THRESHOLD) {
        return;
    }

    if (!is_repeating_long_press || (now - last_repeat) >= _TCA8418_LONG_PRESS_REPEAT_INTERVAL) {
        queueEvent(repeat_key);
        is_repeating_long_press = true;
        last_repeat = now;
    }
}

void TCA8418Keyboard::released()
{
    if (state != Held) {
        return;
    }

    if (last_key >= _TCA8418_NUM_KEYS) { // reset to idle if last_key out of bounds
        last_key = UINT8_MAX;
        state = Idle;
        return;
    }

    uint32_t now = millis();
    int32_t held_interval = now - press_started_at;
    last_tap = now;

    bool is_char_input_allowed = cannedMessageModule && cannedMessageModule->isCharInputAllowed();
    if (is_char_input_allowed) {
        if (tap_interval < _TCA8418_MULTI_TAP_THRESHOLD && should_backspace && TCA8418TapMap[last_key][(char_idx % TCA8418TapMod[last_key])] != Key::BSP) {
            queueEvent(BSP);
        } 
        if (held_interval > _TCA8418_LONG_PRESS_THRESHOLD) {
            if (!is_repeating_long_press) {
                queueEvent(TCA8418LongPressMap[last_key]);
                // LOG_DEBUG("Long Press Key: %i Map: %i", last_key, TCA8418LongPressMap[last_key]);
            }
        } else {
            queueEvent(TCA8418TapMap[last_key][(char_idx % TCA8418TapMod[last_key])]);
            // LOG_DEBUG("Key Press: %i Index:%i if %i Map: %c", last_key, char_idx, TCA8418TapMod[last_key],
            //           TCA8418TapMap[last_key][(char_idx % TCA8418TapMod[last_key])]);
        }
    } else if (!is_repeating_long_press) {
        queueEvent(TCA8418NavMap[last_key]);
        // LOG_DEBUG("Long Press Key: %i Map: %i", last_key, TCA8418LongPressMap[last_key]);
    }
    is_repeating_long_press = false;
}

void TCA8418Keyboard::setBacklight(bool on)
{
    if (on) {
        digitalWrite(TCA8418_COL9, HIGH);
    } else {
        digitalWrite(TCA8418_COL9, LOW);
    }
}
