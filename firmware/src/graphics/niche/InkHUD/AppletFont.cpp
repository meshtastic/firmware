#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./AppletFont.h"

#include <assert.h>

using namespace NicheGraphics;

InkHUD::AppletFont::AppletFont()
{
    // Default constructor uses the in-built AdafruitGFX font (not recommended)
}

InkHUD::AppletFont::AppletFont(const GFXfont &adafruitGFXFont, Encoding encoding, int8_t paddingTop, int8_t paddingBottom)
    : gfxFont(&adafruitGFXFont), encoding(encoding)
{
    // AdafruitGFX fonts are drawn relative to a "cursor line";
    // they print as if the glyphs are resting on the line of piece of ruled paper.
    // The glyphs also each have a different height.

    // To simplify drawing, we will scan the entire font now, and determine an appropriate height for a line of text
    // We also need to know where that "cursor line" sits inside this "line height";
    // we need this additional info in order to align text by top-left, bottom-right, etc

    // AdafruitGFX fonts do declare a line-height, but this seems to include a certain amount of padding,
    // which we'd rather not deal with. If we want padding, we'll add it manually.

    this->ascenderHeight = 0;
    this->descenderHeight = 0;
    this->height = 0;

    // Scan each glyph in the AdafruitGFX font
    for (uint16_t i = 0; i <= (gfxFont->last - gfxFont->first); i++) {
        uint8_t glyphHeight = gfxFont->glyph[i].height; // Height of glyph
        this->height = max(this->height, glyphHeight);  // Store if it's a new max

        // Calculate how far the glyph rises the cursor line
        // Store if new max value
        // Caution: signed and unsigned types
        int8_t glyphAscender = 0 - gfxFont->glyph[i].yOffset;
        if (glyphAscender > 0)
            this->ascenderHeight = max(this->ascenderHeight, (uint8_t)glyphAscender);

        int8_t glyphDescender = gfxFont->glyph[i].height + gfxFont->glyph[i].yOffset;
        if (glyphDescender > 0)
            this->descenderHeight = max(this->descenderHeight, (uint8_t)glyphDescender);
    }

    // Apply any manual padding to grow or shrink the line size
    // Helpful if a font has one or two exceptionally large characters, which would make the lines ridiculously tall
    ascenderHeight += paddingTop;
    descenderHeight += paddingBottom;

    // Find how far the cursor advances when we "print" a space character
    spaceCharWidth = gfxFont->glyph[(uint8_t)' ' - gfxFont->first].xAdvance;
}

/*

             ▲    #####  #         ▲
             │    #      #         │
  lineHeight │    ###    #         │
             │    #      #  #   #  │ heightAboveCursor
             │    #      #  #   #  │
             │    #      #   ####  │
             │ -----------------#----
             │                 #   │ heightBelowCursor
             ▼               ###   ▼
*/

uint8_t InkHUD::AppletFont::lineHeight()
{
    return this->height;
}

// AdafruitGFX fonts print characters so that they nicely on an imaginary line (think: ruled paper).
// This value is the height of the font, above that imaginary line.
// Used to calculate the true height of the font
uint8_t InkHUD::AppletFont::heightAboveCursor()
{
    return this->ascenderHeight;
}

// AdafruitGFX fonts print characters so that they nicely on an imaginary line (think: ruled paper).
// This value is the height of the font, below that imaginary line.
// Used to calculate the true height of the font
uint8_t InkHUD::AppletFont::heightBelowCursor()
{
    return this->descenderHeight;
}

// Width of the space character
// Used with Applet::printWrapped
uint8_t InkHUD::AppletFont::widthBetweenWords()
{
    return this->spaceCharWidth;
}

// Convert a unicode char from set of UTF-8 bytes to UTF-32
// Used by AppletFont::applyEncoding, which remaps unicode chars for extended ASCII fonts, based on their UTF-32 value
uint32_t InkHUD::AppletFont::toUtf32(std::string utf8)
{
    uint32_t utf32 = 0;

    switch (utf8.length()) {
    case 2:
        // 5 bits + 6 bits
        utf32 |= (utf8.at(0) & 0b00011111) << 6;
        utf32 |= (utf8.at(1) & 0b00111111);
        break;

    case 3:
        // 4 bits + 6 bits + 6 bits
        utf32 |= (utf8.at(0) & 0b00001111) << (6 + 6);
        utf32 |= (utf8.at(1) & 0b00111111) << 6;
        utf32 |= (utf8.at(2) & 0b00111111);
        break;

    case 4:
        // 3 bits + 6 bits + 6 bits + 6 bits
        utf32 |= (utf8.at(0) & 0b00000111) << (6 + 6 + 6);
        utf32 |= (utf8.at(1) & 0b00111111) << (6 + 6);
        utf32 |= (utf8.at(2) & 0b00111111) << 6;
        utf32 |= (utf8.at(3) & 0b00111111);
        break;
    default:
        assert(false);
    }

    return utf32;
}

// Process a string, collating UTF-8 bytes, and sending them off for re-encoding to extended ASCII
// Not all InkHUD text is passed through here, only text which could potentially contain non-ASCII chars
std::string InkHUD::AppletFont::decodeUTF8(std::string encoded)
{
    // Final processed output
    std::string decoded;

    // Holds bytes for one UTF-8 char during parsing
    std::string utf8Char;
    uint8_t utf8CharSize = 0;

    for (char &c : encoded) {

        // If first byte
        if (utf8Char.empty()) {
            // If MSB is unset, byte is an ASCII char
            // If MSB is set, byte is part of a UTF-8 char. Counting number of higher-order bits tells how many bytes in char
            if ((c & 0x80)) {
                char c1 = c;
                while (c1 & 0x80) {
                    c1 <<= 1;
                    utf8CharSize++;
                }
            }
        }

        // Append the byte to the UTF-8 char we're building
        utf8Char += c;

        // More bytes left to collect. Iterate.
        if (utf8Char.length() < utf8CharSize)
            continue;

        // Now collected all bytes for this char
        // Remap the value to match the encoding of our 8-bit AppletFont
        decoded += applyEncoding(utf8Char);

        // Reset, ready to build next UTF-8 char from the encoded bytes
        utf8Char.clear();
        utf8CharSize = 0;
    } // For each char

    // All chars processed, return result
    return decoded;
}

