/*

E-Ink display driver
    - GDEW0102T4
    - Controller: UC8175
    - Size: 1.02 inch
    - Resolution: 80px x 128px

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./UC8175.h"

namespace NicheGraphics::Drivers
{

class GDEW0102T4 : public UC8175
{
  private:
    static constexpr uint16_t width = 80;
    static constexpr uint16_t height = 128;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    struct FastConfig {
        uint8_t reg30;
        uint8_t reg50;
        uint8_t reg82;
        uint8_t lutW2;
        uint8_t lutB2;
    };

    GDEW0102T4();
    void setFastConfig(FastConfig cfg);
    FastConfig getFastConfig() const;

  protected:
    void configCommon() override;
    void configFull() override;
    void configFast() override;
    void writeOldImage() override;
    void finalizeUpdate() override;

  private:
    FastConfig fastConfig = {0x13, 0xF2, 0x12, 0x0E, 0x14};
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
