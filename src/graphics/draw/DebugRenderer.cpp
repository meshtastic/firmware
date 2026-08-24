#include "configuration.h"
#if HAS_SCREEN
#include "../Screen.h"
#include "DebugRenderer.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "UIRenderer.h"
#include "airtime.h"
#include "gps/RTC.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/TFTColorRegions.h"
#include "graphics/TFTPalette.h"
#include "graphics/TimeFormatters.h"
#include "graphics/images.h"
#include "main.h"

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
#include "mesh/wifi/WiFiAPClient.h"
#include <WiFi.h>
#ifdef ARCH_ESP32
#include "mesh/wifi/WiFiAPClient.h"
#endif
#endif

#include <DisplayFormatters.h>
#include <RadioLibInterface.h>
#include <target_specific.h>

using namespace meshtastic;

// External variables
extern std::unique_ptr<graphics::Screen> screen;
extern NodeStatus *nodeStatus;
extern AirTime *airTime;

// External functions from Screen.cpp
extern bool heartbeat;

namespace graphics
{
namespace DebugRenderer
{

// ****************************
// * WiFi Screen              *
// ****************************
void drawFrameWiFi(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    int line = 1;

    // === Set Title
    const char *titleStr = "WiFi";

    // === Header ===
    graphics::drawCommonHeader(display, x, y, titleStr);

    const char *wifiName = config.network.wifi_ssid;

    if (WiFi.status() != WL_CONNECTED) {
        display->drawString(x, getTextPositions(display)[line++], "WiFi: Not Connected");
    } else {
        display->drawString(x, getTextPositions(display)[line++], "WiFi: Connected");

        char rssiStr[32];
        snprintf(rssiStr, sizeof(rssiStr), "RSSI: %d", WiFi.RSSI());
        display->drawString(x, getTextPositions(display)[line++], rssiStr);
    }

    /*
    - WL_CONNECTED: assigned when connected to a WiFi network;
    - WL_NO_SSID_AVAIL: assigned when no SSID are available;
    - WL_CONNECT_FAILED: assigned when the connection fails for all the attempts;
    - WL_CONNECTION_LOST: assigned when the connection is lost;
    - WL_DISCONNECTED: assigned when disconnected from a network;
    - WL_IDLE_STATUS: it is a temporary status assigned when WiFi.begin() is called and remains active until the number of
    attempts expires (resulting in WL_CONNECT_FAILED) or a connection is established (resulting in WL_CONNECTED);
    - WL_SCAN_COMPLETED: assigned when the scan networks is completed;
    - WL_NO_SHIELD: assigned when no WiFi shield is present;

    */
    if (WiFi.status() == WL_CONNECTED) {
        char ipStr[64];
        snprintf(ipStr, sizeof(ipStr), "IP: %s", WiFi.localIP().toString().c_str());
        display->drawString(x, getTextPositions(display)[line++], ipStr);
    } else if (WiFi.status() == WL_NO_SSID_AVAIL) {
        display->drawString(x, getTextPositions(display)[line++], "SSID Not Found");
    } else if (WiFi.status() == WL_CONNECTION_LOST) {
        display->drawString(x, getTextPositions(display)[line++], "Connection Lost");
    } else if (WiFi.status() == WL_IDLE_STATUS) {
        display->drawString(x, getTextPositions(display)[line++], "Idle ... Reconnecting");
    } else if (WiFi.status() == WL_CONNECT_FAILED) {
        display->drawString(x, getTextPositions(display)[line++], "Connection Failed");
    }
#ifdef ARCH_ESP32
    else {
        // Codes:
        // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#wi-fi-reason-code
        display->drawString(x, getTextPositions(display)[line++],
                            WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(getWifiDisconnectReason())));
    }
#else
    else {
        char statusStr[32];
        snprintf(statusStr, sizeof(statusStr), "Unknown status: %d", WiFi.status());
        display->drawString(x, getTextPositions(display)[line++], statusStr);
    }
#endif

    char ssidStr[64];
    snprintf(ssidStr, sizeof(ssidStr), "SSID: %s", wifiName);
    display->drawString(x, getTextPositions(display)[line++], ssidStr);

    display->drawString(x, getTextPositions(display)[line++], "URL: http://meshtastic.local");

    graphics::drawCommonFooter(display, x, y);

