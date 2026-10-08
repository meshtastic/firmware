/*

E-Ink display driver for Heltec Vision Master E290
    - Panel: DEPG0290BNS800F6_V2.1
    - Size: 2.9 inch
    - Resolution: 128px x 296px

HYBRID DRIVER - combines two approaches to fix ghosting:

From ZJY128296_029EAAMFGN (OTP-based):
    - OTP LUT: waveform from controller memory (not custom in firmware)
    - Internal temperature sensor (0x80) for automatic waveform selection
    - Border waveform 0x05 (follow LUT1, drive white)
    - Update sequence 0xFF (differential from OTP)

From DEPG0290BNS800:
    - Buffer offset: 1 byte (panel wiring quirk)

NOT used from ZJY:
    - configScanning override (not needed for this panel)

Result: No ghosting, faster refresh (~300ms), auto temperature adaptation.

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class HeltecVME290 : public SSD16XX
{
    // Display properties
  private:
    static constexpr uint32_t width = 128;
    static constexpr uint32_t height = 296;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    HeltecVME290() : SSD16XX(width, height, supported, 1) {} // offset 1 byte like DEPG0290BNS800

  protected:
    void configWaveform() override;
    void configUpdateSequence() override;
    void detachFromUpdate() override;
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
