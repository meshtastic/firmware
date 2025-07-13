#pragma once

#define SATELLITE_IMAGE_WIDTH 16
#define SATELLITE_IMAGE_HEIGHT 15
const uint8_t SATELLITE_IMAGE[] PROGMEM = {0x00, 0x08, 0x00, 0x1C, 0x00, 0x0E, 0x20, 0x07, 0x70, 0x02,
                                           0xF8, 0x00, 0xF0, 0x01, 0xE0, 0x03, 0xC8, 0x01, 0x9C, 0x54,
                                           0x0E, 0x52, 0x07, 0x48, 0x02, 0x26, 0x00, 0x10, 0x00, 0x0E};

#define imgSatellite_width 8
#define imgSatellite_height 8
const uint8_t imgSatellite[] PROGMEM = {
    0b00000000, 0b00000000, 0b00000000, 0b00011000, 0b11011011, 0b11111111, 0b11011011, 0b00011000,
};

const uint8_t imgUSB[] PROGMEM = {0x00, 0xfc, 0xf0, 0xfc, 0x88, 0xff, 0x86, 0xfe, 0x85, 0xfe, 0x89, 0xff, 0xf1, 0xfc, 0x00, 0xfc};
const uint8_t imgUSB_HighResolution[] PROGMEM = {0x00, 0x3e, 0xf8, 0x80, 0x43, 0xf8, 0xc0, 0xc2, 0xff, 0x60, 0x42, 0xfc,
                                                 0x3c, 0xc2, 0xff, 0x22, 0x42, 0xf8, 0x3d, 0x42, 0xf8, 0x22, 0xc2, 0xff,
                                                 0x61, 0x42, 0xfc, 0xc0, 0xc2, 0xff, 0x80, 0x43, 0xf8, 0x00, 0x3e, 0xf8};
const uint8_t imgPower[] PROGMEM = {0x40, 0x40, 0x40, 0x58, 0x48, 0x08, 0x08, 0x08,
                                    0x1C, 0x22, 0x22, 0x41, 0x7F, 0x22, 0x22, 0x22};
const uint8_t imgUser[] PROGMEM = {0x3C, 0x42, 0x99, 0xA5, 0xA5, 0x99, 0x42, 0x3C};
const uint8_t imgPositionEmpty[] PROGMEM = {0x20, 0x30, 0x28, 0x24, 0x42, 0xFF};
const uint8_t imgPositionSolid[] PROGMEM = {0x20, 0x30, 0x38, 0x3C, 0x7E, 0xFF};

const uint8_t bluetoothConnectedIcon[36] PROGMEM = {0xfe, 0x01, 0xff, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00, 0xe3, 0x1f,
                                                    0xf3, 0x3f, 0x33, 0x30, 0x33, 0x33, 0x33, 0x33, 0x03, 0x33, 0xff, 0x33,
                                                    0xfe, 0x31, 0x00, 0x30, 0x30, 0x30, 0x30, 0x30, 0xf0, 0x3f, 0xe0, 0x1f};

#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) ||      \
     defined(ST7789_CS) || defined(USE_ST7789) || defined(HX8357_CS) || defined(ILI9488_CS) || ARCH_PORTDUINO) &&                \
    !defined(DISPLAY_FORCE_SMALL_FONTS)
const uint8_t imgQuestionL1[] PROGMEM = {0xff, 0x01, 0x01, 0x32, 0x7b, 0x49, 0x49, 0x6f, 0x26, 0x01, 0x01, 0xff};
const uint8_t imgQuestionL2[] PROGMEM = {0x0f, 0x08, 0x08, 0x08, 0x06, 0x0f, 0x0f, 0x06, 0x08, 0x08, 0x08, 0x0f};
const uint8_t imgInfoL1[] PROGMEM = {0xff, 0x01, 0x01, 0x01, 0x1e, 0x7f, 0x1e, 0x01, 0x01, 0x01, 0x01, 0xff};
const uint8_t imgInfoL2[] PROGMEM = {0x0f, 0x08, 0x08, 0x08, 0x06, 0x0f, 0x0f, 0x06, 0x08, 0x08, 0x08, 0x0f};
const uint8_t imgSFL1[] PROGMEM = {0xb6, 0x8f, 0x19, 0x11, 0x31, 0xe3, 0xc2, 0x01,
                                   0x01, 0xf9, 0xf9, 0x89, 0x89, 0x89, 0x09, 0xeb};
