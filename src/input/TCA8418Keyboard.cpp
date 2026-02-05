#include "TCA8418Keyboard.h"

#define _TCA8418_COLS 3
#define _TCA8418_ROWS 4
#define _TCA8418_NUM_KEYS 12

#define _TCA8418_LONG_PRESS_THRESHOLD 2000
#define _TCA8418_MULTI_TAP_THRESHOLD 750

using Key = TCA8418KeyboardBase::TCA8418Key;

// Num chars per key, Modulus for rotating through characters
static uint8_t TCA8418TapMod[_TCA8418_NUM_KEYS] = {13, 7, 7, 7, 7, 7, 9, 7, 9, 2, 2, 2};

static unsigned char TCA8418TapMap[_TCA8418_NUM_KEYS][13] = {
    {'1', '.', ',', '?', '!', ':', ';', '-', '_', '\\', '/', '(', ')'}, // 1
    {'2', 'a', 'b', 'c', 'A', 'B', 'C'},                                // 2
    {'3', 'd', 'e', 'f', 'D', 'E', 'F'},                                // 3
    {'4', 'g', 'h', 'i', 'G', 'H', 'I'},                                // 4
    {'5', 'j', 'k', 'l', 'J', 'K', 'L'},                                // 5
    {'6', 'm', 'n', 'o', 'M', 'N', 'O'},                                // 6
    {'7', 'p', 'q', 'r', 's', 'P', 'Q', 'R', 'S'},                      // 7
    {'8', 't', 'u', 'v', 'T', 'U', 'V'},                                // 8
    {'9', 'w', 'x', 'y', 'z', 'W', 'X', 'Y', 'Z'},                      // 9
    {'*', '+'},                                                         // *
    {'0', ' '},                                                         // 0
    {'#', '@'},                                                         // #
};

static unsigned char TCA8418LongPressMap[_TCA8418_NUM_KEYS] = {
    Key::ESC,   // 1
    Key::UP,    // 2
    Key::NONE,  // 3
    Key::LEFT,  // 4
    Key::NONE,  // 5
    Key::RIGHT, // 6
    Key::NONE,  // 7
    Key::DOWN,  // 8
    Key::NONE,  // 9
    Key::BSP,   // *
    Key::NONE,  // 0
    Key::NONE,  // #
};

TCA8418Keyboard::TCA8418Keyboard()
    : TCA8418KeyboardBase(_TCA8418_ROWS, _TCA8418_COLS), last_key(-1), next_key(-1), last_tap(0L), char_idx(0), tap_interval(0),
      should_backspace(false)
{
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
    uint8_t next_key = 0;
    int row = (key - 1) / 10;
    int col = (key - 1) % 10;

    if (row >= _TCA8418_ROWS || col >= _TCA8418_COLS) {
        return; // Invalid key
    }

    // Compute key index based on dynamic row/column
    next_key = row * _TCA8418_COLS + col;

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
        should_backspace = false; // dont backspace on new key
    } else {
        char_idx += 1;           // Cycle through characters if same key pressed
        should_backspace = true; // allow backspace on same key
    }

    // Store the current key as the last key
    last_key = next_key;
    last_tap = now;
}

void TCA8418Keyboard::released()
{
    if (state != Held) {
        return;
    }

    if (last_key < 0 || last_key > _TCA8418_NUM_KEYS) { // reset to idle if last_key out of bounds
        last_key = -1;
        state = Idle;
        return;
    }
    uint32_t now = millis();
    int32_t held_interval = now - last_tap;
    last_tap = now;
    if (tap_interval < _TCA8418_MULTI_TAP_THRESHOLD && should_backspace) {
        queueEvent(BSP);
    }
    if (held_interval > _TCA8418_LONG_PRESS_THRESHOLD) {
        queueEvent(TCA8418LongPressMap[last_key]);
        // LOG_DEBUG("Long Press Key: %i Map: %i", last_key, TCA8418LongPressMap[last_key]);
    } else {
        queueEvent(TCA8418TapMap[last_key][(char_idx % TCA8418TapMod[last_key])]);
        // LOG_DEBUG("Key Press: %i Index:%i if %i Map: %c", last_key, char_idx, TCA8418TapMod[last_key],
        //           TCA8418TapMap[last_key][(char_idx % TCA8418TapMod[last_key])]);
    }
}

void TCA8418Keyboard::setBacklight(bool on)
{
    if (on) {
        digitalWrite(TCA8418_COL9, HIGH);
    } else {
        digitalWrite(TCA8418_COL9, LOW);
    }
}
