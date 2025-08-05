#include "configuration.h"
#if HAS_SCREEN
#include "../Screen.h"
#include "DebugRenderer.h"
#include "FSCommon.h"
#include "NodeDB.h"
#include "Throttle.h"
#include "UIRenderer.h"
#include "airtime.h"
#include "gps/RTC.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include "main.h"
#include "mesh/Channels.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "sleep.h"

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
#include "mesh/wifi/WiFiAPClient.h"
#include <WiFi.h>
#ifdef ARCH_ESP32
#include "mesh/wifi/WiFiAPClient.h"
#endif
#endif

#ifdef ARCH_ESP32
#include "modules/StoreForwardModule.h"
#endif
#include <DisplayFormatters.h>
#include <RadioLibInterface.h>
#include <target_specific.h>

using namespace meshtastic;

// External variables
extern graphics::Screen *screen;
extern PowerStatus *powerStatus;
extern NodeStatus *nodeStatus;
extern GPSStatus *gpsStatus;
extern Channels channels;
extern AirTime *airTime;

// External functions from Screen.cpp
extern bool heartbeat;

#ifdef ARCH_ESP32
extern StoreForwardModule *storeForwardModule;
#endif

namespace graphics
{
namespace DebugRenderer
{

void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->fillRect(0 + x, 0 + y, x + display->getWidth(), y + FONT_HEIGHT_SMALL);
        display->setColor(BLACK);
    }

    char channelStr[20];
    snprintf(channelStr, sizeof(channelStr), "#%s", channels.getName(channels.getPrimaryIndex()));
    // Display nodes status
    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT) {
        UIRenderer::drawNodes(display, x + (SCREEN_WIDTH * 0.25), y + 2, nodeStatus);
    } else {
        UIRenderer::drawNodes(display, x + (SCREEN_WIDTH * 0.25), y + 3, nodeStatus);
    }
#if HAS_GPS
    // Display GPS status
    if (config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        UIRenderer::drawGpsPowerStatus(display, x, y + 2, gpsStatus);
    } else {
        if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT) {
            UIRenderer::drawGps(display, x + (SCREEN_WIDTH * 0.63), y + 2, gpsStatus);
        } else {
            UIRenderer::drawGps(display, x + (SCREEN_WIDTH * 0.63), y + 3, gpsStatus);
        }
    }
#endif
    display->setColor(WHITE);
    // Draw the channel name
    display->drawString(x, y + FONT_HEIGHT_SMALL, channelStr);
    // Draw our hardware ID to assist with bluetooth pairing. Either prefix with Info or S&F Logo
    if (moduleConfig.store_forward.enabled) {
#ifdef ARCH_ESP32
        if (!Throttle::isWithinTimespanMs(storeForwardModule->lastHeartbeat,
                                          (storeForwardModule->heartbeatInterval * 1200))) { // no heartbeat, overlap a bit
#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) ||      \
     defined(ST7789_CS) || defined(USE_ST7789) || defined(ILI9488_CS) || defined(HX8357_CS) || ARCH_PORTDUINO) &&                \
    !defined(DISPLAY_FORCE_SMALL_FONTS)
            display->drawFastImage(x + SCREEN_WIDTH - 14 - display->getStringWidth(screen->ourId), y + 3 + FONT_HEIGHT_SMALL, 12,
                                   8, imgQuestionL1);
            display->drawFastImage(x + SCREEN_WIDTH - 14 - display->getStringWidth(screen->ourId), y + 11 + FONT_HEIGHT_SMALL, 12,
                                   8, imgQuestionL2);
#else
            display->drawFastImage(x + SCREEN_WIDTH - 10 - display->getStringWidth(screen->ourId), y + 2 + FONT_HEIGHT_SMALL, 8,
                                   8, imgQuestion);
#endif
        } else {
#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) ||      \
     defined(ST7789_CS) || defined(USE_ST7789) || defined(ILI9488_CS) || defined(HX8357_CS)) &&                                  \
    !defined(DISPLAY_FORCE_SMALL_FONTS)
            display->drawFastImage(x + SCREEN_WIDTH - 18 - display->getStringWidth(screen->ourId), y + 3 + FONT_HEIGHT_SMALL, 16,
                                   8, imgSFL1);
            display->drawFastImage(x + SCREEN_WIDTH - 18 - display->getStringWidth(screen->ourId), y + 11 + FONT_HEIGHT_SMALL, 16,
                                   8, imgSFL2);
