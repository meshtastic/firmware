
#if LAYOUT == NOKIA_5130    // Nokia 5130 keyboard size
  #define _TCA8418_COLS 5
  #define _TCA8418_ROWS 5
  #define _TCA8418_NUM_KEYS 25

  uint8_t TCA8418TapMod[_TCA8418_NUM_KEYS] = {1, 1, 1, 1, 13, 7, 9, 2, 7, 7, 7, 2, 7,  7, 9, 2}; // Num chars per key, Modulus for rotating through characters

  unsigned char TCA8418TapMap[_TCA8418_NUM_KEYS][13] = {
    {_TCA8418_BSP},                                                     // C
    {_TCA8418_SELECT},                                                  // Navi
    {_TCA8418_UP},                                                      // Up
    {_TCA8418_DOWN},                                                    // Down
    {'1', '.', ',', '?', '!', ':', ';', '-', '_', '\\', '/', '(', ')'}, // 1
    {'4', 'g', 'h', 'i', 'G', 'H', 'I'},                                // 4
    {'7', 'p', 'q', 'r', 's', 'P', 'Q', 'R', 'S'},                      // 7
    {'*', '+'},                                                         // *
    {'2', 'a', 'b', 'c', 'A', 'B', 'C'},                                // 2
    {'5', 'j', 'k', 'l', 'J', 'K', 'L'},                                // 5
    {'8', 't', 'u', 'v', 'T', 'U', 'V'},                                // 8
    {'0', ' '},                                                         // 0
    {'3', 'd', 'e', 'f', 'D', 'E', 'F'},                                // 3
    {'6', 'm', 'n', 'o', 'M', 'N', 'O'},                                // 6
    {'9', 'w', 'x', 'y', 'z', 'W', 'X', 'Y', 'Z'},                      // 9
    {'#', '@'}                                                          // #
  };

  unsigned char TCA8418LongPressMap[_TCA8418_NUM_KEYS] = {
    _TCA8418_ESC,    // C
    _TCA8418_NONE,   // Navi
    _TCA8418_NONE,   // Up
    _TCA8418_NONE,   // Down
    _TCA8418_NONE,   // 1
    _TCA8418_LEFT,   // 4
    _TCA8418_NONE,   // 7
    _TCA8418_NONE,   // *
    _TCA8418_UP,     // 2
    _TCA8418_NONE,   // 5
    _TCA8418_DOWN,   // 8
    _TCA8418_NONE,   // 0
    _TCA8418_NONE,   // 3
    _TCA8418_RIGHT,  // 6
    _TCA8418_NONE,   // 9
    _TCA8418_REBOOT, // #
  };




#elif LAYOUT == 3x4    // 3x4 Phone-style Matrix Keypad
  #define _TCA8418_COLS 3
  #define _TCA8418_ROWS 4
  #define _TCA8418_NUM_KEYS 12
  
  uint8_t TCA8418TapMod[_TCA8418_NUM_KEYS] = {13, 7, 7, 7, 7, 7, 9, 7, 9, 2, 2, 2}; // Num chars per key, Modulus for rotating through characters

  unsigned char TCA8418TapMap[_TCA8418_NUM_KEYS][13] = {
    {'1', '.', ',', '?', '!', ':', ';', '-', '_', '\\', '/', '(', ')'},  // 1
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

  unsigned char TCA8418LongPressMap[_TCA8418_NUM_KEYS] = {
    _TCA8418_ESC,    // 1
    _TCA8418_UP,     // 2
    _TCA8418_NONE,   // 3
    _TCA8418_LEFT,   // 4
    _TCA8418_NONE,   // 5
    _TCA8418_RIGHT,  // 6
    _TCA8418_NONE,   // 7
    _TCA8418_DOWN,   // 8
    _TCA8418_NONE,   // 9
    _TCA8418_NONE,   // *
    _TCA8418_NONE,   // 0
    _TCA8418_NONE,   // #
  };
#endif
