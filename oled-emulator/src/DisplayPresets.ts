/**
 * DisplayPresets.ts - OLED display presets for emulator
 */

import type { OLEDDISPLAY_GEOMETRY } from "./OLEDDisplay";

// Display type presets with realistic OLED colors
export interface DisplayPreset {
  name: string;
  geometry: OLEDDISPLAY_GEOMETRY;
  pixelOn: { r: number; g: number; b: number };
  pixelOff: { r: number; g: number; b: number };
  glow: number;
  contrast: number;
}

export const DISPLAY_PRESETS: Record<string, DisplayPreset> = {
  // Classic blue OLED (SSD1306)
  "ssd1306-blue": {
    name: "SSD1306 Blue",
    geometry: "128x64",
    pixelOn: { r: 100, g: 180, b: 255 },
    pixelOff: { r: 2, g: 3, b: 8 },
    glow: 0.4,
    contrast: 1.0,
  },
  // White OLED (SSD1306)
  "ssd1306-white": {
    name: "SSD1306 White",
    geometry: "128x64",
    pixelOn: { r: 255, g: 255, b: 245 },
    pixelOff: { r: 5, g: 5, b: 5 },
    glow: 0.3,
    contrast: 1.0,
  },
  // Yellow-blue dual color OLED
  "ssd1306-yellow-blue": {
    name: "SSD1306 Yellow/Blue",
    geometry: "128x64",
    // Yellow section is top 16 rows, blue is rest
    pixelOn: { r: 255, g: 220, b: 50 }, // Will be overridden per-pixel
    pixelOff: { r: 3, g: 3, b: 5 },
    glow: 0.35,
    contrast: 1.0,
  },
  // SH1106 (slightly different characteristics)
  sh1106: {
    name: "SH1106",
    geometry: "128x64",
    pixelOn: { r: 120, g: 200, b: 255 },
    pixelOff: { r: 2, g: 2, b: 6 },
    glow: 0.45,
    contrast: 0.95,
  },
  // SH1107 128x128
  sh1107: {
    name: "SH1107 128x128",
    geometry: "128x128",
    pixelOn: { r: 255, g: 255, b: 240 },
    pixelOff: { r: 4, g: 4, b: 6 },
    glow: 0.3,
    contrast: 1.0,
  },
  // Small 64x48 OLED
  "ssd1306-64x48": {
    name: "SSD1306 64x48",
    geometry: "64x48",
    pixelOn: { r: 100, g: 180, b: 255 },
    pixelOff: { r: 2, g: 3, b: 8 },
    glow: 0.4,
    contrast: 1.0,
  },
  // Mini 128x32
  "ssd1306-128x32": {
    name: "SSD1306 128x32",
    geometry: "128x32",
    pixelOn: { r: 100, g: 180, b: 255 },
    pixelOff: { r: 2, g: 3, b: 8 },
    glow: 0.4,
    contrast: 1.0,
  },
};