#else
            display->drawFastImage(x + SCREEN_WIDTH - 13 - display->getStringWidth(screen->ourId), y + 2 + FONT_HEIGHT_SMALL, 11,
                                   8, imgSF);
#endif
        }
#endif
    } else {
        // TODO: Raspberry Pi supports more than just the one screen size
#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) ||      \
     defined(ST7789_CS) || defined(USE_ST7789) || defined(ILI9488_CS) || defined(HX8357_CS) || ARCH_PORTDUINO) &&                \
    !defined(DISPLAY_FORCE_SMALL_FONTS)
        display->drawFastImage(x + SCREEN_WIDTH - 14 - display->getStringWidth(screen->ourId), y + 3 + FONT_HEIGHT_SMALL, 12, 8,
                               imgInfoL1);
        display->drawFastImage(x + SCREEN_WIDTH - 14 - display->getStringWidth(screen->ourId), y + 11 + FONT_HEIGHT_SMALL, 12, 8,
                               imgInfoL2);
#else
        display->drawFastImage(x + SCREEN_WIDTH - 10 - display->getStringWidth(screen->ourId), y + 2 + FONT_HEIGHT_SMALL, 8, 8,
                               imgInfo);
#endif
    }

    display->drawString(x + SCREEN_WIDTH - display->getStringWidth(screen->ourId), y + FONT_HEIGHT_SMALL, screen->ourId);

    // Draw any log messages
    display->drawLogBuffer(x, y + (FONT_HEIGHT_SMALL * 2));

    /* Display a heartbeat pixel that blinks every time the frame is redrawn */
#ifdef SHOW_REDRAWS
    if (heartbeat)
        display->setPixel(0, 0);
    heartbeat = !heartbeat;
#endif
}

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

    /* Display a heartbeat pixel that blinks every time the frame is redrawn */
#ifdef SHOW_REDRAWS
    if (heartbeat)
        display->setPixel(0, 0);
    heartbeat = !heartbeat;
#endif
#endif
}