    /* Display a heartbeat pixel that blinks every time the frame is redrawn */
#ifdef SHOW_REDRAWS
    if (heartbeat)
        display->setPixel(0, 0);
    heartbeat = !heartbeat;
#endif
#endif
}

// ****************************
// * LoRa Focused Screen      *
// ****************************
#if (defined(_VARIANT_T_DECK_PRO_V1_1) || defined(T_DECK_MAX) || T5S3_EPD_UI_PROFILE) && defined(USE_EINK)
static void drawTDeckLoRaFocused(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    (void)state;
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    graphics::drawCommonHeader(display, x, y, "LoRa");

    const int screenW = display->getWidth();
    const int screenH = display->getHeight();
    const int contentMargin = T5S3_EPD_UI_CONTENT_MARGIN;
    const int contentLeft = x + contentMargin;
    const int contentRight = x + screenW - contentMargin;
    const int contentWidth = contentRight - contentLeft;
    const int footerReserve = T5S3_EPD_UI_FOOTER_RESERVE;
    const int bodyBottom = y + screenH - footerReserve;

    uint32_t onlineNodes = nodeStatus ? nodeStatus->getNumOnline() : 0;
    uint32_t totalNodes = nodeStatus ? nodeStatus->getNumTotal() : 0;
    char nodeSummary[24];
    snprintf(nodeSummary, sizeof(nodeSummary), "NODES %u/%u", static_cast<unsigned>(onlineNodes),
             static_cast<unsigned>(totalNodes));

    uint8_t dmac[6] = {0};
    getMacAddr(dmac);
    snprintf(screen->ourId, sizeof(screen->ourId), "%02x%02x", dmac[4], dmac[5]);
    char bleSummary[24];
    snprintf(bleSummary, sizeof(bleSummary), "BLE %s", screen->ourId);

    const int summaryY = y + FONT_HEIGHT_SMALL + 5;
    display->setFont(T5S3_EPD_UI_FONT_BODY);
    display->drawString(contentLeft, summaryY, nodeSummary);
    const int bleWidth = display->getStringWidth(bleSummary);
    display->drawString(contentRight - bleWidth, summaryY, bleSummary);
    const int separatorY = summaryY + FONT_HEIGHT_SMALL + 3;
    display->drawLine(contentLeft, separatorY, contentRight, separatorY);

    char modeStr[24];
    if (!config.lora.use_preset) {
        snprintf(modeStr, sizeof(modeStr), "BW%u-SF%u-CR%u", static_cast<unsigned>(config.lora.bandwidth),
                 static_cast<unsigned>(config.lora.spread_factor), static_cast<unsigned>(config.lora.coding_rate));
    } else {
        const char *preset = DisplayFormatters::getModemPresetDisplayName(config.lora.modem_preset, false, true);
        snprintf(modeStr, sizeof(modeStr), "%s", preset ? preset : "Custom");
    }

    char frequencyValue[40];
    if (RadioLibInterface::instance) {
        const float frequency = RadioLibInterface::instance->getFreq();
        if (config.lora.channel_num == 0)
            snprintf(frequencyValue, sizeof(frequencyValue), "%.3f MHz", frequency);
        else
            snprintf(frequencyValue, sizeof(frequencyValue), "%.3f MHz / slot %d", frequency, config.lora.channel_num);
    } else {
        snprintf(frequencyValue, sizeof(frequencyValue), "--");
    }

    const char *region = myRegion ? myRegion->name : "UNSET";
    const char *role = DisplayFormatters::getDeviceRole(config.device.role);
    const char *txState = config.lora.tx_enabled ? "ENABLED" : "DISABLED";
    int channelPercent = airTime ? static_cast<int>(airTime->channelUtilizationPercent() + 0.5f) : 0;
    if (channelPercent < 0)
        channelPercent = 0;
    if (channelPercent > 100)
        channelPercent = 100;

    const int panelTop = separatorY + 7;
    const int panelTitleHeight = T5S3_EPD_UI_DEBUG_PANEL_TITLE_HEIGHT;
    const int normalRowHeight = T5S3_EPD_UI_DEBUG_ROW_HEIGHT;
    const int utilizationRowHeight = T5S3_EPD_UI_DEBUG_UTILIZATION_ROW_HEIGHT;
    const int panelHeight = panelTitleHeight + normalRowHeight * 5 + utilizationRowHeight + 4;
    display->drawRect(contentLeft, panelTop, contentWidth, panelHeight);
    display->setFont(T5S3_EPD_UI_FONT_BODY);
    display->drawString(contentLeft + 7, panelTop + 5, "RADIO CONFIG");
    const char *radioState = config.lora.tx_enabled ? "TX READY" : "TX OFF";
    const int radioStateWidth = display->getStringWidth(radioState);
    display->drawString(contentRight - radioStateWidth - 7, panelTop + 5, radioState);

    int rowY = panelTop + panelTitleHeight;
    auto drawValueRow = [&](const char *label, const char *value) {
        display->setFont(T5S3_EPD_UI_FONT_BODY);
        display->drawString(contentLeft + 9, rowY + 6, label);
        char clippedValue[64];
        const int valueMaxWidth = contentWidth - 92;
        UIRenderer::truncateStringWithEmotes(display, value, clippedValue, sizeof(clippedValue), valueMaxWidth);
        const int valueWidth = display->getStringWidth(clippedValue);
        display->drawString(contentRight - valueWidth - 9, rowY + 6, clippedValue);
        display->drawLine(contentLeft + 7, rowY + normalRowHeight - 1, contentRight - 7, rowY + normalRowHeight - 1);
        rowY += normalRowHeight;
    };

    drawValueRow("REGION", region);
    drawValueRow("PRESET", modeStr);
    drawValueRow("FREQUENCY", frequencyValue);
    drawValueRow("ROLE", role ? role : "Unknown");
    drawValueRow("TX", txState);

    display->setFont(T5S3_EPD_UI_FONT_BODY);
    display->drawString(contentLeft + 9, rowY + 6, "CHANNEL USE");
    char channelValue[12];
    snprintf(channelValue, sizeof(channelValue), "%d%%", channelPercent);
    const int channelValueWidth = display->getStringWidth(channelValue);
    display->drawString(contentRight - channelValueWidth - 9, rowY + 6, channelValue);
    const int barX = contentLeft + 9;
    const int barY = rowY + 27;
    const int barWidth = contentWidth - 18;
    const int barHeight = 8;
    display->drawRect(barX, barY, barWidth, barHeight);
    const int fillWidth = ((barWidth - 2) * channelPercent) / 100;
    if (fillWidth > 0)
        display->fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2);

