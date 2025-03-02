#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Custom data types for InkHUD

Only "general purpose" data-types should be defined here.
If your applet has its own structs or enums, which won't be useful to other applets,
please define them inside (or in the same folder as) your applet.

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/Drivers/EInk/EInk.h"

namespace NicheGraphics::InkHUD
{

// Color, understood by display controller IC (as bit values)
// Also suitable for use as AdafruitGFX colors
enum Color : uint8_t {
    BLACK = 0,
    WHITE = 1,
};

// Info contained within AppletFont
struct FontDimensions {
    uint8_t height;
    uint8_t ascenderHeight;
    uint8_t descenderHeight;
};

// Which edge Applet::printAt will place on the X parameter
enum HorizontalAlignment : uint8_t {
    LEFT,
    RIGHT,
    CENTER,
};

// Which edge Applet::printAt will place on the Y parameter
enum VerticalAlignment : uint8_t {
    TOP,
    MIDDLE,
    BOTTOM,
};

// An easy-to-understand intepretation of SNR and RSSI
// Calculate with Applet::getSignalStringth
enum SignalStrength : int8_t {
    SIGNAL_UNKNOWN = -1,
    SIGNAL_NONE,
    SIGNAL_BAD,
    SIGNAL_FAIR,
    SIGNAL_GOOD,
};

} // namespace NicheGraphics::InkHUD

#endif