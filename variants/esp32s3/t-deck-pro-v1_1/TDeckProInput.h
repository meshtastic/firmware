#pragma once

#include "input/DeviceInputProvider.h"

class TDeckProInput final : public DeviceInputProvider
{
  public:
    void begin() override;
    std::unique_ptr<TCA8418KeyboardBase> createTca8418Keyboard() override;
    uint8_t tca8418KeyboardAddress() const override { return 0x34; }
    bool usesGestureRecognizer() const override { return true; }
};