    if (panelTop + panelHeight <= bodyBottom)
        display->drawLine(contentLeft, panelTop + panelHeight, contentRight, panelTop + panelHeight);
    graphics::drawCommonFooter(display, x, y);
}

struct TDeckUsageRow {
    const char *label;
    uint32_t used;
    uint32_t total;
};

static void drawTDeckSystemScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    (void)state;
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    graphics::drawCommonHeader(display, x, y, "System");

    const int screenW = display->getWidth();
    const int screenH = display->getHeight();
    const int contentMargin = T5S3_EPD_UI_CONTENT_MARGIN;
    const int contentLeft = x + contentMargin;
    const int contentRight = x + screenW - contentMargin;
    const int contentWidth = contentRight - contentLeft;
    const int footerReserve = T5S3_EPD_UI_FOOTER_RESERVE;
    const int bodyBottom = y + screenH - footerReserve;

    char apiState[48];
    const char *clientWord = currentResolution == ScreenResolution::High ? "Client" : "App";
    snprintf(apiState, sizeof(apiState), "No %ss", clientWord);
    if (service->api_state == service->STATE_BLE)
        snprintf(apiState, sizeof(apiState), "%s / BLE", clientWord);
    else if (service->api_state == service->STATE_WIFI)
        snprintf(apiState, sizeof(apiState), "%s / WiFi", clientWord);
    else if (service->api_state == service->STATE_SERIAL)
        snprintf(apiState, sizeof(apiState), "%s / Serial", clientWord);
    else if (service->api_state == service->STATE_PACKET)
        snprintf(apiState, sizeof(apiState), "%s / Internal", clientWord);
    else if (service->api_state == service->STATE_HTTP)
        snprintf(apiState, sizeof(apiState), "%s / HTTP", clientWord);
    else if (service->api_state == service->STATE_ETH)
        snprintf(apiState, sizeof(apiState), "%s / Ethernet", clientWord);

    const int summaryY = y + FONT_HEIGHT_SMALL + 5;
    display->setFont(T5S3_EPD_UI_FONT_BODY);
    display->drawString(contentLeft, summaryY, "RUNTIME");
    const char *summaryState = isAPIConnected(service->api_state) ? "ACTIVE" : "STANDBY";
    const int summaryStateWidth = display->getStringWidth(summaryState);
    display->drawString(contentRight - summaryStateWidth, summaryY, summaryState);
    const int separatorY = summaryY + FONT_HEIGHT_SMALL + 3;
    display->drawLine(contentLeft, separatorY, contentRight, separatorY);

    TDeckUsageRow usage[4];
    int usageCount = 0;
    const uint32_t heapTotal = static_cast<uint32_t>(memGet.getHeapSize());
    const uint32_t heapFree = static_cast<uint32_t>(memGet.getFreeHeap());
    usage[usageCount++] = {"HEAP", heapTotal > heapFree ? heapTotal - heapFree : 0, heapTotal};

#ifdef ESP32
#ifndef T5_S3_EPAPER_PRO
    const uint32_t psramTotal = static_cast<uint32_t>(memGet.getPsramSize());
    const uint32_t psramFree = static_cast<uint32_t>(memGet.getFreePsram());
    if (psramTotal > 0)
        usage[usageCount++] = {"PSRAM", psramTotal > psramFree ? psramTotal - psramFree : 0, psramTotal};
#endif
    const uint32_t flashTotal = static_cast<uint32_t>(FSCom.totalBytes());
    const uint32_t flashUsed = static_cast<uint32_t>(FSCom.usedBytes());
    if (flashTotal > 0)
        usage[usageCount++] = {"FLASH", flashUsed, flashTotal};