const uint8_t imgSFL2[] PROGMEM = {0x0e, 0x09, 0x09, 0x09, 0x09, 0x09, 0x08, 0x08,
                                   0x00, 0x0f, 0x0f, 0x00, 0x08, 0x08, 0x08, 0x0f};
#else
const uint8_t imgInfo[] PROGMEM = {0xff, 0x81, 0x00, 0xfb, 0xfb, 0x00, 0x81, 0xff};
const uint8_t imgQuestion[] PROGMEM = {0xbf, 0x41, 0xc0, 0x8b, 0xdb, 0x70, 0xa1, 0xdf};
const uint8_t imgSF[] PROGMEM = {0xd2, 0xb7, 0xad, 0xbb, 0x92, 0x01, 0xfd, 0xfd, 0x15, 0x85, 0xf5};
#endif

// === Horizontal battery  ===
// Basic battery design and all related pieces
const unsigned char batteryBitmap_h_bottom[] PROGMEM = {
    0b00011110, 0b00000000, 0b00000001, 0b00000000, 0b00000001, 0b00000000, 0b00000001, 0b00000000, 0b00000001,
    0b00000000, 0b00000001, 0b00000000, 0b00000001, 0b00000000, 0b00000001, 0b00000000, 0b00000001, 0b00000000,
    0b00000001, 0b00000000, 0b00000001, 0b00000000, 0b00000001, 0b00000000, 0b00011110, 0b00000000};

const unsigned char batteryBitmap_h_top[] PROGMEM = {
    0b00111100, 0b00000000, 0b01000000, 0b00000000, 0b01000000, 0b00000000, 0b01000000, 0b00000000, 0b01000000,
    0b00000000, 0b11000000, 0b00000000, 0b11000000, 0b00000000, 0b11000000, 0b00000000, 0b01000000, 0b00000000,
    0b01000000, 0b00000000, 0b01000000, 0b00000000, 0b01000000, 0b00000000, 0b00111100, 0b00000000};

// Lightning Bolt
const unsigned char lightning_bolt_h[] PROGMEM = {
    0b00000000, 0b00000000, 0b00100000, 0b00000000, 0b00110000, 0b00000000, 0b00111000, 0b00000000, 0b00111100,
    0b00000000, 0b00011110, 0b00000000, 0b11111111, 0b00000000, 0b01111000, 0b00000000, 0b00111100, 0b00000000,
    0b00011100, 0b00000000, 0b00001100, 0b00000000, 0b00000100, 0b00000000, 0b00000000, 0b00000000};

// === Vertical battery ===
// Basic battery design and all related pieces
const unsigned char batteryBitmap_v[] PROGMEM = {0b00011100, 0b00111110, 0b01000001, 0b01000001, 0b00000000, 0b00000000,
                                                 0b00000000, 0b01000001, 0b01000001, 0b01000001, 0b00111110};
// This is the left and right bars for the fill in
const unsigned char batteryBitmap_sidegaps_v[] PROGMEM = {0b10000010, 0b10000010, 0b10000010};
// Lightning Bolt
const unsigned char lightning_bolt_v[] PROGMEM = {0b00000100, 0b00000110, 0b00011111, 0b00001100, 0b00000100};

#define mail_width 10
#define mail_height 7
static const unsigned char mail[] PROGMEM = {
    0b11111111, 0b00, // Top line
    0b10000001, 0b00, // Edges
    0b11000011, 0b00, // Diagonals start
    0b10100101, 0b00, // Inner M part
    0b10011001, 0b00, // Inner M part
    0b10000001, 0b00, // Edges
    0b11111111, 0b00  // Bottom line
};

// 📬 Mail / Message
const uint8_t icon_mail[] PROGMEM = {
    0b11111111, // ████████ top border
    0b10000001, // █      █ sides
    0b11000011, // ██    ██ diagonal
    0b10100101, // █ █  █ █ inner M
    0b10011001, // █  ██  █ inner M
    0b10000001, // █      █ sides
    0b10000001, // █      █ sides
    0b11111111  // ████████ bottom
};

