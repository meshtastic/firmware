#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./AppletFont.h"

using namespace NicheGraphics;

InkHUD::AppletFont::AppletFont()
{
    // Default constructor uses the in-built AdafruitGFX font
}

InkHUD::AppletFont::AppletFont(const GFXfont &adafruitGFXFont) : gfxFont(&adafruitGFXFont)
{
    // AdafruitGFX fonts are drawn relative to a "cursor line";
    // they print as if the glyphs are resting on the line of piece of ruled paper.
    // The glyphs also each have a different height.

    // To simplify drawing, we will scan the entire font now, and determine an appropriate height for a line of text
    // We also need to know where that "cursor line" sits inside this "line height";
    // we need this additional info in order to align text by top-left, bottom-right, etc

    // AdafruitGFX fonts do declare a line-height, but this seems to include a certain amount of padding,
    // which we'd rather not deal with. If we want padding, we'll add it manually.

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
    }

    // Determine how far characters may hang "below the line"
    descenderHeight = height - ascenderHeight;

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

// Add to the list of substituted glyphs
// This "find and replace" operation will be run before text is printed
// Used to swap out UTF8 special characters, either with a custom font, or with a suitable ASCII approximation
void InkHUD::AppletFont::addSubstitution(const char *from, const char *to)
{
    substitutions.push_back({.from = from, .to = to});
}

// Run all registered substitutions on a string
// Used to swap out UTF8 special chars
void InkHUD::AppletFont::applySubstitutions(std::string *text)
{
    // For each substitution
    for (Substitution s : substitutions) {

        // Find and replace
        // - search for Substitution::from
        // - replace with Substitution::to
        size_t i = text->find(s.from);
        while (i != std::string::npos) {
            text->replace(i, strlen(s.from), s.to);
            i = text->find(s.from, i); // Continue looking from last position
        }
    }
}

// Apply a set of substitutions which remap UTF8 for a Windows-1251 font
// Windows-1251 is an 8-bit character encoding, suitable for several languages which use the Cyrillic script
void InkHUD::AppletFont::addSubstitutionsWin1251()
{
    addSubstitution("Ђ", "\x80");
    addSubstitution("Ѓ", "\x81");
    addSubstitution("ѓ", "\x83");
    addSubstitution("€", "\x88");
    addSubstitution("Љ", "\x8A");
    addSubstitution("Њ", "\x8C");
    addSubstitution("Ќ", "\x8D");
    addSubstitution("Ћ", "\x8E");
    addSubstitution("Џ", "\x8F");

    addSubstitution("ђ", "\x90");
    addSubstitution("љ", "\x9A");
    addSubstitution("њ", "\x9C");
    addSubstitution("ќ", "\x9D");
    addSubstitution("ћ", "\x9E");
    addSubstitution("џ", "\x9F");

    addSubstitution("Ў", "\xA1");
    addSubstitution("ў", "\xA2");
    addSubstitution("Ј", "\xA3");
    addSubstitution("Ґ", "\xA5");
    addSubstitution("Ё", "\xA8");
    addSubstitution("Є", "\xAA");
    addSubstitution("Ї", "\xAF");

    addSubstitution("І", "\xB2");
    addSubstitution("і", "\xB3");
    addSubstitution("ґ", "\xB4");
    addSubstitution("ё", "\xB8");
    addSubstitution("№", "\xB9");
    addSubstitution("є", "\xBA");
    addSubstitution("ј", "\xBC");
    addSubstitution("Ѕ", "\xBD");
    addSubstitution("ѕ", "\xBE");
    addSubstitution("ї", "\xBF");

    addSubstitution("А", "\xC0");
    addSubstitution("Б", "\xC1");
    addSubstitution("В", "\xC2");
    addSubstitution("Г", "\xC3");
    addSubstitution("Д", "\xC4");
    addSubstitution("Е", "\xC5");
    addSubstitution("Ж", "\xC6");
    addSubstitution("З", "\xC7");
    addSubstitution("И", "\xC8");
    addSubstitution("Й", "\xC9");
    addSubstitution("К", "\xCA");
    addSubstitution("Л", "\xCB");
    addSubstitution("М", "\xCC");
    addSubstitution("Н", "\xCD");
    addSubstitution("О", "\xCE");
    addSubstitution("П", "\xCF");

    addSubstitution("Р", "\xD0");
    addSubstitution("С", "\xD1");
    addSubstitution("Т", "\xD2");
    addSubstitution("У", "\xD3");
    addSubstitution("Ф", "\xD4");
    addSubstitution("Х", "\xD5");
    addSubstitution("Ц", "\xD6");
    addSubstitution("Ч", "\xD7");
    addSubstitution("Ш", "\xD8");
    addSubstitution("Щ", "\xD9");
    addSubstitution("Ъ", "\xDA");
    addSubstitution("Ы", "\xDB");
    addSubstitution("Ь", "\xDC");
    addSubstitution("Э", "\xDD");
    addSubstitution("Ю", "\xDE");
    addSubstitution("Я", "\xDF");

    addSubstitution("а", "\xE0");
    addSubstitution("б", "\xE1");
    addSubstitution("в", "\xE2");
    addSubstitution("г", "\xE3");
    addSubstitution("д", "\xE4");
    addSubstitution("е", "\xE5");
    addSubstitution("ж", "\xE6");
    addSubstitution("з", "\xE7");
    addSubstitution("и", "\xE8");
    addSubstitution("й", "\xE9");
    addSubstitution("к", "\xEA");
    addSubstitution("л", "\xEB");
    addSubstitution("м", "\xEC");
    addSubstitution("н", "\xED");
    addSubstitution("о", "\xEE");
    addSubstitution("п", "\xEF");

    addSubstitution("р", "\xF0");
    addSubstitution("с", "\xF1");
    addSubstitution("т", "\xF2");
    addSubstitution("у", "\xF3");
    addSubstitution("ф", "\xF4");
    addSubstitution("х", "\xF5");
    addSubstitution("ц", "\xF6");
    addSubstitution("ч", "\xF7");
    addSubstitution("ш", "\xF8");
    addSubstitution("щ", "\xF9");
    addSubstitution("ъ", "\xFA");
    addSubstitution("ы", "\xFB");
    addSubstitution("ь", "\xFC");
    addSubstitution("э", "\xFD");
    addSubstitution("ю", "\xFE");
    addSubstitution("я", "\xFF");
}

#endif