void drawFrameSettings(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->fillRect(0 + x, 0 + y, x + display->getWidth(), y + FONT_HEIGHT_SMALL);
        display->setColor(BLACK);
    }

    char batStr[20];
    if (powerStatus->getHasBattery()) {
        int batV = powerStatus->getBatteryVoltageMv() / 1000;
        int batCv = (powerStatus->getBatteryVoltageMv() % 1000) / 10;

        snprintf(batStr, sizeof(batStr), "B %01d.%02dV %3d%% %c%c", batV, batCv, powerStatus->getBatteryChargePercent(),
                 powerStatus->getIsCharging() ? '+' : ' ', powerStatus->getHasUSB() ? 'U' : ' ');

        // Line 1
        display->drawString(x, y, batStr);
        if (config.display.heading_bold)
            display->drawString(x + 1, y, batStr);
    } else {
        // Line 1
        display->drawString(x, y, "USB");
        if (config.display.heading_bold)
            display->drawString(x + 1, y, "USB");
    }

    //    auto mode = DisplayFormatters::getModemPresetDisplayName(config.lora.modem_preset, true);

    //    display->drawString(x + SCREEN_WIDTH - display->getStringWidth(mode), y, mode);
    //    if (config.display.heading_bold)
    //        display->drawString(x + SCREEN_WIDTH - display->getStringWidth(mode) - 1, y, mode);

    uint32_t currentMillis = millis();
    uint32_t seconds = currentMillis / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    uint32_t days = hours / 24;
    // currentMillis %= 1000;
    // seconds %= 60;
    // minutes %= 60;
    // hours %= 24;

    // Show uptime as days, hours, minutes OR seconds
    std::string uptime = UIRenderer::drawTimeDelta(days, hours, minutes, seconds);

    // Line 1 (Still)
    display->drawString(x + SCREEN_WIDTH - display->getStringWidth(uptime.c_str()), y, uptime.c_str());
    if (config.display.heading_bold)
        display->drawString(x - 1 + SCREEN_WIDTH - display->getStringWidth(uptime.c_str()), y, uptime.c_str());

    display->setColor(WHITE);

    // Setup string to assemble analogClock string
    std::string analogClock = "";

    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true); // Display local timezone
    if (rtc_sec > 0) {
        long hms = rtc_sec % SEC_PER_DAY;
        // hms += tz.tz_dsttime * SEC_PER_HOUR;
        // hms -= tz.tz_minuteswest * SEC_PER_MIN;
        // mod `hms` to ensure in positive range of [0...SEC_PER_DAY)
        hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

        // Tear apart hms into h:m:s
        int hour = hms / SEC_PER_HOUR;
        int min = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
        int sec = (hms % SEC_PER_HOUR) % SEC_PER_MIN; // or hms % SEC_PER_MIN

        char timebuf[12];

        if (config.display.use_12h_clock) {
            std::string meridiem = "am";
            if (hour >= 12) {
                if (hour > 12)
                    hour -= 12;
                meridiem = "pm";
            }
            if (hour == 00) {
                hour = 12;
            }
            snprintf(timebuf, sizeof(timebuf), "%d:%02d:%02d%s", hour, min, sec, meridiem.c_str());
        } else {
            snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d", hour, min, sec);
        }
        analogClock += timebuf;
    }

    // Line 2
    display->drawString(x, y + FONT_HEIGHT_SMALL * 1, analogClock.c_str());

    // Display Channel Utilization
    char chUtil[13];
    snprintf(chUtil, sizeof(chUtil), "ChUtil %2.0f%%", airTime->channelUtilizationPercent());
    display->drawString(x + SCREEN_WIDTH - display->getStringWidth(chUtil), y + FONT_HEIGHT_SMALL * 1, chUtil);

#if HAS_GPS
    if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        // Line 3
        if (config.display.gps_format !=
            meshtastic_Config_DisplayConfig_GpsCoordinateFormat_DMS) // if DMS then don't draw altitude
            UIRenderer::drawGpsAltitude(display, x, y + FONT_HEIGHT_SMALL * 2, gpsStatus);

        // Line 4
        UIRenderer::drawGpsCoordinates(display, x, y + FONT_HEIGHT_SMALL * 3, gpsStatus);
    } else {
        UIRenderer::drawGpsPowerStatus(display, x, y + FONT_HEIGHT_SMALL * 2, gpsStatus);
    }
#endif
/* Display a heartbeat pixel that blinks every time the frame is redrawn */
#ifdef SHOW_REDRAWS
    if (heartbeat)
        display->setPixel(0, 0);
    heartbeat = !heartbeat;
#endif
}

// Trampoline functions for DebugInfo class access
void drawDebugInfoTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    drawFrame(display, state, x, y);
}

void drawDebugInfoSettingsTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    drawFrameSettings(display, state, x, y);
}

void drawDebugInfoWiFiTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    drawFrameWiFi(display, state, x, y);
}

