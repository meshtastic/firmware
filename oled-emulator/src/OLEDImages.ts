/**
 * OLEDImages.ts - Icon and image data for OLED display emulator
 *
 * These are direct ports of the XBM/bitmap icons from the Meshtastic firmware.
 * Format: XBM style (LSB first, horizontal scanning)
 */

// Satellite icon (8x8)
export const imgSatellite_width = 8;
export const imgSatellite_height = 8;
export const imgSatellite = new Uint8Array([
  0b00000000, 0b00000000, 0b00000000, 0b00011000, 0b11011011, 0b11111111,
  0b11011011, 0b00011000,
]);

// USB icon (10x8)
export const imgUSB = new Uint8Array([
  0x00, 0xfc, 0xf0, 0xfc, 0x88, 0xff, 0x86, 0xfe, 0x85, 0xfe, 0x89, 0xff, 0xf1,
  0xfc, 0x00, 0xfc,
]);

// Power icon (8x16)
export const imgPower = new Uint8Array([
  0x40, 0x40, 0x40, 0x58, 0x48, 0x08, 0x08, 0x08, 0x1c, 0x22, 0x22, 0x41, 0x7f,
  0x22, 0x22, 0x22,
]);

// User icon (8x8)
export const imgUser = new Uint8Array([
  0x3c, 0x42, 0x99, 0xa5, 0xa5, 0x99, 0x42, 0x3c,
]);

// Position empty arrow (6x8)
export const imgPositionEmpty = new Uint8Array([
  0x20, 0x30, 0x28, 0x24, 0x42, 0xff,
]);

// Position solid arrow (6x8)
export const imgPositionSolid = new Uint8Array([
  0x20, 0x30, 0x38, 0x3c, 0x7e, 0xff,
]);

// Mail icon (10x7)
export const mail_width = 10;
export const mail_height = 7;
export const mail = new Uint8Array([
  0b11111111,
  0b00, // Top line
  0b10000001,
  0b00, // Edges
  0b11000011,
  0b00, // Diagonals start
  0b10100101,
  0b00, // Inner M part
  0b10011001,
  0b00, // Inner M part
  0b10000001,
  0b00, // Edges
  0b11111111,
  0b00, // Bottom line
]);

// Hop icon (9x10)
export const hop_width = 9;
export const hop_height = 10;
export const hop = new Uint8Array([
  0x05, 0x00, 0x07, 0x00, 0x05, 0x00, 0x38, 0x00, 0x28, 0x00, 0x38, 0x00, 0xc0,
  0x01, 0x40, 0x01, 0xc0, 0x01, 0x40, 0x00,
]);

// Mail icon (8x8)
export const icon_mail = new Uint8Array([
  0b11111111, // ████████ top border
  0b10000001, // █      █ sides
  0b11000011, // ██    ██ diagonal
  0b10100101, // █ █  █ █ inner M
  0b10011001, // █  ██  █ inner M
  0b10000001, // █      █ sides
  0b10000001, // █      █ sides
  0b11111111, // ████████ bottom
]);

// Compass icon (8x8)
export const icon_compass = new Uint8Array([
  0x3c, // Row 0: ..####..
  0x52, // Row 1: .#..#.#.
  0x91, // Row 2: #...#..#
  0x91, // Row 3: #...#..#
  0x91, // Row 4: #...#..#
  0x81, // Row 5: #......#
  0x42, // Row 6: .#....#.
  0x3c, // Row 7: ..####..
]);

// Radio icon (8x8)
export const icon_radio = new Uint8Array([
  0x0f, // Row 0: ####....
  0x10, // Row 1: ....#...
  0x27, // Row 2: ###..#..
  0x48, // Row 3: ...#..#.
  0x93, // Row 4: ##..#..#
  0xa4, // Row 5: ..#..#.#
  0xa8, // Row 6: ...#.#.#
  0xa9, // Row 7: #..#.#.#
]);

// System/settings icon (8x8)
export const icon_system = new Uint8Array([
  0x24, // Row 0: ..#..#..
  0x3c, // Row 1: ..####..
  0xc3, // Row 2: ##....##
  0x5a, // Row 3: .#.##.#.
  0x5a, // Row 4: .#.##.#.
  0xc3, // Row 5: ##....##
  0x3c, // Row 6: ..####..
  0x24, // Row 7: ..#..#..
]);

// WiFi icon (8x8)
export const icon_wifi = new Uint8Array([
  0b00000000, 0b00011000, 0b00111100, 0b01111110, 0b11011011, 0b00011000,
  0b00011000, 0b00000000,
]);

// Nodes icon (8x8)
export const icon_nodes = new Uint8Array([
  0xf9, // Row 0: #..#####
  0x00, // Row 1
  0xf9, // Row 2: #..#####
  0x00, // Row 3
  0xf9, // Row 4: #..#####
  0x00, // Row 5
  0xf9, // Row 6: #..#####
  0x00, // Row 7
]);

// Battery vertical (7x11)
export const batteryBitmap_v = new Uint8Array([
  0b00011100, 0b00111110, 0b01000001, 0b01000001, 0b00000000, 0b00000000,
  0b00000000, 0b01000001, 0b01000001, 0b01000001, 0b00111110,
]);

// Battery side gaps (8x3)
export const batteryBitmap_sidegaps_v = new Uint8Array([
  0b10000010, 0b10000010, 0b10000010,
]);

// Lightning bolt vertical (5x5)
export const lightning_bolt_v = new Uint8Array([
  0b00000100, 0b00000110, 0b00011111, 0b00001100, 0b00000100,
]);

