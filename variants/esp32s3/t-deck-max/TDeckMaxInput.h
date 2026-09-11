#pragma once

#include "input/DeviceInputProvider.h"

class TDeckMaxInput final : public DeviceInputProvider
{
  public:
    void begin() override;
    std::unique_ptr<TCA8418KeyboardBase> createTca8418Keyboard() override;
    uint8_t tca8418KeyboardAddress() const override;
    bool usesGestureRecognizer() const override { return true; }
    bool isMaxTouchKeySource(const char *source) const override;
    bool isSafeMenuBackLabel(const char *label) const override;
};
