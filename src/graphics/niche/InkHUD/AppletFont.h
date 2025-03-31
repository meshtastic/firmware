#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

    Wrapper class for an AdafruitGFX font
    Pre-calculates some font dimension info which InkHUD uses repeatedly

    Also contains an optional set of "substitutions".
    These can be used to detect special UTF8 chars, and replace occurrences with a remapped char val to suit a custom font
    These can also be used to swap UTF8 chars for a suitable ASCII substitution (e.g. German ö -> oe, etc)

*/

#pragma once

#include "configuration.h"

#include <GFX.h> // GFXRoot drawing lib

namespace NicheGraphics::InkHUD
{

// An AdafruitGFX font, bundled with precalculated dimensions which are used frequently by InkHUD
class AppletFont
{
  public:
    AppletFont();
    explicit AppletFont(const GFXfont &adafruitGFXFont);

    uint8_t lineHeight();
    uint8_t heightAboveCursor();
    uint8_t heightBelowCursor();
    uint8_t widthBetweenWords(); // Width of the space character

    void applySubstitutions(std::string *text);             // Run all char-substitution operations, prior to printing
    void addSubstitution(const char *from, const char *to); // Register a find-replace action, for remapping UTF8 chars
    void addSubstitutionsWin1251();                         // Cyrillic fonts: remap UTF8 values to their Win-1251 equivalent
    // Todo: Polish font

    const GFXfont *gfxFont = NULL; // Default value: in-built AdafruitGFX font

  private:
    uint8_t height = 8;          // Default value: in-built AdafruitGFX font
    uint8_t ascenderHeight = 0;  // Default value: in-built AdafruitGFX font
    uint8_t descenderHeight = 8; // Default value: in-built AdafruitGFX font
    uint8_t spaceCharWidth = 8;  // Default value: in-built AdafruitGFX font

    // One pair of find-replace values, for substituting or remapping UTF8 chars
    struct Substitution {
        const char *from;
        const char *to;
    };

    std::vector<Substitution> substitutions; // List of all character substitutions to run, prior to printing a string
};

} // namespace NicheGraphics::InkHUD

#endif