// Horizontal battery bottom (13x9)
export const batteryBitmap_h_bottom = new Uint8Array([
  0b00011110, 0b00000000, 0b00000001, 0b00000000, 0b00000001, 0b00000000,
  0b00000001, 0b00000000, 0b00000001, 0b00000000, 0b00000001, 0b00000000,
  0b00000001, 0b00000000, 0b00000001, 0b00000000, 0b00000001, 0b00000000,
  0b00000001, 0b00000000, 0b00000001, 0b00000000, 0b00000001, 0b00000000,
  0b00011110, 0b00000000,
]);

// Horizontal battery top (13x9)
export const batteryBitmap_h_top = new Uint8Array([
  0b00111100, 0b00000000, 0b01000000, 0b00000000, 0b01000000, 0b00000000,
  0b01000000, 0b00000000, 0b01000000, 0b00000000, 0b11000000, 0b00000000,
  0b11000000, 0b00000000, 0b11000000, 0b00000000, 0b01000000, 0b00000000,
  0b01000000, 0b00000000, 0b01000000, 0b00000000, 0b01000000, 0b00000000,
  0b00111100, 0b00000000,
]);

// Lightning bolt horizontal (13x9)
export const lightning_bolt_h = new Uint8Array([
  0b00000000, 0b00000000, 0b00100000, 0b00000000, 0b00110000, 0b00000000,
  0b00111000, 0b00000000, 0b00111100, 0b00000000, 0b00011110, 0b00000000,
  0b11111111, 0b00000000, 0b01111000, 0b00000000, 0b00111100, 0b00000000,
  0b00011100, 0b00000000, 0b00001100, 0b00000000, 0b00000100, 0b00000000,
  0b00000000, 0b00000000,
]);

// Signal bars icon (various signal strengths)
export const signal_bars = {
  width: 12,
  height: 8,
  // 0 bars
  none: new Uint8Array([
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000,
  ]),
  // 1 bar
  low: new Uint8Array([
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000011, 0b00000000, 0b00000011, 0b00000000,
  ]),
  // 2 bars
  medium: new Uint8Array([
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00001100, 0b00000000, 0b00001100, 0b00000000,
    0b00001111, 0b00000000, 0b00001111, 0b00000000,
  ]),
  // 3 bars
  high: new Uint8Array([
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00110000, 0b00000000,
    0b00110000, 0b00000000, 0b00111100, 0b00000000, 0b00111100, 0b00000000,
    0b00111111, 0b00000000, 0b00111111, 0b00000000,
  ]),
  // 4 bars (full)
  full: new Uint8Array([
    0b11000000, 0b00000000, 0b11000000, 0b00000000, 0b11110000, 0b00000000,
    0b11110000, 0b00000000, 0b11111100, 0b00000000, 0b11111100, 0b00000000,
    0b11111111, 0b00000000, 0b11111111, 0b00000000,
  ]),
};

// Meshtastic logo (simplified 32x32)
export const logo_width = 32;
export const logo_height = 32;
export const meshtastic_logo = new Uint8Array([
  0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x3f, 0x00, 0x00, 0x06, 0x60, 0x00, 0x00,
  0x03, 0xc0, 0x00, 0x80, 0x01, 0x80, 0x01, 0xc0, 0x00, 0x00, 0x03, 0x60, 0x00,
  0x00, 0x06, 0x30, 0x00, 0x00, 0x0c, 0x18, 0xf8, 0x1f, 0x18, 0x08, 0x04, 0x20,
  0x10, 0x0c, 0x02, 0x40, 0x30, 0x04, 0x01, 0x80, 0x20, 0x06, 0x01, 0x80, 0x60,
  0x02, 0xe1, 0x87, 0x40, 0x02, 0x11, 0x88, 0x40, 0x03, 0x09, 0x90, 0xc0, 0x03,
  0x09, 0x90, 0xc0, 0x02, 0x11, 0x88, 0x40, 0x02, 0xe1, 0x87, 0x40, 0x06, 0x01,
  0x80, 0x60, 0x04, 0x01, 0x80, 0x20, 0x0c, 0x02, 0x40, 0x30, 0x08, 0x04, 0x20,
  0x10, 0x18, 0xf8, 0x1f, 0x18, 0x30, 0x00, 0x00, 0x0c, 0x60, 0x00, 0x00, 0x06,
  0xc0, 0x00, 0x00, 0x03, 0x80, 0x01, 0x80, 0x01, 0x00, 0x03, 0xc0, 0x00, 0x00,
  0x06, 0x60, 0x00, 0x00, 0xfc, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00,
]);

// Bluetooth icon (8x11)
export const icon_bluetooth = new Uint8Array([
  0b00000000, 0b10000100, 0b01001010, 0b00101010, 0b00010100, 0b11111111,
  0b00010100, 0b00101010, 0b01001010, 0b10000100, 0b00000000,
]);

// GPS/Location pin icon (8x12)
export const icon_gps_pin = new Uint8Array([
  0b00111100, 0b01111110, 0b11111111, 0b11100111, 0b11100111, 0b11111111,
  0b01111110, 0b00111100, 0b00011000, 0b00011000, 0b00001000, 0b00000000,
]);

// Message/chat bubble icon (10x8)
export const icon_message = new Uint8Array([
  0b11111111, 0b00, 0b10000001, 0b00, 0b10000001, 0b00, 0b10000001, 0b00,
  0b10000001, 0b00, 0b11111111, 0b00, 0b00000110, 0b00, 0b00000010, 0b00,
]);

/**
 * Helper to convert icon data to XBM format if needed
 */
export function toXbm(data: Uint8Array | number[]): Uint8Array {
  return data instanceof Uint8Array ? data : new Uint8Array(data);
}
