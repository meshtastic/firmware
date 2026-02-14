/**
 * Meshtastic OLED Emulator
 *
 * A Vue.js component and TypeScript library for emulating SSD1306/SH1106 OLED displays.
 * Provides the same OLEDDisplay API as the Meshtastic firmware, enabling
 * screen development and testing in the browser.
 */

// Core display class
export {
  OLEDDisplay,
  type OLEDDISPLAY_GEOMETRY,
  type OLEDDISPLAY_COLOR,
  type OLEDDISPLAY_TEXT_ALIGNMENT,
  type OLEDFont,
} from "./OLEDDisplay";
export {
  WHITE,
  BLACK,
  INVERSE,
  TEXT_ALIGN_LEFT,
  TEXT_ALIGN_CENTER,
  TEXT_ALIGN_RIGHT,
} from "./OLEDDisplay";

// Fonts
export {
  ArialMT_Plain_10,
  ArialMT_Plain_16,
  FONT_HEIGHT_SMALL,
  FONT_HEIGHT_MEDIUM,
  FONT_HEIGHT_LARGE,
  createFontFromBytes,
} from "./OLEDFonts";

// Simple working fonts (recommended)
export { Font_6x8, Font_5x7, SimpleFont } from "./SimpleFonts";

// Images and icons
export * as OLEDImages from "./OLEDImages";

// Screen renderers (firmware UI ports)
export * as ScreenRenderers from "./ScreenRenderers";

// Vue component
export { default as OLEDEmulator } from "./OLEDEmulator.vue";
export { DISPLAY_PRESETS, type DisplayPreset } from "./DisplayPresets";

// Demo component
export { default as OLEDEmulatorDemo } from "./Demo.vue";
