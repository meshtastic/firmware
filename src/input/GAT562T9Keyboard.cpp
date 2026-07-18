#include "GAT562T9Keyboard.h"

#if defined(GAT562_T9_KEYBOARD)

#include <Arduino.h>

#define GAT562_T9_COLS 3
#define GAT562_T9_ROWS 5
#define GAT562_T9_NUM_KEYS 12

#define GAT562_T9_LONG_PRESS_THRESHOLD 2000
#define GAT562_T9_MULTI_TAP_THRESHOLD 750

static uint8_t GAT562T9TapMod[GAT562_T9_NUM_KEYS] = {7, 7, 7, 7, 7, 7, 9, 7, 9, 2, 2, 2};

static unsigned char GAT562T9TapMap[GAT562_T9_NUM_KEYS][9] = {
    {',', '.', '!', '?', '<', '>', '1'},      // 1
    {'a', 'b', 'c', 'A', 'B', 'C', '2'},      // 2
    {'d', 'e', 'f', 'D', 'E', 'F', '3'},      // 3
    {'g', 'h', 'i', 'G', 'H', 'I', '4'},      // 4
    {'j', 'k', 'l', 'J', 'K', 'L', '5'},      // 5
    {'m', 'n', 'o', 'M', 'N', 'O', '6'},      // 6
    {'p', 'q', 'r', 's', 'P', 'Q', 'R', 'S', '7'}, // 7
    {'t', 'u', 'v', 'T', 'U', 'V', '8'},      // 8
    {'w', 'x', 'y', 'z', 'W', 'X', 'Y', 'Z', '9'}, // 9
    {'*', '+'},                               // *
    {' ', '0'},                               // 0
    {'#', '^'},                               // #
};

GAT562T9Keyboard::GAT562T9Keyboard()
    : TCA8418KeyboardBase(GAT562_T9_ROWS, GAT562_T9_COLS), last_key(UINT8_MAX), next_key(UINT8_MAX), last_tap(0L),
      char_idx(0), tap_interval(0), should_backspace(false)
{
}

void GAT562T9Keyboard::pressed(uint8_t key)
{
    if (state == Init || state == Busy) {
        return;
    }

    int row = (key - 1) / 10;
    int col = (key - 1) % 10;

    if (col >= GAT562_T9_COLS || row < 0 || row > 4 || row == 3) {
        return;
    }

    next_key = (uint8_t)((row < 3 ? row : 3) * GAT562_T9_COLS + col);

    state = Held;
    uint32_t now = millis();
    tap_interval = now - last_tap;
    if (tap_interval < 0) {
        last_tap = 0;
        state = Busy;
        return;
    }

    if (next_key != last_key || tap_interval > GAT562_T9_MULTI_TAP_THRESHOLD) {
        char_idx = 0;
        should_backspace = false;
    } else {
        char_idx += 1;
        should_backspace = true;
    }

    last_key = next_key;
    last_tap = now;
}

void GAT562T9Keyboard::released()
{
    if (state != Held) {
        return;
    }

    if (last_key >= GAT562_T9_NUM_KEYS) {
        last_key = UINT8_MAX;
        state = Idle;
        return;
    }

    uint32_t now = millis();
    int32_t held_interval = now - last_tap;
    last_tap = now;

    if (held_interval > GAT562_T9_LONG_PRESS_THRESHOLD) {
        queueEvent(GAT562T9TapMap[last_key][0]);
    } else {
        queueEvent(GAT562T9TapMap[last_key][char_idx % GAT562T9TapMod[last_key]]);
    }

    state = Idle;
}

#endif
