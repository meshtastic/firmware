#pragma once

#include <stddef.h>
#include <stdint.h>

namespace t5s3_epaper
{

static constexpr uint16_t LOGICAL_WIDTH = 540;
static constexpr uint16_t LOGICAL_HEIGHT = 960;
static constexpr uint16_t PANEL_WIDTH = 960;
static constexpr uint16_t PANEL_HEIGHT = 540;
static constexpr uint16_t PANEL_ROW_BYTES = (PANEL_WIDTH + 7) / 8;

struct LogicalPoint
{
    uint16_t x;
    uint16_t y;
};

struct PanelPoint
{
    uint16_t x;
    uint16_t y;
};

struct TouchPoint
{
    uint16_t x;
    uint16_t y;
};

struct PanelPixelAddress
{
    uint32_t byteOffset;
    uint8_t bitMask;
};

constexpr size_t logicalFramebufferBytes()
{
    return static_cast<size_t>(LOGICAL_WIDTH) * ((LOGICAL_HEIGHT + 7) / 8);
}

constexpr size_t panelFramebufferBytes()
{
    return static_cast<size_t>(PANEL_ROW_BYTES) * PANEL_HEIGHT;
}

constexpr uint8_t fastEpdBitForOledPixel(bool oledWhite)
{
    return oledWhite ? 1 : 0;
}

constexpr PanelPoint logicalToPanel(LogicalPoint point)
{
    return {point.y, static_cast<uint16_t>(PANEL_HEIGHT - 1 - point.x)};
}

constexpr PanelPixelAddress logicalPixelToPanelAddress(LogicalPoint point)
{
    const PanelPoint panel = logicalToPanel(point);
    return {static_cast<uint32_t>(panel.y) * PANEL_ROW_BYTES + panel.x / 8,
            static_cast<uint8_t>(0x80U >> (panel.x & 7U))};
}

constexpr LogicalPoint panelToLogical(PanelPoint point)
{
    return {static_cast<uint16_t>(PANEL_HEIGHT - 1 - point.y), point.x};
}

// T5S3 GT911 reports coordinates in the portrait logical frame.
constexpr LogicalPoint gt911ToLogical(TouchPoint point)
{
    return {point.x, point.y};
}

constexpr bool isValidGt911Point(TouchPoint point)
{
    return point.x < LOGICAL_WIDTH && point.y < LOGICAL_HEIGHT;
}

} // namespace t5s3_epaper
