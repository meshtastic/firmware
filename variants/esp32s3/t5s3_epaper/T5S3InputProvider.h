#pragma once

#include "input/DeviceInputProvider.h"

#if !defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS) && defined(T5_S3_EPAPER_PRO_V2)
#include "T5S3Keyboard.h"
#endif

class T5S3InputProvider final : public DeviceInputProvider
{
  public:
    explicit T5S3InputProvider(bool v2) : v2(v2) {}
    ~T5S3InputProvider() override;

    bool usesGestureRecognizer() const override { return v2; }
    bool supportsTextInput() const override;
    void startTextInput(const char *header, const char *initialText, uint32_t durationMs,
                        std::function<void(const std::string &)> callback) override;
    void stopTextInput(bool callEmptyCallback) override;
    bool handleTextInput(const InputEvent &event) override;
    bool drawTextInput(OLEDDisplay *display) override;
    bool textInputIsActive() const override;

  private:
    bool v2;
#if !defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS) && defined(T5_S3_EPAPER_PRO_V2)
    std::unique_ptr<graphics::T5S3Keyboard> keyboard;
#endif
};