// 📍 GPS Screen / Location Pin
const unsigned char icon_compass[] PROGMEM = {
    0x3C, // Row 0: ..####..
    0x52, // Row 1: .#..#.#.
    0x91, // Row 2: #...#..#
    0x91, // Row 3: #...#..#
    0x91, // Row 4: #...#..#
    0x81, // Row 5: #......#
    0x42, // Row 6: .#....#.
    0x3C  // Row 7: ..####..
};

const uint8_t icon_radio[] PROGMEM = {
    0x0F, // Row 0: ####....
    0x10, // Row 1: ....#...
    0x27, // Row 2: ###..#..
    0x48, // Row 3: ...#..#.
    0x93, // Row 4: ##..#..#
    0xA4, // Row 5: ..#..#.#
    0xA8, // Row 6: ...#.#.#
    0xA9  // Row 7: #..#.#.#
};

// 🪙 Memory Icon
const uint8_t icon_memory[] PROGMEM = {
    0x24, // Row 0: ..#..#..
    0x3C, // Row 1: ..####..
    0xC3, // Row 2: ##....##
    0x5A, // Row 3: .#.##.#.
    0x5A, // Row 4: .#.##.#.
    0xC3, // Row 5: ##....##
    0x3C, // Row 6: ..####..
    0x24  // Row 7: ..#..#..
};

// 🌐 Wi-Fi
const uint8_t icon_wifi[] PROGMEM = {0b00000000, 0b00011000, 0b00111100, 0b01111110,
                                     0b11011011, 0b00011000, 0b00011000, 0b00000000};

const uint8_t icon_nodes[] PROGMEM = {
    0xF9, // Row 0  #..#######
    0x00, // Row 1
    0xF9, // Row 2  #..#######
    0x00, // Row 3
    0xF9, // Row 4  #..#######
    0x00, // Row 5
    0xF9, // Row 6  #..#######
    0x00  // Row 7
};

// ➤ Chevron Triangle Arrow Icon (8x8)
const uint8_t icon_list[] PROGMEM = {
    0x10, // Row 0: ...#....
    0x10, // Row 1: ...#....
    0x38, // Row 2: ..###...
    0x38, // Row 3: ..###...
    0x7C, // Row 4: .#####..
    0x6C, // Row 5: .##.##..
    0xC6, // Row 6: ##...##.
    0x82  // Row 7: #.....#.
};

// 📶 Signal Bars Icon (left to right, small to large with spacing)
const uint8_t icon_signal[] PROGMEM = {
    0b00000000, // ░░░░░░░
    0b10000000, // ░░░░░░░
    0b10100000, // ░░░░█░█
    0b10100000, // ░░░░█░█
    0b10101000, // ░░█░█░█
    0b10101000, // ░░█░█░█
    0b10101010, // █░█░█░█
    0b11111111  // ███████
};

// ↔️ Distance / Measurement Icon (double-ended arrow)
const uint8_t icon_distance[] PROGMEM = {
    0b00000000, // ░░░░░░░░
    0b10000001, // █░░░░░█ arrowheads
    0b01000010, // ░█░░░█░
    0b00100100, // ░░█░█░░
    0b00011000, // ░░░██░░ center
    0b00100100, // ░░█░█░░
    0b01000010, // ░█░░░█░
    0b10000001  // █░░░░░█
};

// ⚠️ Error / Fault
const uint8_t icon_error[] PROGMEM = {
    0b00011000, // ░░░██░░░
    0b00011000, // ░░░██░░░
    0b00011000, // ░░░██░░░
    0b00011000, // ░░░██░░░
    0b00000000, // ░░░░░░░░
    0b00011000, // ░░░██░░░
    0b00000000, // ░░░░░░░░
    0b00000000  // ░░░░░░░░
};

// 🏠 Optimized Home Icon (8x8)
const uint8_t icon_home[] PROGMEM = {
    0b00011000, //    ██
    0b00111100, //   ████
    0b01111110, //  ██████
    0b11111111, // ███████
    0b11000011, // ██    ██
    0b11011011, // ██ ██ ██
    0b11011011, // ██ ██ ██
    0b11111111  // ███████
};