#endif

    const int usageTop = separatorY + 7;
    const int usageTitleHeight = T5S3_EPD_UI_SYSTEM_USAGE_TITLE_HEIGHT;
    const int usageRowHeight = T5S3_EPD_UI_SYSTEM_USAGE_ROW_HEIGHT;
    const int usagePanelHeight = usageTitleHeight + usageCount * usageRowHeight + 4;
    display->drawRect(contentLeft, usageTop, contentWidth, usagePanelHeight);
    display->setFont(T5S3_EPD_UI_FONT_BODY);
    display->drawString(contentLeft + 7, usageTop + 5, "RESOURCE USAGE");
    const int usageBottom = usageTop + usagePanelHeight;
    for (int i = 0; i < usageCount; ++i) {
        const int rowY = usageTop + usageTitleHeight + i * usageRowHeight;
        const uint32_t total = usage[i].total;
        const uint32_t used = usage[i].used > total ? total : usage[i].used;
        const int percent = total > 0 ? static_cast<int>((static_cast<uint64_t>(used) * 100) / total) : 0;
        char value[32];
        snprintf(value, sizeof(value), "%d%%  %u/%uK", percent, static_cast<unsigned>(used / 1024),
                 static_cast<unsigned>(total / 1024));
        display->setFont(T5S3_EPD_UI_FONT_BODY);
        display->drawString(contentLeft + 9, rowY + 4, usage[i].label);
        const int valueWidth = display->getStringWidth(value);
        display->drawString(contentRight - valueWidth - 9, rowY + 4, value);

        const int barX = contentLeft + 9;
        const int barY = rowY + 21;
        const int barWidth = contentWidth - 18;
        const int barHeight = 7;
        display->drawRect(barX, barY, barWidth, barHeight);
        const int fillWidth = ((barWidth - 2) * percent) / 100;
        if (fillWidth > 0)
            display->fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2);
        if (i + 1 < usageCount)
            display->drawLine(contentLeft + 7, rowY + usageRowHeight - 1, contentRight - 7, rowY + usageRowHeight - 1);
    }

    char versionValue[40];
    snprintf(versionValue, sizeof(versionValue), "Ver: %s", optstr(APP_VERSION));
    char uptimeValue[32];
    getUptimeStr(millis(), "Up: ", uptimeValue, sizeof(uptimeValue));

    const int statusTop = usageBottom + 8;
    const int statusTitleHeight = T5S3_EPD_UI_SYSTEM_STATUS_TITLE_HEIGHT;
    const int statusRowHeight = T5S3_EPD_UI_SYSTEM_STATUS_ROW_HEIGHT;
    const int statusPanelHeight = statusTitleHeight + statusRowHeight * 3 + 4;
    display->drawRect(contentLeft, statusTop, contentWidth, statusPanelHeight);
    display->setFont(T5S3_EPD_UI_FONT_BODY);
    display->drawString(contentLeft + 7, statusTop + 5, "RUNTIME STATUS");

    const char *statusLabels[] = {"VERSION", "UPTIME", "API"};
    const char *statusValues[] = {versionValue, uptimeValue, apiState};
    for (int i = 0; i < 3; ++i) {
        const int rowY = statusTop + statusTitleHeight + i * statusRowHeight;
        display->setFont(T5S3_EPD_UI_FONT_BODY);
        display->drawString(contentLeft + 9, rowY + 5, statusLabels[i]);
        char clippedValue[48];
        UIRenderer::truncateStringWithEmotes(display, statusValues[i], clippedValue, sizeof(clippedValue), contentWidth - 96);
        const int valueWidth = display->getStringWidth(clippedValue);
        display->drawString(contentRight - valueWidth - 9, rowY + 5, clippedValue);
        if (i < 2)
            display->drawLine(contentLeft + 7, rowY + statusRowHeight - 1, contentRight - 7, rowY + statusRowHeight - 1);
    }

    if (statusTop + statusPanelHeight <= bodyBottom)
        display->drawLine(contentLeft, statusTop + statusPanelHeight, contentRight, statusTop + statusPanelHeight);
    graphics::drawCommonFooter(display, x, y);
}
#endif

