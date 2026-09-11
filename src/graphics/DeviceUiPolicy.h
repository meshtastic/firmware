#pragma once

#include <cstdint>

enum class DeviceParallelPanel : uint8_t {
    None,
    T5S3V1,
    T5S3V2,
};

struct DeviceUiMetrics
{
    bool largeProfile = false;
    const uint8_t *fontMeta = nullptr;
    const uint8_t *fontBody = nullptr;
    const uint8_t *fontTitle = nullptr;
    const uint8_t *fontSmall = nullptr;
    const uint8_t *fontMedium = nullptr;
    const uint8_t *fontLarge = nullptr;
    uint16_t contentMargin = 8;
    uint16_t footerReserve = 16;
    uint16_t nodeCardHeight = 50;
    uint16_t nodeCardGap = 4;
    uint16_t nodeMaxRows = 4;
    uint16_t favoriteCardHeight = 102;
    uint16_t favoriteCompassWidth = 72;
    uint16_t favoriteRowHeight = 26;
    uint16_t positionPanelHeight = 112;
    uint16_t positionCardGap = 8;
    uint16_t positionCompassWidth = 104;
    uint16_t positionRowHeight = 26;
    uint16_t homeMessageHeight = 27;
    uint16_t homeGridGap = 4;
    uint16_t homeCellHeight = 52;
    uint16_t homeStatusRowHeight = 22;
    uint16_t debugPanelTitleHeight = 22;
    uint16_t debugRowHeight = 29;
    uint16_t debugUtilizationRowHeight = 43;
    uint16_t systemUsageTitleHeight = 22;
    uint16_t systemUsageRowHeight = 34;
    uint16_t systemStatusTitleHeight = 22;
    uint16_t systemStatusRowHeight = 26;
    uint16_t menuWidth = 218;
    uint16_t menuTitleHeight = 25;
    uint16_t menuRowHeight = 30;
    uint16_t menuMessageRowHeight = 18;
    uint16_t menuBottomPadding = 7;
    uint16_t menuScreenMargin = 4;
    uint16_t messageMargin = 2;
    uint16_t messageScrollbarWidth = 3;
    uint16_t messageBubblePadX = 3;
    uint16_t messageBubblePadY = 4;
    uint16_t messageBubbleRadius = 4;
    uint16_t messageBubbleMinWidth = 24;
    uint16_t messageTextIndent = 2;
    uint16_t navIconSize = 16;
    uint16_t navSpacing = 8;
    uint16_t navIconDrawSize = 16;
    uint16_t navIconScale = 1;
    uint16_t navTouchExpansion = 2;
    uint16_t batteryScale = 1;
    uint16_t keyMinHeight = 0;
};

class DeviceUiPolicy
{
  public:
    virtual ~DeviceUiPolicy() = default;

    virtual const DeviceUiMetrics &metrics() const = 0;
    virtual bool usesExpandedEinkUi() const { return false; }
    virtual bool isT5S3() const { return false; }
    virtual bool usesPanelCoordinateMapping() const { return false; }
    virtual bool usesSharedSpiEink() const { return false; }
    virtual bool includesPsramUsage() const { return true; }
    virtual bool supportsBrightness() const { return false; }
    virtual DeviceParallelPanel parallelPanel() const { return DeviceParallelPanel::None; }
    virtual uint32_t responsiveRefreshThrottleMs() const { return 1000; }
    virtual uint32_t backgroundRefreshIntervalMs() const { return 0; }
    virtual uint8_t menuFullRefreshPeriod() const { return 0; }
    virtual bool supportsT5Keyboard() const { return false; }
    virtual bool supportsUserBacklightControl() const { return false; }
    virtual void initializeUserBacklightControl() {}
    virtual bool userBacklightEnabled() const { return false; }
    virtual void setUserBacklightEnabled(bool enabled) { (void)enabled; }

    virtual void mapLogicalToPanel(uint16_t logicalX, uint16_t logicalY, uint16_t &panelX,
                                   uint16_t &panelY) const
    {
        panelX = logicalX;
        panelY = logicalY;
    }

    virtual void mapPanelToLogical(uint16_t panelX, uint16_t panelY, uint16_t &logicalX,
                                   uint16_t &logicalY) const
    {
        logicalX = panelX;
        logicalY = panelY;
    }
};

const DeviceUiPolicy &getDefaultDeviceUiPolicy();
const DeviceUiMetrics &getDefaultDeviceUiMetrics();
DeviceUiPolicy *getDeviceUiPolicy();

inline const DeviceUiMetrics &deviceUiMetrics()
{
    return getDeviceUiPolicy()->metrics();
}
