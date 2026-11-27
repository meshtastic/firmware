/*

E-Ink display driver for Heltec Vision Master E290
    - Same PCB ships two panels; FAST waveform is selected at runtime
    - CustomLut: flex 15-22243-M0282 (stock DEPG0290BNS800 LUT)
    - OtpFast: flex FPC-7519revb (OTP mode 2)
    - Unset: FAST promoted to FULL until first-boot panel select

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./HeltecVME290Waveform.h"
#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class HeltecVME290 : public SSD16XX
{
  private:
    static constexpr uint32_t width = 128;
    static constexpr uint32_t height = 296;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    HeltecVME290() : SSD16XX(width, height, supported, 1) { activeInstance = this; }

    static HeltecVME290 *getActive() { return activeInstance; }

    void begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst = -1) override;
    void update(uint8_t *imageData, UpdateTypes type) override;

    HeltecVME290Waveform waveform() const { return mode; }
    void setWaveform(HeltecVME290Waveform next);
    void toggleWaveform(); // CustomLut <-> OtpFast (Unset becomes OtpFast)

    // First-boot / recovery: paint OTP FAST regardless of stored mode.
    void updateOtpFast(uint8_t *imageData);

    // Blocking FULL refresh that shows the current mode label (rotation matches InkHUD default).
    void showModeBanner();

    // Blocking first-boot panel select (optical OTP FAST probe). No-op if mode already set.
    void runPanelSelectIfNeeded(uint8_t userButtonPin, uint8_t auxButtonPin);

  protected:
    void configVoltages() override;
    void configWaveform() override;
    void configUpdateSequence() override;
    void detachFromUpdate() override;
    void finalizeUpdate() override;

  private:
    void loadMode();
    void saveMode();
    // updated=false: initial FULL frame; updated=true: OTP FAST frame (status line only differs).
    void paintSelectFrame(uint8_t *buffer, bool updated);
    void paintBannerFrame(uint8_t *buffer, const char *label);
    HeltecVME290Waveform waitForSelectChoice(uint8_t userButtonPin, uint8_t auxButtonPin);

    HeltecVME290Waveform mode = HeltecVME290Waveform::Unset;
    bool forceOtpFast = false; // one-shot for updateOtpFast
    static HeltecVME290 *activeInstance;
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