// 🔧 Generic module (gear-like shape)
const uint8_t icon_module[] PROGMEM = {
    0b00011000, // ░░░██░░░
    0b00111100, // ░░████░░
    0b01111110, // ░██████░
    0b11011011, // ██░██░██
    0b11011011, // ██░██░██
    0b01111110, // ░██████░
    0b00111100, // ░░████░░
    0b00011000  // ░░░██░░░
};

#define mute_symbol_width 8
#define mute_symbol_height 8
const uint8_t mute_symbol[] PROGMEM = {
    0b00011001, // █
    0b00100110, //  █
    0b00100100, //   ████
    0b01001010, //  █ █  █
    0b01010010, //  █  █ █
    0b01100010, // ████████
    0b11111111, //    █  █
    0b10011000, //        █
};

#define mute_symbol_big_width 16
#define mute_symbol_big_height 16
const uint8_t mute_symbol_big[] PROGMEM = {0b00000001, 0b00000000, 0b11000010, 0b00000011, 0b00110100, 0b00001100, 0b00011000,
                                           0b00001000, 0b00011000, 0b00010000, 0b00101000, 0b00010000, 0b01001000, 0b00010000,
                                           0b10001000, 0b00010000, 0b00001000, 0b00010001, 0b00001000, 0b00010010, 0b00001000,
                                           0b00010100, 0b00000100, 0b00101000, 0b11111100, 0b00111111, 0b01000000, 0b00100010,
                                           0b10000000, 0b01000001, 0b00000000, 0b10000000};

// Bell icon for Alert Message
#define bell_alert_width 8
#define bell_alert_height 8
const unsigned char bell_alert[] PROGMEM = {0b00011000, 0b00100100, 0b00100100, 0b01000010,
                                            0b01000010, 0b01000010, 0b11111111, 0b00011000};

#define key_symbol_width 8
#define key_symbol_height 8
const uint8_t key_symbol[] PROGMEM = {0b00000000, 0b00000000, 0b00000110, 0b11111001,
                                      0b10101001, 0b10000110, 0b00000000, 0b00000000};

#define placeholder_width 8
#define placeholder_height 8
const uint8_t placeholder[] PROGMEM = {0b11111111, 0b11111111, 0b11111111, 0b11111111,
                                       0b11111111, 0b11111111, 0b11111111, 0b11111111};

#define icon_node_width 8
#define icon_node_height 8
static const uint8_t icon_node[] PROGMEM = {
    0x10, //    #
    0x10, //    #     ← antenna
    0x10, //    #
    0xFE, // #######  ← device top
    0x82, // #     #
    0xAA, // # # # #  ← body with pattern
    0x92, // #  #  #
    0xFE  // #######  ← device base
};

#define bluetoothdisabled_width 8
#define bluetoothdisabled_height 8
const uint8_t bluetoothdisabled[] PROGMEM = {0b11101100, 0b01010100, 0b01001100, 0b01010100,
                                             0b01001100, 0b00000000, 0b00000000, 0b00000000};

#define smallbulletpoint_width 8
#define smallbulletpoint_height 8
const uint8_t smallbulletpoint[] PROGMEM = {0b00000011, 0b00000011, 0b00000000, 0b00000000,
                                            0b00000000, 0b00000000, 0b00000000, 0b00000000};

// Digital Clock
#define digital_icon_clock_width 8
#define digital_icon_clock_height 8
const uint8_t digital_icon_clock[] PROGMEM = {0b00111100, 0b01000010, 0b10000101, 0b10101001,
                                              0b10010001, 0b10000001, 0b01000010, 0b00111100};
// Analog Clock
#define analog_icon_clock_width 8
#define analog_icon_clock_height 8
const uint8_t analog_icon_clock[] PROGMEM = {0b11111111, 0b01000010, 0b00100100, 0b00011000,
                                             0b00100100, 0b01000010, 0b01000010, 0b11111111};

#include "img/icon.xbm"
static_assert(sizeof(icon_bits) >= 0, "Silence unused variable warning");