// ****************************
// * LoRa Focused Screen      *
// ****************************
void drawLoRaFocused(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    int line = 1;

    // === Set Title
    const char *titleStr = (isHighResolution) ? "LoRa Info" : "LoRa";

    // === Header ===
    graphics::drawCommonHeader(display, x, y, titleStr);

    // === First Row: Region / BLE Name ===
    graphics::UIRenderer::drawNodes(display, x, getTextPositions(display)[line] + 2, nodeStatus, 0, true, "");

    uint8_t dmac[6];
    char shortnameble[35];
    getMacAddr(dmac);
    snprintf(screen->ourId, sizeof(screen->ourId), "%02x%02x", dmac[4], dmac[5]);
    snprintf(shortnameble, sizeof(shortnameble), "BLE: %s", screen->ourId);
    int textWidth = display->getStringWidth(shortnameble);
    int nameX = (SCREEN_WIDTH - textWidth);
    display->drawString(nameX, getTextPositions(display)[line++], shortnameble);

    // === Second Row: Radio Preset ===
    auto mode = DisplayFormatters::getModemPresetDisplayName(config.lora.modem_preset, false);
    char regionradiopreset[25];
    const char *region = myRegion ? myRegion->name : NULL;
    if (region != nullptr) {
        snprintf(regionradiopreset, sizeof(regionradiopreset), "%s/%s", region, mode);
    }
    textWidth = display->getStringWidth(regionradiopreset);
    nameX = (SCREEN_WIDTH - textWidth) / 2;
    display->drawString(nameX, getTextPositions(display)[line++], regionradiopreset);

    // === Third Row: Frequency / ChanNum ===
    char frequencyslot[35];
    char freqStr[16];
    float freq = RadioLibInterface::instance->getFreq();
    snprintf(freqStr, sizeof(freqStr), "%.3f", freq);
    if (config.lora.channel_num == 0) {
        snprintf(frequencyslot, sizeof(frequencyslot), "Freq: %sMHz", freqStr);
    } else {
        snprintf(frequencyslot, sizeof(frequencyslot), "Freq/Ch: %sMHz (%d)", freqStr, config.lora.channel_num);
    }
    size_t len = strlen(frequencyslot);
    if (len >= 4 && strcmp(frequencyslot + len - 4, " (0)") == 0) {
        frequencyslot[len - 4] = '\0'; // Remove the last three characters
    }
    textWidth = display->getStringWidth(frequencyslot);
    nameX = (SCREEN_WIDTH - textWidth) / 2;
    display->drawString(nameX, getTextPositions(display)[line++], frequencyslot);

    // === Fourth Row: Channel Utilization ===
    const char *chUtil = "ChUtil:";
    char chUtilPercentage[10];
    snprintf(chUtilPercentage, sizeof(chUtilPercentage), "%2.0f%%", airTime->channelUtilizationPercent());

    int chUtil_x = (isHighResolution) ? display->getStringWidth(chUtil) + 10 : display->getStringWidth(chUtil) + 5;
    int chUtil_y = getTextPositions(display)[line] + 3;

    int chutil_bar_width = (isHighResolution) ? 100 : 50;
    int chutil_bar_height = (isHighResolution) ? 12 : 7;
    int extraoffset = (isHighResolution) ? 6 : 3;
    int chutil_percent = airTime->channelUtilizationPercent();

    int centerofscreen = SCREEN_WIDTH / 2;
    int total_line_content_width = (chUtil_x + chutil_bar_width + display->getStringWidth(chUtilPercentage) + extraoffset) / 2;
    int starting_position = centerofscreen - total_line_content_width;

    display->drawString(starting_position, getTextPositions(display)[line++], chUtil);

    // Force 56% or higher to show a full 100% bar, text would still show related percent.
    if (chutil_percent >= 61) {
        chutil_percent = 100;
    }

    // Weighting for nonlinear segments
    float milestone1 = 25;
    float milestone2 = 40;
    float weight1 = 0.45; // Weight for 0–25%
    float weight2 = 0.35; // Weight for 25–40%
    float weight3 = 0.20; // Weight for 40–100%
    float totalWeight = weight1 + weight2 + weight3;

    int seg1 = chutil_bar_width * (weight1 / totalWeight);
    int seg2 = chutil_bar_width * (weight2 / totalWeight);
    int seg3 = chutil_bar_width * (weight3 / totalWeight);

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
        display->fillRect(starting_position + chUtil_x, chUtil_y, fillRight, chutil_bar_height);
    }

    display->drawString(starting_position + chUtil_x + chutil_bar_width + extraoffset, getTextPositions(display)[4],
                        chUtilPercentage);
}

