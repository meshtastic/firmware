#pragma once

#include "graphics/DeviceUiPolicy.h"

class TDeckUiPolicy final : public DeviceUiPolicy
{
  public:
    const DeviceUiMetrics &metrics() const override;
    bool usesExpandedEinkUi() const override { return true; }
    bool usesSharedSpiEink() const override { return true; }
    uint32_t responsiveRefreshThrottleMs() const override { return 200; }
};