void drawLoRaFocused(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
#if (defined(_VARIANT_T_DECK_PRO_V1_1) || defined(T_DECK_MAX) || T5S3_EPD_UI_PROFILE) && defined(USE_EINK)
    drawTDeckLoRaFocused(display, state, x, y);
    return;
#endif
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    int line = 1;

    // === Set Title
    const char *titleStr = (currentResolution == ScreenResolution::High) ? "LoRa Info" : "LoRa";

    // === Header ===
    graphics::drawCommonHeader(display, x, y, titleStr);

    // === First Row: Region / BLE Name ===
    graphics::UIRenderer::drawNodes(display, x, getTextPositions(display)[line] + 2, nodeStatus, 0, true, "");

    uint8_t dmac[6];
    char shortnameble[35];
    getMacAddr(dmac);
    snprintf(screen->ourId, sizeof(screen->ourId), "%02x%02x", dmac[4], dmac[5]);
    if (currentResolution == ScreenResolution::UltraLow) {
        snprintf(shortnameble, sizeof(shortnameble), "%s", screen->ourId);
    } else {
        snprintf(shortnameble, sizeof(shortnameble), "BLE: %s", screen->ourId);
    }
    int textWidth = display->getStringWidth(shortnameble);
    int nameX = (SCREEN_WIDTH - textWidth);
    display->drawString(nameX, getTextPositions(display)[line++], shortnameble);

    if (!graphics::isCompactPanel(display)) {
        // === Second Row: Role ===
        auto role = DisplayFormatters::getDeviceRole(config.device.role);
        char device_role[25];
        snprintf(device_role, sizeof(device_role), "Role: %s", role);
        textWidth = display->getStringWidth(device_role);
        nameX = (SCREEN_WIDTH - textWidth) / 2;
        display->drawString(nameX, getTextPositions(display)[line++], device_role);
    }

    // === Third Row: Radio Preset ===
    // For custom modem settings show the actual parameters; for presets use the preset name.
    char modeStr[16];
    if (!config.lora.use_preset) {
        snprintf(modeStr, sizeof(modeStr), "BW%u-SF%u-CR%u", static_cast<unsigned>(config.lora.bandwidth),
                 static_cast<unsigned>(config.lora.spread_factor), static_cast<unsigned>(config.lora.coding_rate));
    } else {
        strncpy(modeStr, DisplayFormatters::getModemPresetDisplayName(config.lora.modem_preset, false, true),
                sizeof(modeStr) - 1);
        modeStr[sizeof(modeStr) - 1] = '\0';
    }

    char regionradiopreset[25];
    const char *region = myRegion ? myRegion->name : NULL;
    if (region != nullptr) {
        if (currentResolution == ScreenResolution::UltraLow) {
            snprintf(regionradiopreset, sizeof(regionradiopreset), "%s", region);
        } else {
            snprintf(regionradiopreset, sizeof(regionradiopreset), "%s/%s", region, modeStr);
        }
    }
    textWidth = display->getStringWidth(regionradiopreset);
    nameX = (SCREEN_WIDTH - textWidth) / 2;
    display->drawString(nameX, getTextPositions(display)[line++], regionradiopreset);

    // === Fourth Row: Frequency / ChanNum ===
    char frequencyslot[35];
    char freqStr[16];
    float freq = RadioLibInterface::instance->getFreq();
    snprintf(freqStr, sizeof(freqStr), "%.3f", freq);
    if (config.lora.channel_num == 0) {
        if (currentResolution == ScreenResolution::UltraLow) {
            snprintf(frequencyslot, sizeof(frequencyslot), "%sMHz", freqStr);
        } else {
            snprintf(frequencyslot, sizeof(frequencyslot), "Freq: %sMHz", freqStr);
        }
    } else {
        if (currentResolution == ScreenResolution::UltraLow) {
            snprintf(frequencyslot, sizeof(frequencyslot), "%sMHz (%d)", freqStr, config.lora.channel_num);
        } else {
            snprintf(frequencyslot, sizeof(frequencyslot), "Freq: %sMHz (%d)", freqStr, config.lora.channel_num);
        }
    }
    size_t len = strlen(frequencyslot);
    if (len >= 4 && strcmp(frequencyslot + len - 4, " (0)") == 0) {
        frequencyslot[len - 4] = '\0'; // Remove the last three characters
    }
    textWidth = display->getStringWidth(frequencyslot);
    nameX = (SCREEN_WIDTH - textWidth) / 2;
    display->drawString(nameX, getTextPositions(display)[line++], frequencyslot);

#if !defined(OLED_TINY)
    // === Fifth Row: Channel Utilization ===
    if (!config.lora.tx_enabled) {
        const char *txdisabled = "Transmit Disabled";
        textWidth = display->getStringWidth(txdisabled);
        display->drawString((SCREEN_WIDTH - textWidth) / 2, getTextPositions(display)[line], txdisabled);
    } else {

        const char *chUtil = "ChUtil:";
        char chUtilPercentage[10];
        snprintf(chUtilPercentage, sizeof(chUtilPercentage), "%2.0f%%", airTime->channelUtilizationPercent());

        int chUtil_x = (currentResolution == ScreenResolution::High) ? display->getStringWidth(chUtil) + 10
                                                                     : display->getStringWidth(chUtil) + 5;
        int chUtil_y = getTextPositions(display)[line] + 3;

        int chutil_bar_width = (currentResolution == ScreenResolution::High) ? 100 : 50;
        int chutil_bar_max_fill = chutil_bar_width - 2; // Account for border
        int chutil_bar_height = (currentResolution == ScreenResolution::High) ? 12 : 7;
        int extraoffset = (currentResolution == ScreenResolution::High) ? 6 : 3;
        int chutil_percent = airTime->channelUtilizationPercent();
        const int raw_chutil_percent = chutil_percent;

        int centerofscreen = SCREEN_WIDTH / 2;
        int total_line_content_width =
            (chUtil_x + chutil_bar_width + display->getStringWidth(chUtilPercentage) + extraoffset) / 2;
        int starting_position = centerofscreen - total_line_content_width;

        display->drawString(starting_position, getTextPositions(display)[line], chUtil);

        // Force 61% or higher to show a full 100% bar, text would still show related percent.
        if (chutil_percent >= 61) {
            chutil_percent = 100;
        }

        // Weighting for nonlinear segments
        float milestone1 = 25;
        float milestone2 = 40;
        float weight1 = 0.45; // Weight for 0-25%
        float weight2 = 0.35; // Weight for 25-40%
        float weight3 = 0.20; // Weight for 40-100%
        float totalWeight = weight1 + weight2 + weight3;

        int seg1 = chutil_bar_max_fill * (weight1 / totalWeight);
        int seg2 = chutil_bar_max_fill * (weight2 / totalWeight);
        int seg3 = chutil_bar_max_fill - seg1 - seg2; // Remainder absorbs rounding errors

        int fillRight = 0;

        if (chutil_percent <= milestone1) {
            fillRight = (seg1 * (chutil_percent / milestone1));
        } else if (chutil_percent <= milestone2) {
            fillRight = seg1 + (seg2 * ((chutil_percent - milestone1) / (milestone2 - milestone1)));
        } else {
            fillRight = seg1 + seg2 + (seg3 * ((chutil_percent - milestone2) / (100 - milestone2)));
        }

        // Draw outline
        display->drawRect(starting_position + chUtil_x, chUtil_y, chutil_bar_width, chutil_bar_height);

        // Fill progress
        if (fillRight > 0) {
#if GRAPHICS_TFT_COLORING_ENABLED
            uint16_t UtilizationFillColor = TFTPalette::Good;
            if (raw_chutil_percent >= 60) {
                UtilizationFillColor = TFTPalette::Bad;
            } else if (raw_chutil_percent >= 35) {
                UtilizationFillColor = TFTPalette::Medium;
            }
            setAndRegisterTFTColorRole(TFTColorRole::UtilizationFill, UtilizationFillColor, TFTPalette::Black,
                                       starting_position + chUtil_x + 1, chUtil_y + 1, fillRight, chutil_bar_height - 2);
#endif
            display->fillRect(starting_position + chUtil_x + 1, chUtil_y + 1, fillRight, chutil_bar_height - 2);
        }

        display->drawString(starting_position + chUtil_x + chutil_bar_width + extraoffset, getTextPositions(display)[line++],
                            chUtilPercentage);
    }
#endif
    graphics::drawCommonFooter(display, x, y);
}

