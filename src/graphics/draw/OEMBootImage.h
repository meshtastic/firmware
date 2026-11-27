#pragma once

#include <stdint.h>

namespace graphics
{

/**
 * Runtime OEM boot screen, loaded from /prefs/oemboot.
 *
 * Lets a vendor brand a stock firmware build the same way USERPREFS_OEM_TEXT / USERPREFS_OEM_IMAGE_DATA do at
 * build time, without shipping a custom binary. The file is written by a client (e.g. over XModem) and is only
 * read once at boot. The Meshtastic logo is always shown first; a build-time OEM screen takes precedence.
 *
 * File layout (little endian, packed):
 *   uint32 magic ("OEMB")  uint8 version (1)  uint16 width  uint16 height  uint8 font_size (0 small, 1 medium, 2 large)
 *   char text[32] (NUL padded)  uint8 xbm[((width + 7) / 8) * height]
 */
namespace OEMBootImage
{

struct Image {
    uint16_t width;
    uint16_t height;
    uint8_t fontSize;
    char text[33];
    uint8_t *xbm; // XBM bit order, rows padded to whole bytes
};

/// Load /prefs/oemboot if present and valid. Returns true if an image is available afterwards.
bool load();

/// The loaded image, or nullptr.
const Image *get();

/// Free the image buffer once the boot screen is done.
void release();

} // namespace OEMBootImage
} // namespace graphics