// ****************************
// *      System Screen       *
// ****************************
void drawMemoryUsage(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
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
    int barsOffset = (isHighResolution) ? 24 : 0;
#ifdef USE_EINK
    barsOffset -= 12;
#endif
    const int barX = x + 40 + barsOffset;

    auto drawUsageRow = [&](const char *label, uint32_t used, uint32_t total, bool isHeap = false) {
        if (total == 0)
            return;

        int percent = (used * 100) / total;

        char combinedStr[24];
        if (isHighResolution) {
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

        // Bar
        int barY = getTextPositions(display)[line] + (FONT_HEIGHT_SMALL - barHeight) / 2;
        display->setColor(WHITE);
        display->drawRect(barX, barY, adjustedBarWidth, barHeight);

        display->fillRect(barX, barY, fillWidth, barHeight);
        display->setColor(WHITE);

        // Value string
        display->setTextAlignment(TEXT_ALIGN_RIGHT);
        display->drawString(SCREEN_WIDTH - 2, getTextPositions(display)[line], combinedStr);
    };

    // === Memory values ===
    uint32_t heapUsed = memGet.getHeapSize() - memGet.getFreeHeap();
    uint32_t heapTotal = memGet.getHeapSize();

    uint32_t psramUsed = memGet.getPsramSize() - memGet.getFreePsram();
    uint32_t psramTotal = memGet.getPsramSize();

    uint32_t flashUsed = 0, flashTotal = 0;
#ifdef ESP32
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
    if (psramUsed > 0) {
        line += 1;
        drawUsageRow("PSRAM:", psramUsed, psramTotal);
    }
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
    if (line < 2) {
        line += 1;
    }
    line += 1;
    char appversionstr[35];
    snprintf(appversionstr, sizeof(appversionstr), "Ver: %s", optstr(APP_VERSION));
    char appversionstr_formatted[40];
    char *lastDot = strrchr(appversionstr, '.');
    if (lastDot) {
        size_t prefixLen = lastDot - appversionstr;
        strncpy(appversionstr_formatted, appversionstr, prefixLen);
        appversionstr_formatted[prefixLen] = '\0';
        strncat(appversionstr_formatted, " (", sizeof(appversionstr_formatted) - strlen(appversionstr_formatted) - 1);
        strncat(appversionstr_formatted, lastDot + 1, sizeof(appversionstr_formatted) - strlen(appversionstr_formatted) - 1);
        strncat(appversionstr_formatted, ")", sizeof(appversionstr_formatted) - strlen(appversionstr_formatted) - 1);
        strncpy(appversionstr, appversionstr_formatted, sizeof(appversionstr) - 1);
        appversionstr[sizeof(appversionstr) - 1] = '\0';
    }
    int textWidth = display->getStringWidth(appversionstr);
    int nameX = (SCREEN_WIDTH - textWidth) / 2;
    display->drawString(nameX, getTextPositions(display)[line], appversionstr);

    if (SCREEN_HEIGHT > 64 || (SCREEN_HEIGHT <= 64 && line < 4)) { // Only show uptime if the screen can show it
        line += 1;
        char uptimeStr[32] = "";
        uint32_t uptime = millis() / 1000;
        uint32_t days = uptime / 86400;
        uint32_t hours = (uptime % 86400) / 3600;
        uint32_t mins = (uptime % 3600) / 60;
        // Show as "Up: 2d 3h", "Up: 5h 14m", or "Up: 37m"
        if (days)
            snprintf(uptimeStr, sizeof(uptimeStr), " Up: %ud %uh", days, hours);
        else if (hours)
            snprintf(uptimeStr, sizeof(uptimeStr), " Up: %uh %um", hours, mins);
        else
            snprintf(uptimeStr, sizeof(uptimeStr), " Uptime: %um", mins);
        textWidth = display->getStringWidth(uptimeStr);
        nameX = (SCREEN_WIDTH - textWidth) / 2;
        display->drawString(nameX, getTextPositions(display)[line], uptimeStr);
    }
}
} // namespace DebugRenderer
} // namespace graphics
#endif