// ****************************
// *      System Screen       *
// ****************************
void drawSystemScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
#if (defined(_VARIANT_T_DECK_PRO_V1_1) || defined(T_DECK_MAX) || T5S3_EPD_UI_PROFILE) && defined(USE_EINK)
    drawTDeckSystemScreen(display, state, x, y);
    return;
#endif
    display->clear();
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // === Set Title
    const char *titleStr = "System";

    // === Header ===
    graphics::drawCommonHeader(display, x, y, titleStr);

    // === Layout ===
    int line = 1;
    const int barHeight = 6;
    const int labelX = x;
    int barsOffset = (currentResolution == ScreenResolution::High) ? 24 : 0;
#ifdef USE_EINK
#if !defined(_VARIANT_T_DECK_PRO_V1_1) && !defined(T_DECK_MAX)
    barsOffset -= 12;
#endif
#if defined(T5_S3_EPAPER_PRO)
    barsOffset += 60;
#endif
#endif
    int barX = x + barsOffset;
    if (currentResolution == ScreenResolution::UltraLow) {
        barX += 45;
    } else {
        barX += 40;
    }
    auto drawUsageRow = [&](const char *label, uint32_t used, uint32_t total, bool isHeap = false) {
        if (total == 0)
            return;

        int percent = (used * 100) / total;

        char combinedStr[24];
        if (currentResolution == ScreenResolution::High) {
            snprintf(combinedStr, sizeof(combinedStr), "%s%3d%%  %u/%uKB", (percent > 80) ? "! " : "", percent, used / 1024,
                     total / 1024);
        } else {
            snprintf(combinedStr, sizeof(combinedStr), "%s%3d%%", (percent > 80) ? "! " : "", percent);
        }

        int textWidth = display->getStringWidth(combinedStr);
        int adjustedBarWidth = SCREEN_WIDTH - barX - textWidth - 6;
        if (adjustedBarWidth < 10)
            adjustedBarWidth = 10;

        int fillWidth = (used * adjustedBarWidth) / total;

        // Label
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->drawString(labelX, getTextPositions(display)[line], label);
#if !defined(OLED_TINY)
        // Bar
        int barY = getTextPositions(display)[line] + (FONT_HEIGHT_SMALL - barHeight) / 2;
        display->setColor(WHITE);
        display->drawRect(barX, barY, adjustedBarWidth, barHeight);

#if GRAPHICS_TFT_COLORING_ENABLED
        uint16_t UtilizationFillColor = TFTPalette::Good;
        if (percent >= 80) {
            UtilizationFillColor = TFTPalette::Bad;
        } else if (percent >= 60) {
            UtilizationFillColor = TFTPalette::Medium;
        }
        setAndRegisterTFTColorRole(TFTColorRole::UtilizationFill, UtilizationFillColor, TFTPalette::Black, barX + 1, barY + 1,
                                   fillWidth - 1, barHeight - 2);
#endif

        display->fillRect(barX, barY, fillWidth, barHeight);
        display->setColor(WHITE);
#endif
        // Value string
        display->setTextAlignment(TEXT_ALIGN_RIGHT);
        display->drawString(SCREEN_WIDTH, getTextPositions(display)[line], combinedStr);
    };

    // === Memory values ===
    uint32_t heapUsed = memGet.getHeapSize() - memGet.getFreeHeap();
    uint32_t heapTotal = memGet.getHeapSize();

    uint32_t flashUsed = 0, flashTotal = 0;