// Re-encode a single UTF-8 character to extended ASCII
// Target encoding depends on the font
char InkHUD::AppletFont::applyEncoding(std::string utf8)
{
    // ##################################################### Syntactic Sugar #####################################################
#define REMAP(in, out)                                                                                                           \
    case in:                                                                                                                     \
        return out;
    // ###########################################################################################################################

    // Latin - Central Europe
    // https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP1250.TXT
    if (encoding == WINDOWS_1250) {
        // 1-Byte chars: no remapping
        if (utf8.length() == 1)
            return utf8.at(0);

        // Multi-byte chars:
        switch (toUtf32(utf8)) {
            REMAP(0x20AC, 0x80); // EURO SIGN
            REMAP(0x201A, 0x82); // SINGLE LOW-9 QUOTATION MARK
            REMAP(0x201E, 0x84); // DOUBLE LOW-9 QUOTATION MARK
            REMAP(0x2026, 0x85); // HORIZONTAL ELLIPSIS
            REMAP(0x2020, 0x86); // DAGGER
            REMAP(0x2021, 0x87); // DOUBLE DAGGER
            REMAP(0x2030, 0x89); // PER MILLE SIGN
            REMAP(0x0160, 0x8A); // LATIN CAPITAL LETTER S WITH CARON
            REMAP(0x2039, 0x8B); // SINGLE LEFT-POINTING ANGLE QUOTATION MARK
            REMAP(0x015A, 0x8C); // LATIN CAPITAL LETTER S WITH ACUTE
            REMAP(0x0164, 0x8D); // LATIN CAPITAL LETTER T WITH CARON
            REMAP(0x017D, 0x8E); // LATIN CAPITAL LETTER Z WITH CARON
            REMAP(0x0179, 0x8F); // LATIN CAPITAL LETTER Z WITH ACUTE

            REMAP(0x2018, 0x91); // LEFT SINGLE QUOTATION MARK
            REMAP(0x2019, 0x92); // RIGHT SINGLE QUOTATION MARK
            REMAP(0x201C, 0x93); // LEFT DOUBLE QUOTATION MARK
            REMAP(0x201D, 0x94); // RIGHT DOUBLE QUOTATION MARK
            REMAP(0x2022, 0x95); // BULLET
            REMAP(0x2013, 0x96); // EN DASH
            REMAP(0x2014, 0x97); // EM DASH
            REMAP(0x2122, 0x99); // TRADE MARK SIGN
            REMAP(0x0161, 0x9A); // LATIN SMALL LETTER S WITH CARON
            REMAP(0x203A, 0x9B); // SINGLE RIGHT-POINTING ANGLE QUOTATION MARK
            REMAP(0x015B, 0x9C); // LATIN SMALL LETTER S WITH ACUTE
            REMAP(0x0165, 0x9D); // LATIN SMALL LETTER T WITH CARON
            REMAP(0x017E, 0x9E); // LATIN SMALL LETTER Z WITH CARON
            REMAP(0x017A, 0x9F); // LATIN SMALL LETTER Z WITH ACUTE

            REMAP(0x00A0, 0xA0); // NO-BREAK SPACE
            REMAP(0x02C7, 0xA1); // CARON
            REMAP(0x02D8, 0xA2); // BREVE
            REMAP(0x0141, 0xA3); // LATIN CAPITAL LETTER L WITH STROKE
            REMAP(0x00A4, 0xA4); // CURRENCY SIGN
            REMAP(0x0104, 0xA5); // LATIN CAPITAL LETTER A WITH OGONEK
            REMAP(0x00A6, 0xA6); // BROKEN BAR
            REMAP(0x00A7, 0xA7); // SECTION SIGN
            REMAP(0x00A8, 0xA8); // DIAERESIS
            REMAP(0x00A9, 0xA9); // COPYRIGHT SIGN
            REMAP(0x015E, 0xAA); // LATIN CAPITAL LETTER S WITH CEDILLA
            REMAP(0x00AB, 0xAB); // LEFT-POINTING DOUBLE ANGLE QUOTATION MARK
            REMAP(0x00AC, 0xAC); // NOT SIGN
            REMAP(0x00AD, 0xAD); // SOFT HYPHEN
            REMAP(0x00AE, 0xAE); // REGISTERED SIGN
            REMAP(0x017B, 0xAF); // LATIN CAPITAL LETTER Z WITH DOT ABOVE

            REMAP(0x00B0, 0xB0); // DEGREE SIGN
            REMAP(0x00B1, 0xB1); // PLUS-MINUS SIGN
            REMAP(0x02DB, 0xB2); // OGONEK
            REMAP(0x0142, 0xB3); // LATIN SMALL LETTER L WITH STROKE
            REMAP(0x00B4, 0xB4); // ACUTE ACCENT
            REMAP(0x00B5, 0xB5); // MICRO SIGN
            REMAP(0x00B6, 0xB6); // PILCROW SIGN
            REMAP(0x00B7, 0xB7); // MIDDLE DOT
            REMAP(0x00B8, 0xB8); // CEDILLA
            REMAP(0x0105, 0xB9); // LATIN SMALL LETTER A WITH OGONEK
            REMAP(0x015F, 0xBA); // LATIN SMALL LETTER S WITH CEDILLA
            REMAP(0x00BB, 0xBB); // RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
            REMAP(0x013D, 0xBC); // LATIN CAPITAL LETTER L WITH CARON
            REMAP(0x02DD, 0xBD); // DOUBLE ACUTE ACCENT
            REMAP(0x013E, 0xBE); // LATIN SMALL LETTER L WITH CARON
            REMAP(0x017C, 0xBF); // LATIN SMALL LETTER Z WITH DOT ABOVE

            REMAP(0x0154, 0xC0); // LATIN CAPITAL LETTER R WITH ACUTE
            REMAP(0x00C1, 0xC1); // LATIN CAPITAL LETTER A WITH ACUTE
            REMAP(0x00C2, 0xC2); // LATIN CAPITAL LETTER A WITH CIRCUMFLEX
            REMAP(0x0102, 0xC3); // LATIN CAPITAL LETTER A WITH BREVE
            REMAP(0x00C4, 0xC4); // LATIN CAPITAL LETTER A WITH DIAERESIS
            REMAP(0x0139, 0xC5); // LATIN CAPITAL LETTER L WITH ACUTE
            REMAP(0x0106, 0xC6); // LATIN CAPITAL LETTER C WITH ACUTE
            REMAP(0x00C7, 0xC7); // LATIN CAPITAL LETTER C WITH CEDILLA
            REMAP(0x010C, 0xC8); // LATIN CAPITAL LETTER C WITH CARON
            REMAP(0x00C9, 0xC9); // LATIN CAPITAL LETTER E WITH ACUTE
            REMAP(0x0118, 0xCA); // LATIN CAPITAL LETTER E WITH OGONEK
            REMAP(0x00CB, 0xCB); // LATIN CAPITAL LETTER E WITH DIAERESIS
            REMAP(0x011A, 0xCC); // LATIN CAPITAL LETTER E WITH CARON
            REMAP(0x00CD, 0xCD); // LATIN CAPITAL LETTER I WITH ACUTE
            REMAP(0x00CE, 0xCE); // LATIN CAPITAL LETTER I WITH CIRCUMFLEX
            REMAP(0x010E, 0xCF); // LATIN CAPITAL LETTER D WITH CARON

            REMAP(0x0110, 0xD0); // LATIN CAPITAL LETTER D WITH STROKE
            REMAP(0x0143, 0xD1); // LATIN CAPITAL LETTER N WITH ACUTE
            REMAP(0x0147, 0xD2); // LATIN CAPITAL LETTER N WITH CARON
            REMAP(0x00D3, 0xD3); // LATIN CAPITAL LETTER O WITH ACUTE
            REMAP(0x00D4, 0xD4); // LATIN CAPITAL LETTER O WITH CIRCUMFLEX
            REMAP(0x0150, 0xD5); // LATIN CAPITAL LETTER O WITH DOUBLE ACUTE
            REMAP(0x00D6, 0xD6); // LATIN CAPITAL LETTER O WITH DIAERESIS
            REMAP(0x00D7, 0xD7); // MULTIPLICATION SIGN
            REMAP(0x0158, 0xD8); // LATIN CAPITAL LETTER R WITH CARON
            REMAP(0x016E, 0xD9); // LATIN CAPITAL LETTER U WITH RING ABOVE
            REMAP(0x00DA, 0xDA); // LATIN CAPITAL LETTER U WITH ACUTE
            REMAP(0x0170, 0xDB); // LATIN CAPITAL LETTER U WITH DOUBLE ACUTE
            REMAP(0x00DC, 0xDC); // LATIN CAPITAL LETTER U WITH DIAERESIS
            REMAP(0x00DD, 0xDD); // LATIN CAPITAL LETTER Y WITH ACUTE
            REMAP(0x0162, 0xDE); // LATIN CAPITAL LETTER T WITH CEDILLA
            REMAP(0x00DF, 0xDF); // LATIN SMALL LETTER SHARP S

            REMAP(0x0155, 0xE0); // LATIN SMALL LETTER R WITH ACUTE
            REMAP(0x00E1, 0xE1); // LATIN SMALL LETTER A WITH ACUTE
            REMAP(0x00E2, 0xE2); // LATIN SMALL LETTER A WITH CIRCUMFLEX
            REMAP(0x0103, 0xE3); // LATIN SMALL LETTER A WITH BREVE
            REMAP(0x00E4, 0xE4); // LATIN SMALL LETTER A WITH DIAERESIS
            REMAP(0x013A, 0xE5); // LATIN SMALL LETTER L WITH ACUTE
            REMAP(0x0107, 0xE6); // LATIN SMALL LETTER C WITH ACUTE
            REMAP(0x00E7, 0xE7); // LATIN SMALL LETTER C WITH CEDILLA
            REMAP(0x010D, 0xE8); // LATIN SMALL LETTER C WITH CARON
            REMAP(0x00E9, 0xE9); // LATIN SMALL LETTER E WITH ACUTE
            REMAP(0x0119, 0xEA); // LATIN SMALL LETTER E WITH OGONEK
            REMAP(0x00EB, 0xEB); // LATIN SMALL LETTER E WITH DIAERESIS
            REMAP(0x011B, 0xEC); // LATIN SMALL LETTER E WITH CARON
            REMAP(0x00ED, 0xED); // LATIN SMALL LETTER I WITH ACUTE
            REMAP(0x00EE, 0xEE); // LATIN SMALL LETTER I WITH CIRCUMFLEX
            REMAP(0x010F, 0xEF); // LATIN SMALL LETTER D WITH CARON

            REMAP(0x0111, 0xF0); // LATIN SMALL LETTER D WITH STROKE
            REMAP(0x0144, 0xF1); // LATIN SMALL LETTER N WITH ACUTE
            REMAP(0x0148, 0xF2); // LATIN SMALL LETTER N WITH CARON
            REMAP(0x00F3, 0xF3); // LATIN SMALL LETTER O WITH ACUTE
            REMAP(0x00F4, 0xF4); // LATIN SMALL LETTER O WITH CIRCUMFLEX
            REMAP(0x0151, 0xF5); // LATIN SMALL LETTER O WITH DOUBLE ACUTE
            REMAP(0x00F6, 0xF6); // LATIN SMALL LETTER O WITH DIAERESIS
            REMAP(0x00F7, 0xF7); // DIVISION SIGN
            REMAP(0x0159, 0xF8); // LATIN SMALL LETTER R WITH CARON
            REMAP(0x016F, 0xF9); // LATIN SMALL LETTER U WITH RING ABOVE
            REMAP(0x00FA, 0xFA); // LATIN SMALL LETTER U WITH ACUTE
            REMAP(0x0171, 0xFB); // LATIN SMALL LETTER U WITH DOUBLE ACUTE
            REMAP(0x00FC, 0xFC); // LATIN SMALL LETTER U WITH DIAERESIS
            REMAP(0x00FD, 0xFD); // LATIN SMALL LETTER Y WITH ACUTE
            REMAP(0x0163, 0xFE); // LATIN SMALL LETTER T WITH CEDILLA
            REMAP(0x02D9, 0xFF); // DOT ABOVE
        }
    }

    // Latin - Cyrillic
    // https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP1251.TXT
    else if (encoding == WINDOWS_1251) {
        // 1-Byte chars: no remapping
        if (utf8.length() == 1)
            return utf8.at(0);

        // Multi-byte chars:
        switch (toUtf32(utf8)) {
            REMAP(0x0402, 0x80); // CYRILLIC CAPITAL LETTER DJE
            REMAP(0x0403, 0x81); // CYRILLIC CAPITAL LETTER GJE
            REMAP(0x201A, 0x82); // SINGLE LOW-9 QUOTATION MARK
            REMAP(0x0453, 0x83); // CYRILLIC SMALL LETTER GJE
            REMAP(0x201E, 0x84); // DOUBLE LOW-9 QUOTATION MARK
            REMAP(0x2026, 0x85); // HORIZONTAL ELLIPSIS
            REMAP(0x2020, 0x86); // DAGGER
            REMAP(0x2021, 0x87); // DOUBLE DAGGER
            REMAP(0x20AC, 0x88); // EURO SIGN
            REMAP(0x2030, 0x89); // PER MILLE SIGN
            REMAP(0x0409, 0x8A); // CYRILLIC CAPITAL LETTER LJE
            REMAP(0x2039, 0x8B); // SINGLE LEFT-POINTING ANGLE QUOTATION MARK
            REMAP(0x040A, 0x8C); // CYRILLIC CAPITAL LETTER NJE
            REMAP(0x040C, 0x8D); // CYRILLIC CAPITAL LETTER KJE
            REMAP(0x040B, 0x8E); // CYRILLIC CAPITAL LETTER TSHE
            REMAP(0x040F, 0x8F); // CYRILLIC CAPITAL LETTER DZHE

            REMAP(0x0452, 0x90); // CYRILLIC SMALL LETTER DJE
            REMAP(0x2018, 0x91); // LEFT SINGLE QUOTATION MARK
            REMAP(0x2019, 0x92); // RIGHT SINGLE QUOTATION MARK
            REMAP(0x201C, 0x93); // LEFT DOUBLE QUOTATION MARK
            REMAP(0x201D, 0x94); // RIGHT DOUBLE QUOTATION MARK
            REMAP(0x2022, 0x95); // BULLET
            REMAP(0x2013, 0x96); // EN DASH
            REMAP(0x2014, 0x97); // EM DASH
            REMAP(0x2122, 0x99); // TRADE MARK SIGN
            REMAP(0x0459, 0x9A); // CYRILLIC SMALL LETTER LJE
            REMAP(0x203A, 0x9B); // SINGLE RIGHT-POINTING ANGLE QUOTATION MARK
            REMAP(0x045A, 0x9C); // CYRILLIC SMALL LETTER NJE
            REMAP(0x045C, 0x9D); // CYRILLIC SMALL LETTER KJE
            REMAP(0x045B, 0x9E); // CYRILLIC SMALL LETTER TSHE
            REMAP(0x045F, 0x9F); // CYRILLIC SMALL LETTER DZHE

            REMAP(0x00A0, 0xA0); // NO-BREAK SPACE
            REMAP(0x040E, 0xA1); // CYRILLIC CAPITAL LETTER SHORT U
            REMAP(0x045E, 0xA2); // CYRILLIC SMALL LETTER SHORT U
            REMAP(0x0408, 0xA3); // CYRILLIC CAPITAL LETTER JE
            REMAP(0x00A4, 0xA4); // CURRENCY SIGN
            REMAP(0x0490, 0xA5); // CYRILLIC CAPITAL LETTER GHE WITH UPTURN
            REMAP(0x00A6, 0xA6); // BROKEN BAR
            REMAP(0x00A7, 0xA7); // SECTION SIGN
            REMAP(0x0401, 0xA8); // CYRILLIC CAPITAL LETTER IO
            REMAP(0x00A9, 0xA9); // COPYRIGHT SIGN
            REMAP(0x0404, 0xAA); // CYRILLIC CAPITAL LETTER UKRAINIAN IE
            REMAP(0x00AB, 0xAB); // LEFT-POINTING DOUBLE ANGLE QUOTATION MARK
            REMAP(0x00AC, 0xAC); // NOT SIGN
            REMAP(0x00AD, 0xAD); // SOFT HYPHEN
            REMAP(0x00AE, 0xAE); // REGISTERED SIGN
            REMAP(0x0407, 0xAF); // CYRILLIC CAPITAL LETTER YI

            REMAP(0x00B0, 0xB0); // DEGREE SIGN
            REMAP(0x00B1, 0xB1); // PLUS-MINUS SIGN
            REMAP(0x0406, 0xB2); // CYRILLIC CAPITAL LETTER BYELORUSSIAN-UKRAINIAN I
            REMAP(0x0456, 0xB3); // CYRILLIC SMALL LETTER BYELORUSSIAN-UKRAINIAN I
            REMAP(0x0491, 0xB4); // CYRILLIC SMALL LETTER GHE WITH UPTURN
            REMAP(0x00B5, 0xB5); // MICRO SIGN
            REMAP(0x00B6, 0xB6); // PILCROW SIGN
            REMAP(0x00B7, 0xB7); // MIDDLE DOT
            REMAP(0x0451, 0xB8); // CYRILLIC SMALL LETTER IO
            REMAP(0x2116, 0xB9); // NUMERO SIGN
            REMAP(0x0454, 0xBA); // CYRILLIC SMALL LETTER UKRAINIAN IE
            REMAP(0x00BB, 0xBB); // RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
            REMAP(0x0458, 0xBC); // CYRILLIC SMALL LETTER JE
            REMAP(0x0405, 0xBD); // CYRILLIC CAPITAL LETTER DZE
            REMAP(0x0455, 0xBE); // CYRILLIC SMALL LETTER DZE
            REMAP(0x0457, 0xBF); // CYRILLIC SMALL LETTER YI

            REMAP(0x0410, 0xC0); // CYRILLIC CAPITAL LETTER A
            REMAP(0x0411, 0xC1); // CYRILLIC CAPITAL LETTER BE
            REMAP(0x0412, 0xC2); // CYRILLIC CAPITAL LETTER VE
            REMAP(0x0413, 0xC3); // CYRILLIC CAPITAL LETTER GHE
            REMAP(0x0414, 0xC4); // CYRILLIC CAPITAL LETTER DE
            REMAP(0x0415, 0xC5); // CYRILLIC CAPITAL LETTER IE
            REMAP(0x0416, 0xC6); // CYRILLIC CAPITAL LETTER ZHE
            REMAP(0x0417, 0xC7); // CYRILLIC CAPITAL LETTER ZE
            REMAP(0x0418, 0xC8); // CYRILLIC CAPITAL LETTER I
            REMAP(0x0419, 0xC9); // CYRILLIC CAPITAL LETTER SHORT I
            REMAP(0x041A, 0xCA); // CYRILLIC CAPITAL LETTER KA
            REMAP(0x041B, 0xCB); // CYRILLIC CAPITAL LETTER EL
            REMAP(0x041C, 0xCC); // CYRILLIC CAPITAL LETTER EM
            REMAP(0x041D, 0xCD); // CYRILLIC CAPITAL LETTER EN
            REMAP(0x041E, 0xCE); // CYRILLIC CAPITAL LETTER O
            REMAP(0x041F, 0xCF); // CYRILLIC CAPITAL LETTER PE

            REMAP(0x0420, 0xD0); // CYRILLIC CAPITAL LETTER ER
            REMAP(0x0421, 0xD1); // CYRILLIC CAPITAL LETTER ES
            REMAP(0x0422, 0xD2); // CYRILLIC CAPITAL LETTER TE
            REMAP(0x0423, 0xD3); // CYRILLIC CAPITAL LETTER U
            REMAP(0x0424, 0xD4); // CYRILLIC CAPITAL LETTER EF
            REMAP(0x0425, 0xD5); // CYRILLIC CAPITAL LETTER HA
            REMAP(0x0426, 0xD6); // CYRILLIC CAPITAL LETTER TSE
            REMAP(0x0427, 0xD7); // CYRILLIC CAPITAL LETTER CHE
            REMAP(0x0428, 0xD8); // CYRILLIC CAPITAL LETTER SHA
            REMAP(0x0429, 0xD9); // CYRILLIC CAPITAL LETTER SHCHA
            REMAP(0x042A, 0xDA); // CYRILLIC CAPITAL LETTER HARD SIGN
            REMAP(0x042B, 0xDB); // CYRILLIC CAPITAL LETTER YERU
            REMAP(0x042C, 0xDC); // CYRILLIC CAPITAL LETTER SOFT SIGN
            REMAP(0x042D, 0xDD); // CYRILLIC CAPITAL LETTER E
            REMAP(0x042E, 0xDE); // CYRILLIC CAPITAL LETTER YU
            REMAP(0x042F, 0xDF); // CYRILLIC CAPITAL LETTER YA

            REMAP(0x0430, 0xE0); // CYRILLIC SMALL LETTER A
            REMAP(0x0431, 0xE1); // CYRILLIC SMALL LETTER BE
            REMAP(0x0432, 0xE2); // CYRILLIC SMALL LETTER VE
            REMAP(0x0433, 0xE3); // CYRILLIC SMALL LETTER GHE
            REMAP(0x0434, 0xE4); // CYRILLIC SMALL LETTER DE
            REMAP(0x0435, 0xE5); // CYRILLIC SMALL LETTER IE
            REMAP(0x0436, 0xE6); // CYRILLIC SMALL LETTER ZHE
            REMAP(0x0437, 0xE7); // CYRILLIC SMALL LETTER ZE
            REMAP(0x0438, 0xE8); // CYRILLIC SMALL LETTER I
            REMAP(0x0439, 0xE9); // CYRILLIC SMALL LETTER SHORT I
            REMAP(0x043A, 0xEA); // CYRILLIC SMALL LETTER KA
            REMAP(0x043B, 0xEB); // CYRILLIC SMALL LETTER EL
            REMAP(0x043C, 0xEC); // CYRILLIC SMALL LETTER EM
            REMAP(0x043D, 0xED); // CYRILLIC SMALL LETTER EN
            REMAP(0x043E, 0xEE); // CYRILLIC SMALL LETTER O
            REMAP(0x043F, 0xEF); // CYRILLIC SMALL LETTER PE

            REMAP(0x0440, 0xF0); // CYRILLIC SMALL LETTER ER
            REMAP(0x0441, 0xF1); // CYRILLIC SMALL LETTER ES
            REMAP(0x0442, 0xF2); // CYRILLIC SMALL LETTER TE
            REMAP(0x0443, 0xF3); // CYRILLIC SMALL LETTER U
            REMAP(0x0444, 0xF4); // CYRILLIC SMALL LETTER EF
            REMAP(0x0445, 0xF5); // CYRILLIC SMALL LETTER HA
            REMAP(0x0446, 0xF6); // CYRILLIC SMALL LETTER TSE
            REMAP(0x0447, 0xF7); // CYRILLIC SMALL LETTER CHE
            REMAP(0x0448, 0xF8); // CYRILLIC SMALL LETTER SHA
            REMAP(0x0449, 0xF9); // CYRILLIC SMALL LETTER SHCHA
            REMAP(0x044A, 0xFA); // CYRILLIC SMALL LETTER HARD SIGN
            REMAP(0x044B, 0xFB); // CYRILLIC SMALL LETTER YERU
            REMAP(0x044C, 0xFC); // CYRILLIC SMALL LETTER SOFT SIGN
            REMAP(0x044D, 0xFD); // CYRILLIC SMALL LETTER E
            REMAP(0x044E, 0xFE); // CYRILLIC SMALL LETTER YU
            REMAP(0x044F, 0xFF); // CYRILLIC SMALL LETTER YA
        }
    }

    // Latin - Western Europe
    // https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP1252.TXT
    else if (encoding == WINDOWS_1252) {
        // 1-Byte chars: no remapping
        if (utf8.length() == 1)
            return utf8.at(0);

        // Multi-byte chars:
        switch (toUtf32(utf8)) {
            REMAP(0x20AC, 0x80) // EURO SIGN
            REMAP(0x201A, 0x82) // SINGLE LOW-9 QUOTATION MARK
            REMAP(0x0192, 0x83) // LATIN SMALL LETTER F WITH HOOK
            REMAP(0x201E, 0x84) // DOUBLE LOW-9 QUOTATION MARK
            REMAP(0x2026, 0x85) // HORIZONTAL ELLIPSIS
            REMAP(0x2020, 0x86) // DAGGER
            REMAP(0x2021, 0x87) // DOUBLE DAGGER
            REMAP(0x02C6, 0x88) // MODIFIER LETTER CIRCUMFLEX ACCENT
            REMAP(0x2030, 0x89) // PER MILLE SIGN
            REMAP(0x0160, 0x8A) // LATIN CAPITAL LETTER S WITH CARON
            REMAP(0x2039, 0x8B) // SINGLE LEFT-POINTING ANGLE QUOTATION MARK
            REMAP(0x0152, 0x8C) // LATIN CAPITAL LIGATURE OE
            REMAP(0x017D, 0x8E) // LATIN CAPITAL LETTER Z WITH CARON

            REMAP(0x2018, 0x91) // LEFT SINGLE QUOTATION MARK
            REMAP(0x2019, 0x92) // RIGHT SINGLE QUOTATION MARK
            REMAP(0x201C, 0x93) // LEFT DOUBLE QUOTATION MARK
            REMAP(0x201D, 0x94) // RIGHT DOUBLE QUOTATION MARK
            REMAP(0x2022, 0x95) // BULLET
            REMAP(0x2013, 0x96) // EN DASH
            REMAP(0x2014, 0x97) // EM DASH
            REMAP(0x02DC, 0x98) // SMALL TILDE
            REMAP(0x2122, 0x99) // TRADE MARK SIGN
            REMAP(0x0161, 0x9A) // LATIN SMALL LETTER S WITH CARON
            REMAP(0x203A, 0x9B) // SINGLE RIGHT-POINTING ANGLE QUOTATION MARK
            REMAP(0x0153, 0x9C) // LATIN SMALL LIGATURE OE
            REMAP(0x017E, 0x9E) // LATIN SMALL LETTER Z WITH CARON
            REMAP(0x0178, 0x9F) // LATIN CAPITAL LETTER Y WITH DIAERESIS

            REMAP(0x00A0, 0xA0) // NO-BREAK SPACE
            REMAP(0x00A1, 0xA1) // INVERTED EXCLAMATION MARK
            REMAP(0x00A2, 0xA2) // CENT SIGN
            REMAP(0x00A3, 0xA3) // POUND SIGN
            REMAP(0x00A4, 0xA4) // CURRENCY SIGN
            REMAP(0x00A5, 0xA5) // YEN SIGN
            REMAP(0x00A6, 0xA6) // BROKEN BAR
            REMAP(0x00A7, 0xA7) // SECTION SIGN
            REMAP(0x00A8, 0xA8) // DIAERESIS
            REMAP(0x00A9, 0xA9) // COPYRIGHT SIGN
            REMAP(0x00AA, 0xAA) // FEMININE ORDINAL INDICATOR
            REMAP(0x00AB, 0xAB) // LEFT-POINTING DOUBLE ANGLE QUOTATION MARK
            REMAP(0x00AC, 0xAC) // NOT SIGN
            REMAP(0x00AD, 0xAD) // SOFT HYPHEN
            REMAP(0x00AE, 0xAE) // REGISTERED SIGN
            REMAP(0x00AF, 0xAF) // MACRON

            REMAP(0x00B0, 0xB0) // DEGREE SIGN
            REMAP(0x00B1, 0xB1) // PLUS-MINUS SIGN
            REMAP(0x00B2, 0xB2) // SUPERSCRIPT TWO
            REMAP(0x00B3, 0xB3) // SUPERSCRIPT THREE
            REMAP(0x00B4, 0xB4) // ACUTE ACCENT
            REMAP(0x00B5, 0xB5) // MICRO SIGN
            REMAP(0x00B6, 0xB6) // PILCROW SIGN
            REMAP(0x00B7, 0xB7) // MIDDLE DOT
            REMAP(0x00B8, 0xB8) // CEDILLA
            REMAP(0x00B9, 0xB9) // SUPERSCRIPT ONE
            REMAP(0x00BA, 0xBA) // MASCULINE ORDINAL INDICATOR
            REMAP(0x00BB, 0xBB) // RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
            REMAP(0x00BC, 0xBC) // VULGAR FRACTION ONE QUARTER
            REMAP(0x00BD, 0xBD) // VULGAR FRACTION ONE HALF
            REMAP(0x00BE, 0xBE) // VULGAR FRACTION THREE QUARTERS
            REMAP(0x00BF, 0xBF) // INVERTED QUESTION MARK

            REMAP(0x00C0, 0xC0) // LATIN CAPITAL LETTER A WITH GRAVE
            REMAP(0x00C1, 0xC1) // LATIN CAPITAL LETTER A WITH ACUTE
            REMAP(0x00C2, 0xC2) // LATIN CAPITAL LETTER A WITH CIRCUMFLEX
            REMAP(0x00C3, 0xC3) // LATIN CAPITAL LETTER A WITH TILDE
            REMAP(0x00C4, 0xC4) // LATIN CAPITAL LETTER A WITH DIAERESIS
            REMAP(0x00C5, 0xC5) // LATIN CAPITAL LETTER A WITH RING ABOVE
            REMAP(0x00C6, 0xC6) // LATIN CAPITAL LETTER AE
            REMAP(0x00C7, 0xC7) // LATIN CAPITAL LETTER C WITH CEDILLA
            REMAP(0x00C8, 0xC8) // LATIN CAPITAL LETTER E WITH GRAVE
            REMAP(0x00C9, 0xC9) // LATIN CAPITAL LETTER E WITH ACUTE
            REMAP(0x00CA, 0xCA) // LATIN CAPITAL LETTER E WITH CIRCUMFLEX
            REMAP(0x00CB, 0xCB) // LATIN CAPITAL LETTER E WITH DIAERESIS
            REMAP(0x00CC, 0xCC) // LATIN CAPITAL LETTER I WITH GRAVE
            REMAP(0x00CD, 0xCD) // LATIN CAPITAL LETTER I WITH ACUTE
            REMAP(0x00CE, 0xCE) // LATIN CAPITAL LETTER I WITH CIRCUMFLEX
            REMAP(0x00CF, 0xCF) // LATIN CAPITAL LETTER I WITH DIAERESIS

            REMAP(0x00D0, 0xD0) // LATIN CAPITAL LETTER ETH
            REMAP(0x00D1, 0xD1) // LATIN CAPITAL LETTER N WITH TILDE
            REMAP(0x00D2, 0xD2) // LATIN CAPITAL LETTER O WITH GRAVE
            REMAP(0x00D3, 0xD3) // LATIN CAPITAL LETTER O WITH ACUTE
            REMAP(0x00D4, 0xD4) // LATIN CAPITAL LETTER O WITH CIRCUMFLEX
            REMAP(0x00D5, 0xD5) // LATIN CAPITAL LETTER O WITH TILDE
            REMAP(0x00D6, 0xD6) // LATIN CAPITAL LETTER O WITH DIAERESIS
            REMAP(0x00D7, 0xD7) // MULTIPLICATION SIGN
            REMAP(0x00D8, 0xD8) // LATIN CAPITAL LETTER O WITH STROKE
            REMAP(0x00D9, 0xD9) // LATIN CAPITAL LETTER U WITH GRAVE
            REMAP(0x00DA, 0xDA) // LATIN CAPITAL LETTER U WITH ACUTE
            REMAP(0x00DB, 0xDB) // LATIN CAPITAL LETTER U WITH CIRCUMFLEX
            REMAP(0x00DC, 0xDC) // LATIN CAPITAL LETTER U WITH DIAERESIS
            REMAP(0x00DD, 0xDD) // LATIN CAPITAL LETTER Y WITH ACUTE
            REMAP(0x00DE, 0xDE) // LATIN CAPITAL LETTER THORN
            REMAP(0x00DF, 0xDF) // LATIN SMALL LETTER SHARP S

            REMAP(0x00E0, 0xE0) // LATIN SMALL LETTER A WITH GRAVE
            REMAP(0x00E1, 0xE1) // LATIN SMALL LETTER A WITH ACUTE
            REMAP(0x00E2, 0xE2) // LATIN SMALL LETTER A WITH CIRCUMFLEX
            REMAP(0x00E3, 0xE3) // LATIN SMALL LETTER A WITH TILDE
            REMAP(0x00E4, 0xE4) // LATIN SMALL LETTER A WITH DIAERESIS
            REMAP(0x00E5, 0xE5) // LATIN SMALL LETTER A WITH RING ABOVE
            REMAP(0x00E6, 0xE6) // LATIN SMALL LETTER AE
            REMAP(0x00E7, 0xE7) // LATIN SMALL LETTER C WITH CEDILLA
            REMAP(0x00E8, 0xE8) // LATIN SMALL LETTER E WITH GRAVE
            REMAP(0x00E9, 0xE9) // LATIN SMALL LETTER E WITH ACUTE
            REMAP(0x00EA, 0xEA) // LATIN SMALL LETTER E WITH CIRCUMFLEX
            REMAP(0x00EB, 0xEB) // LATIN SMALL LETTER E WITH DIAERESIS
            REMAP(0x00EC, 0xEC) // LATIN SMALL LETTER I WITH GRAVE
            REMAP(0x00ED, 0xED) // LATIN SMALL LETTER I WITH ACUTE
            REMAP(0x00EE, 0xEE) // LATIN SMALL LETTER I WITH CIRCUMFLEX
            REMAP(0x00EF, 0xEF) // LATIN SMALL LETTER I WITH DIAERESIS

            REMAP(0x00F0, 0xF0) // LATIN SMALL LETTER ETH
            REMAP(0x00F1, 0xF1) // LATIN SMALL LETTER N WITH TILDE
            REMAP(0x00F2, 0xF2) // LATIN SMALL LETTER O WITH GRAVE
            REMAP(0x00F3, 0xF3) // LATIN SMALL LETTER O WITH ACUTE
            REMAP(0x00F4, 0xF4) // LATIN SMALL LETTER O WITH CIRCUMFLEX
            REMAP(0x00F5, 0xF5) // LATIN SMALL LETTER O WITH TILDE
            REMAP(0x00F6, 0xF6) // LATIN SMALL LETTER O WITH DIAERESIS
            REMAP(0x00F7, 0xF7) // DIVISION SIGN
            REMAP(0x00F8, 0xF8) // LATIN SMALL LETTER O WITH STROKE
            REMAP(0x00F9, 0xF9) // LATIN SMALL LETTER U WITH GRAVE
            REMAP(0x00FA, 0xFA) // LATIN SMALL LETTER U WITH ACUTE
            REMAP(0x00FB, 0xFB) // LATIN SMALL LETTER U WITH CIRCUMFLEX
            REMAP(0x00FC, 0xFC) // LATIN SMALL LETTER U WITH DIAERESIS
            REMAP(0x00FD, 0xFD) // LATIN SMALL LETTER Y WITH ACUTE
            REMAP(0x00FE, 0xFE) // LATIN SMALL LETTER THORN
            REMAP(0x00FF, 0xFF) // LATIN SMALL LETTER Y WITH DIAERESIS
        }
    }

    else /*ASCII or Unhandled*/ {
        if (utf8.length() == 1)
            return utf8.at(0);
    }

    // All single-byte (ASCII) characters should have been handled by now
    // Only unhandled multi-byte UTF8 characters should remain
    assert(utf8.length() > 1);

    // Parse emoji
    // Strip emoji modifiers
    switch (toUtf32(utf8)) {
        REMAP(0x1F44D, 0x01) // 👍 Thumbs Up
        REMAP(0x1F44E, 0x02) // 👎 Thumbs Down

        REMAP(0x1F60A, 0x03) // 😊 Smiling Face with Smiling Eyes
        REMAP(0x1F642, 0x03) // 🙂 Slightly Smiling Face
        REMAP(0x1F601, 0x03) // 😁 Grinning Face with Smiling Eye

        REMAP(0x1F602, 0x04) // 😂 Face with Tears of Joy
        REMAP(0x1F923, 0x04) // 🤣 Rolling on the Floor Laughing
        REMAP(0x1F606, 0x04) // 😆 Smiling with Open Mouth and Closed Eyes

        REMAP(0x1F44B, 0x05) // 👋 Waving Hand

        REMAP(0x02600, 0x06) // ☀ Sun
        REMAP(0x1F31E, 0x06) // 🌞 Sun with Face

        // 0x07 - Bell character (unused)
        REMAP(0x1F327, 0x08) // 🌧️ Cloud with Rain

        REMAP(0x02601, 0x09) // ☁️ Cloud
        REMAP(0x1F32B, 0x09) // Fog

        REMAP(0x1F9E1, 0x0B) // 🧡 Orange Heart
        REMAP(0x02763, 0x0B) // ❣ Heart Exclamation
        REMAP(0x02764, 0x0B) // ❤ Heart
        REMAP(0x1F495, 0x0B) // 💕 Two Hearts
        REMAP(0x1F496, 0x0B) // 💖 Sparkling Heart
        REMAP(0x1F497, 0x0B) // 💗 Growing Heart
        REMAP(0x1F498, 0x0B) // 💘 Heart with Arrow

        REMAP(0x1F4A9, 0x0C) // 💩 Pile of Poo
        // 0x0D - Carriage return (unused)
        REMAP(0x1F514, 0x0E) // 🔔 Bell

        REMAP(0x1F62D, 0x0F) // 😭 Loudly Crying Face
        REMAP(0x1F622, 0x0F) // 😢 Crying Face

        REMAP(0x1F64F, 0x10) // 🙏 Person with Folded Hands
        REMAP(0x1F618, 0x11) // 😘 Face Throwing a Kiss
        REMAP(0x1F389, 0x12) // 🎉 Party Popper

        REMAP(0x1F600, 0x13) // 😀 Grinning Face
        REMAP(0x1F603, 0x13) // 😃 Smiling Face with Open Mouth
        REMAP(0x1F604, 0x13) // 😄 Smiling Face with Open Mouth and Smiling Eyes

        REMAP(0x1F97A, 0x14) // 🥺 Face with Pleading Eyes
        REMAP(0x1F605, 0x15) // 😅 Smiling with Sweat
        REMAP(0x1F525, 0x16) // 🔥 Fire
        REMAP(0x1F926, 0x17) // 🤦 Face Palm
        REMAP(0x1F937, 0x18) // 🤷 Shrug
        REMAP(0x1F644, 0x19) // 🙄 Face with Rolling Eyes
        // 0x1A Substitution (unused)
        REMAP(0x1F917, 0x1B) // 🤗 Hugging Face

        REMAP(0x1F609, 0x1C) // 😉 Winking Face
        REMAP(0x1F61C, 0x1C) // 😜 Face with Stuck-Out Tongue and Winking Eye
        REMAP(0x1F60F, 0x1C) // 😏 Smirking Face

        REMAP(0x1F914, 0x1D) // 🤔 Thinking Face
        REMAP(0x1FAE1, 0x1E) // 🫡 Saluting Face
        REMAP(0x1F44C, 0x1F) // 👌 OK Hand Sign

        REMAP(0x02755, '!') // ❕
        REMAP(0x02757, '!') // ❗
        REMAP(0x0203C, '!') // ‼
        REMAP(0x02753, '?') // ❓
        REMAP(0x02754, '?') // ❔
        REMAP(0x02049, '?') // ⁉

        // Modifiers (deleted)
        REMAP(0x02640, 0x7F) // Gender
        REMAP(0x02642, 0x7F)
        REMAP(0x1F3FB, 0x7F) // Skin Tones
        REMAP(0x1F3FC, 0x7F)
        REMAP(0x1F3FD, 0x7F)
        REMAP(0x1F3FE, 0x7F)
        REMAP(0x1F3FF, 0x7F)
        REMAP(0x0FE00, 0x7F) // Variation Selectors
        REMAP(0x0FE01, 0x7F)
        REMAP(0x0FE02, 0x7F)
        REMAP(0x0FE03, 0x7F)
        REMAP(0x0FE04, 0x7F)
        REMAP(0x0FE05, 0x7F)
        REMAP(0x0FE06, 0x7F)
        REMAP(0x0FE07, 0x7F)
        REMAP(0x0FE08, 0x7F)
        REMAP(0x0FE09, 0x7F)
        REMAP(0x0FE0A, 0x7F)
        REMAP(0x0FE0B, 0x7F)
        REMAP(0x0FE0C, 0x7F)
        REMAP(0x0FE0D, 0x7F)
        REMAP(0x0FE0E, 0x7F)
        REMAP(0x0FE0F, 0x7F)
        REMAP(0x0200D, 0x7F) // Zero Width Joiner
    }

    // If not handled, return SUB
    return '\x1A';

// Sweep up the syntactic sugar
// Don't want ants in the house
#undef REMAP
}

#endif