#include "PowerBankPage.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace graphics
{
namespace PowerBankPage
{

static const char *portWord(PowerBankDetail::PortState state)
{
    switch (state) {
    case PowerBankDetail::PORT_CHARGING_BANK:
        return "In";
    case PowerBankDetail::PORT_POWERING_DEVICE:
        return "Out";
    default:
        return "-";
    }
}

// Appends to a line, truncating rather than overflowing.
static void append(char *line, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void append(char *line, const char *fmt, ...)
{
    const size_t used = strlen(line);
    if (used >= PAGE_LINE_CHARS - 1)
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(line + used, PAGE_LINE_CHARS - used, fmt, args);
    va_end(args);
}

uint8_t formatLines(bool haveStatus, const PowerBankStatus &status, bool haveDetail, const PowerBankDetail &detail,
                    char lines[PAGE_LINES][PAGE_LINE_CHARS])
{
    for (uint8_t i = 0; i < PAGE_LINES; i++)
        lines[i][0] = '\0';
    uint8_t n = 0;

    if (haveStatus)
        append(lines[n], "Battery %d%% %u.%02uV%s", status.batteryPercent, status.batteryMv / 1000,
               (status.batteryMv % 1000) / 10, status.charging ? " charging" : "");
    else
        append(lines[n], "Battery --");
    n++;

    if (!haveDetail) {
        append(lines[n++], "Waiting for power bank");
        return n;
    }
    if (!detail.live) {
        append(lines[n++], "Idle, nothing plugged in");
        return n;
    }

    for (uint8_t i = 0; i < detail.numPorts && i < PowerBankDetail::MAX_PORTS; i++)
        append(lines[n], "%s%s %s", i ? "  " : "", detail.portNames[i], portWord(detail.portStates[i]));
    if (detail.fastCharge)
        append(lines[n], "  %s", detail.fastCharge);
    n++;

    if (detail.hasWireless) {
        if (detail.wirelessFault)
            append(lines[n], "Wireless: object on pad!");
        else if (detail.wirelessActive)
            append(lines[n], "Wireless %u.%uW %s", (unsigned)(detail.wirelessMw / 1000),
                   (unsigned)(detail.wirelessMw % 1000 / 100), detail.wirelessProtocol ? detail.wirelessProtocol : "");
        else
            append(lines[n], "Wireless off");
        n++;
    }

    append(lines[n], "Bus %u.%uV %u.%02uA", detail.busMv / 1000, detail.busMv % 1000 / 100, detail.busMa / 1000,
           detail.busMa % 1000 / 10);
    for (uint8_t i = 0; i < detail.numTemperatures && i < PowerBankDetail::MAX_TEMPERATURES; i++)
        append(lines[n], "%s%d", i ? "/" : "  ", detail.temperaturesC[i]);
    if (detail.numTemperatures)
        append(lines[n], "C%s", detail.overTemperature ? " HOT" : "");
    n++;
    return n;
}

} // namespace PowerBankPage
} // namespace graphics

#if HAS_SCREEN && HAS_POWERBANK
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"

namespace graphics
{
namespace PowerBankPage
{
void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    graphics::drawCommonHeader(display, x, y, "Power Bank");
    y += BASEUI_BELOW_HEADER_MARGIN;

    PowerBankStatus status;
    PowerBankDetail detail;
    const bool haveStatus = powerBank && powerBank->getStatus(status);
    const bool haveDetail = powerBank && powerBank->getDetail(detail);
    char lines[PAGE_LINES][PAGE_LINE_CHARS];
    const uint8_t count = formatLines(haveStatus, status, haveDetail, detail, lines);
    for (uint8_t i = 0; i < count; i++)
        display->drawString(x + BASEUI_BODY_LR_MARGIN, getTextPositions(display)[i + 1] + y, lines[i]);

    graphics::drawCommonFooter(display, x, y);
}
} // namespace PowerBankPage
} // namespace graphics
#endif