#ifdef ESP32
#ifndef T5_S3_EPAPER_PRO
    uint32_t psramUsed = memGet.getPsramSize() - memGet.getFreePsram();
    uint32_t psramTotal = memGet.getPsramSize();
#endif
    flashUsed = FSCom.usedBytes();
    flashTotal = FSCom.totalBytes();
#endif

    uint32_t sdUsed = 0, sdTotal = 0;
    bool hasSD = false;
    /*
    #ifdef HAS_SDCARD
        hasSD = SD.cardType() != CARD_NONE;
        if (hasSD) {
            sdUsed = SD.usedBytes();
            sdTotal = SD.totalBytes();
        }
    #endif
    */
    // === Draw memory rows
    drawUsageRow("Heap:", heapUsed, heapTotal, true);
#ifdef ESP32
#ifndef T5_S3_EPAPER_PRO
    if (psramUsed > 0) {
        line += 1;
        drawUsageRow("PSRAM:", psramUsed, psramTotal);
    }
#endif
    if (flashTotal > 0) {
        line += 1;
        drawUsageRow("Flash:", flashUsed, flashTotal);
    }
#endif
    if (hasSD && sdTotal > 0) {
        line += 1;
        drawUsageRow("SD:", sdUsed, sdTotal);
    }

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    // System Uptime
    if (graphics::isCompactPanel(display)) {
        line += 1;
    } else {
        if (line < 2) {
            line += 1;
        }
        line += 1;
    }

    char appversionstr[35];
    char appversionstr_formatted[40];

    const char *ver = optstr(APP_VERSION);
    char verbuf[32];
    strncpy(verbuf, ver, sizeof(verbuf) - 1);
    verbuf[sizeof(verbuf) - 1] = '\0';

    char *lastDot = strrchr(verbuf, '.');

    if (currentResolution == ScreenResolution::UltraLow) {
        if (lastDot != nullptr) {
            *lastDot = '\0';
        }
        snprintf(appversionstr, sizeof(appversionstr), "Ver: %s", verbuf);
    } else {
        if (lastDot) {
            size_t prefixLen = (size_t)(lastDot - verbuf);
            snprintf(appversionstr_formatted, sizeof(appversionstr_formatted), "Ver: %.*s", (int)prefixLen, verbuf);
            strncat(appversionstr_formatted, " (", sizeof(appversionstr_formatted) - strlen(appversionstr_formatted) - 1);
            strncat(appversionstr_formatted, lastDot + 1, sizeof(appversionstr_formatted) - strlen(appversionstr_formatted) - 1);
            strncat(appversionstr_formatted, ")", sizeof(appversionstr_formatted) - strlen(appversionstr_formatted) - 1);
            strncpy(appversionstr, appversionstr_formatted, sizeof(appversionstr) - 1);
            appversionstr[sizeof(appversionstr) - 1] = '\0';
        } else {
            snprintf(appversionstr, sizeof(appversionstr), "Ver: %s", verbuf);
        }
    }
    int textWidth = display->getStringWidth(appversionstr);
    int nameX = (SCREEN_WIDTH - textWidth) / 2;

    display->drawString(nameX, getTextPositions(display)[line++], appversionstr);

    if (!graphics::isCompactPanel(display) &&
        (SCREEN_HEIGHT > 64 || (SCREEN_HEIGHT <= 64 && line <= 5))) { // Only show uptime if the screen can show it
        char uptimeStr[32] = "";
        getUptimeStr(millis(), "Up: ", uptimeStr, sizeof(uptimeStr));
        textWidth = display->getStringWidth(uptimeStr);
        nameX = (SCREEN_WIDTH - textWidth) / 2;
        display->drawString(nameX, getTextPositions(display)[line++], uptimeStr);
    }

    if (SCREEN_HEIGHT > 64 || (SCREEN_HEIGHT <= 64 && line <= 5)) { // Only show API state if the screen can show it
        char api_state[32] = "";
#if defined(OLED_COMPACT_UI)
        const char *connection = "None";
        if (service->api_state == service->STATE_BLE) {
            connection = "BLE";
        } else if (service->api_state == service->STATE_WIFI) {
            connection = "WiFi";
        } else if (service->api_state == service->STATE_SERIAL) {
            connection = "USB";
        } else if (service->api_state == service->STATE_PACKET) {
            connection = "Local";
        } else if (service->api_state == service->STATE_HTTP) {
            connection = "HTTP";
        } else if (service->api_state == service->STATE_ETH) {
            connection = "Eth";
        }
        snprintf(api_state, sizeof(api_state), "App: %s", connection);
#else
        const char *clientWord = nullptr;

        // Determine if narrow or wide screen
        if (currentResolution == ScreenResolution::High) {
            clientWord = "Client";
        } else {
            clientWord = "App";
        }
        snprintf(api_state, sizeof(api_state), "No %ss Connected", clientWord);

        if (service->api_state == service->STATE_BLE) {
            snprintf(api_state, sizeof(api_state), "%s Connected (BLE)", clientWord);
        } else if (service->api_state == service->STATE_WIFI) {
            snprintf(api_state, sizeof(api_state), "%s Connected (WiFi)", clientWord);
        } else if (service->api_state == service->STATE_SERIAL) {
            snprintf(api_state, sizeof(api_state), "%s Connected (Serial)", clientWord);
        } else if (service->api_state == service->STATE_PACKET) {
            snprintf(api_state, sizeof(api_state), "%s Connected (Internal)", clientWord);
        } else if (service->api_state == service->STATE_HTTP) {
            snprintf(api_state, sizeof(api_state), "%s Connected (HTTP)", clientWord);
        } else if (service->api_state == service->STATE_ETH) {
            snprintf(api_state, sizeof(api_state), "%s Connected (Ethernet)", clientWord);
        }
#endif
        if (api_state[0] != '\0') {
            display->drawString((SCREEN_WIDTH - display->getStringWidth(api_state)) / 2, getTextPositions(display)[line++],
                                api_state);
        }
    }

    graphics::drawCommonFooter(display, x, y);
}

