#ifndef OLEDDISPLAYFONTSGR_h
#define OLEDDISPLAYFONTSGR_h

#ifdef ARDUINO
#include <Arduino.h>
#elif __MBED__
#define PROGMEM
#endif

/**
 * Localization for Greek language containing glyphs for the Greek alphabet.
 * Uses Windows-1253 (CP-1253) encoding for Greek characters.
 *
 * Supported characters:
 * - Uppercase Greek: Α-Ω (U+0391 to U+03A9)
 * - Lowercase Greek: α-ω (U+03B1 to U+03C9)
 * - Accented Greek: ά, έ, ή, ί, ό, ύ, ώ, etc.
 */
extern const uint8_t ArialMT_Plain_10_GR[] PROGMEM;
extern const uint8_t ArialMT_Plain_16_GR[] PROGMEM;
extern const uint8_t ArialMT_Plain_24_GR[] PROGMEM;
#endif
