#pragma once

#include "graphics/DeviceUiPolicy.h"

class T5S3UiPolicy final : public DeviceUiPolicy
{
  public:
    explicit T5S3UiPolicy(bool v2) : v2(v2) {}
    const DeviceUiMetrics &metrics() const override;
    bool usesExpandedEinkUi() const override { return v2; }
    bool isT5S3() const override { return true; }
    bool usesPanelCoordinateMapping() const override { return v2; }
    bool includesPsramUsage() const override { return false; }
    DeviceParallelPanel parallelPanel() const override
    {
        return v2 ? DeviceParallelPanel::T5S3V2 : DeviceParallelPanel::T5S3V1;
    }
    uint32_t responsiveRefreshThrottleMs() const override { return v2 ? 200 : 1000; }
    uint32_t backgroundRefreshIntervalMs() const override { return v2 ? 5 * 60 * 1000 : 0; }
    uint8_t menuFullRefreshPeriod() const override { return v2 ? 5 : 0; }
    bool supportsT5Keyboard() const override { return v2; }
    bool supportsUserBacklightControl() const override { return v2; }
    void initializeUserBacklightControl() override;
    bool userBacklightEnabled() const override;
    void setUserBacklightEnabled(bool enabled) override;
    void mapLogicalToPanel(uint16_t logicalX, uint16_t logicalY, uint16_t &panelX,
                           uint16_t &panelY) const override;
    void mapPanelToLogical(uint16_t panelX, uint16_t panelY, uint16_t &logicalX,
                           uint16_t &logicalY) const override;

  private:
    bool v2;
};
