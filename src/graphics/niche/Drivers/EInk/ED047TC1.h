/*

    E-Ink display driver adapter
        - ED047TC1 (via FastEPD library)
        - Manufacturer: E Ink / used in LilyGo T5-E-Paper-S3-Pro
        - Size: 4.7 inch
        - Physical resolution: 960px x 540px
        - Interface: 8-bit parallel (NOT SPI)

    Unlike the other NicheGraphics EInk drivers, this one drives a parallel e-paper
    panel via the FastEPD library. SPI parameters passed to begin() are ignored.

    The ED047TC1 panel has an inactive pixel border on all four edges (~4–8 physical
    pixels). DISPLAY_WIDTH / DISPLAY_HEIGHT expose a reduced "safe area" to InkHUD so
    that content is never drawn into this dead zone. The update() method copies the
    InkHUD frame buffer into the centre of the larger physical 960×540 buffer, using
    H_OFFSET_BYTES (horizontal, whole bytes = 8 pixels per byte),
    V_OFFSET_TOP and V_OFFSET_BOTTOM (vertical, pixel rows) to position it.

    Changing these constants shifts content inward from each physical edge:
        H_OFFSET_BYTES = 1  →  8px left margin, 8px right margin   (960 – 8 – 8   = 944)
        V_OFFSET_TOP   = 9  →  9px top margin  (asymmetric: top ≠ bottom)
        V_OFFSET_BOTTOM = 8 →  8px bottom margin                    (540 – 9 – 8   = 523)

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./EInk.h"

// Forward declare to avoid pulling FastEPD into all translation units
class FASTEPD;

namespace NicheGraphics::Drivers
{

class ED047TC1 : public EInk
{
    // Safe-area dimensions exposed to InkHUD (physical panel is 960×540).
    //
    // The ED047TC1 has an inactive pixel border on all physical edges.
    // The physical buffer coordinates do NOT directly match the visual orientation
    // due to FastEPD's portrait scan direction and InkHUD's rotation=3 (270° CW):
    //
    //   Physical buffer          Visual on device (rotation=3)
    //   ─────────────────        ──────────────────────────────
    //   Physical LEFT  cols  →   Visual TOP    edge
    //   Physical RIGHT cols  →   Visual BOTTOM edge
    //   Physical TOP   rows  →   Visual RIGHT  edge
    //   Physical BOTTOM rows →   Visual LEFT   edge
    //
    // Offset constants shift the InkHUD safe-area away from each physical dead zone:
    //   H_OFFSET_BYTES : whole bytes from physical left  (8px per byte, affects visual TOP)
    //   Physical right margin = 960 − H_OFFSET_BYTES×8 − DISPLAY_WIDTH  (affects visual BOTTOM)
    //   V_OFFSET_TOP   : pixel rows from physical top    (affects visual RIGHT)
    //   V_OFFSET_BOTTOM: pixel rows from physical bottom (affects visual LEFT)
    //
    // Calibrated by flashing a 1px border box and adjusting until all 4 sides are visible.

    static constexpr uint16_t DISPLAY_WIDTH = 944;  // 960 − H_OFFSET_BYTES×8 − right_margin (8+8 = 16px)
    static constexpr uint16_t DISPLAY_HEIGHT = 523; // 540 − V_OFFSET_TOP − V_OFFSET_BOTTOM (9+8 = 17px)

    static constexpr uint8_t H_OFFSET_BYTES = 1;  // visual TOP  : 8px physical left margin
                                                  // visual BOTTOM: 960−8−944=8px physical right margin
    static constexpr uint8_t V_OFFSET_TOP = 9;    // visual RIGHT : CONFIRMED OK
    static constexpr uint8_t V_OFFSET_BOTTOM = 8; // visual LEFT  : 8px physical bottom margin

    static constexpr UpdateTypes supported = static_cast<UpdateTypes>(FULL | FAST);

  public:
    ED047TC1() : EInk(DISPLAY_WIDTH, DISPLAY_HEIGHT, supported) {}

    // EInk interface — SPI params are not used for this parallel display
    void begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst = 0xFF) override;
    void update(uint8_t *imageData, UpdateTypes type) override;

  protected:
    bool isUpdateDone() override { return true; } // FastEPD updates are blocking

  private:
    FASTEPD *epaper = nullptr;
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
