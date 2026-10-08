/*

E-Ink display driver for Heltec Vision Master E290, FPC-7519revb
    - OTP LUT + internal temp sensor (ZJY128296 approach)
    - 1-byte X offset from DEPG0290BNS800
    - Enable with -D EINK_HELTEC_VME290_OTP (see variant README)

Do not use on 15-22243-M0282 panels: FAST refresh will not paint.

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
