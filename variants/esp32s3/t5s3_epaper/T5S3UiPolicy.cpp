#include "T5S3UiPolicy.h"

#include "graphics/fonts/EinkDisplayFonts.h"
#include "graphics/ScreenFonts.h"
#include "variant.h"

namespace
{
const DeviceUiMetrics t5s3Metrics = {
    true,
    FONT_MEDIUM_LOCAL,
    FONT_MEDIUM_LOCAL,
    Monospaced_plain_30,
    Monospaced_plain_30,
    Monospaced_plain_30,
    Monospaced_plain_30,
    16,
    120,
    88,
    8,
    8,
    168,
    140,
    32,
    220,
    16,
    220,
    34,
    48,
    8,
    84,
    32,
    48,
    50,
    70,
    42,
    58,
    42,
    42,
    528,
    52,
    68,
    30,
    12,
    16,
    16,
    5,
    8,
    6,
    6,
    40,
    4,
    40,
    20,
    32,
    4,
    16,
    2,
    54,
};
} // namespace

const DeviceUiMetrics &T5S3UiPolicy::metrics() const
{
    if (!v2)
        return getDefaultDeviceUiMetrics();
    return t5s3Metrics;
}

void T5S3UiPolicy::mapLogicalToPanel(uint16_t logicalX, uint16_t logicalY, uint16_t &panelX,
                                     uint16_t &panelY) const
{
    if (!v2) {
        panelX = logicalX;
        panelY = logicalY;
        return;
    }
    panelX = logicalY;
    panelY = static_cast<uint16_t>(539 - logicalX);
}

void T5S3UiPolicy::mapPanelToLogical(uint16_t panelX, uint16_t panelY, uint16_t &logicalX,
                                     uint16_t &logicalY) const
{
    if (!v2) {
        logicalX = panelX;
        logicalY = panelY;
        return;
    }
    logicalX = static_cast<uint16_t>(539 - panelY);
    logicalY = panelX;
}

void T5S3UiPolicy::initializeUserBacklightControl()
{
    if (v2)
        t5BacklightLoadUserPreference();
}

bool T5S3UiPolicy::userBacklightEnabled() const
{
    return v2 && t5BacklightIsUserEnabled();
}

void T5S3UiPolicy::setUserBacklightEnabled(bool enabled)
{
    if (!v2)
        return;

    t5BacklightSetUserEnabled(enabled);
    t5BacklightSaveUserPreference(enabled);
}