// ****************************
// * Chirpy Screen      *
// ****************************
void drawChirpy(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    int line = 1;
    int scale = 1;
    int iconX = SCREEN_WIDTH - chirpy_width - (chirpy_width / 3);
    int iconY = (SCREEN_HEIGHT - chirpy_height) / 2;
    int textX_offset = 10;
    if (currentResolution == ScreenResolution::High) {
        textX_offset = textX_offset * 4;
        scale = 2;
        iconX = SCREEN_WIDTH - (chirpy_width * 2) - ((chirpy_width * 2) / 3);
        iconY = (SCREEN_HEIGHT - (chirpy_height * 2)) / 2;
        const int bytesPerRow = (chirpy_width + 7) / 8;

        for (int yy = 0; yy < chirpy_height; ++yy) {
            const uint8_t *rowPtr = chirpy + yy * bytesPerRow;
            for (int xx = 0; xx < chirpy_width; ++xx) {
                const uint8_t byteVal = pgm_read_byte(rowPtr + (xx >> 3));
                const uint8_t bitMask = 1U << (xx & 7); // XBM is LSB-first
                if (byteVal & bitMask) {
                    display->fillRect(iconX + xx * scale, iconY + yy * scale, scale, scale);
                }
            }
        }
    } else {
        display->drawXbm(iconX, iconY, chirpy_width, chirpy_height, chirpy);
    }

#if GRAPHICS_TFT_COLORING_ENABLED
    // Colour Chirpy on colour displays. The glyph is a filled head silhouette whose eyes are holes
    // (off pixels), so two stacked regions render the proper mascot without a second bitmap:
    //   A) whole glyph -> green body / frame / legs
    //   B) the eye band -> black face, with the eye holes turning white via the region's off-colour
    // Start the face band one column in from the head's left edge (col 6) so that edge stays green,
    // matching the green column already left on the right edge (col 31).
    graphics::registerTFTColorRegionDirect(iconX, iconY, chirpy_width * scale, chirpy_height * scale,
                                           graphics::TFTPalette::MeshtasticGreen, graphics::getThemeBodyBg());
    graphics::registerTFTColorRegionDirect(iconX + 7 * scale, iconY + 12 * scale, 24 * scale, 16 * scale,
                                           graphics::TFTPalette::Black, graphics::TFTPalette::White);
#endif

    int textX = (display->getWidth() / 2) - textX_offset - (display->getStringWidth("Hello") / 2);
    display->drawString(textX, getTextPositions(display)[line++], "Hello");
    textX = (display->getWidth() / 2) - textX_offset - (display->getStringWidth("World!") / 2);
    display->drawString(textX, getTextPositions(display)[line++], "World!");
}

} // namespace DebugRenderer
} // namespace graphics
#endif
