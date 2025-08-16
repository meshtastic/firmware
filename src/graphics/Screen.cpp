/*
BaseUI

Developed and Maintained By:
- Ronald Garcia (HarukiToreda) – Lead development and implementation.
- JasonP (Xaositek)  – Screen layout and icon design, UI improvements and testing.
- TonyG (Tropho) – Project management, structural planning, and testing

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "Screen.h"
#include "NodeDB.h"
#include "PowerMon.h"
#include "Throttle.h"
#include "configuration.h"
#if HAS_SCREEN
#include <OLEDDisplay.h>

#include "DisplayFormatters.h"
#include "TimeFormatters.h"
#include "draw/ClockRenderer.h"
#include "draw/DebugRenderer.h"
#include "draw/MenuHandler.h"
#include "draw/MessageRenderer.h"
#include "draw/NodeListRenderer.h"
#include "draw/NotificationRenderer.h"
#include "draw/UIRenderer.h"
#include "modules/CannedMessageModule.h"

#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#include "buzz.h"
#endif
#include "FSCommon.h"
#include "MeshService.h"
#include "RadioLibInterface.h"
#include "error.h"
#include "gps/GeoCoord.h"
#include "gps/RTC.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/emotes.h"
#include "graphics/images.h"
#include "input/TouchScreenImpl1.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "mesh/Channels.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "meshUtils.h"
#include "modules/ExternalNotificationModule.h"
#include "modules/TextMessageModule.h"
#include "modules/WaypointModule.h"
#include "sleep.h"
#include "target_specific.h"

using graphics::Emote;
using graphics::emotes;
using graphics::numEmotes;

extern uint16_t TFT_MESH;

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
#include "mesh/wifi/WiFiAPClient.h"
#endif

#ifdef ARCH_ESP32
#endif

#if ARCH_PORTDUINO
#include "modules/StoreForwardModule.h"
#include "platform/portduino/PortduinoGlue.h"
#endif

using namespace meshtastic; /** @todo remove */

namespace graphics
{

// This means the *visible* area (sh1106 can address 132, but shows 128 for example)
#define IDLE_FRAMERATE 1 // in fps

// DEBUG
#define NUM_EXTRA_FRAMES 3 // text message and debug frame
// if defined a pixel will blink to show redraws
// #define SHOW_REDRAWS

// A text message frame + debug frame + all the node infos
FrameCallback *normalFrames;
static uint32_t targetFramerate = IDLE_FRAMERATE;
// Global variables for alert banner - explicitly define with extern "C" linkage to prevent optimization

uint32_t logo_timeout = 5000; // 4 seconds for EACH logo

// Threshold values for the GPS lock accuracy bar display
uint32_t dopThresholds[5] = {2000, 1000, 500, 200, 100};

// At some point, we're going to ask all of the modules if they would like to display a screen frame
// we'll need to hold onto pointers for the modules that can draw a frame.
std::vector<MeshModule *> moduleFrames;

// Global variables for screen function overlay symbols
std::vector<std::string> functionSymbol;
std::string functionSymbolString;

#if HAS_GPS
// GeoCoord object for the screen
GeoCoord geoCoord;
#endif

#ifdef SHOW_REDRAWS
static bool heartbeat = false;
#endif

#include "graphics/ScreenFonts.h"
#include <Throttle.h>

// Usage: int stringWidth = formatDateTime(datetimeStr, sizeof(datetimeStr), rtc_sec, display);
// End Functions to write date/time to the screen

extern bool hasUnreadMessage;

// ==============================
// Overlay Alert Banner Renderer
// ==============================
// Displays a temporary centered banner message (e.g., warning, status, etc.)
// The banner appears in the center of the screen and disappears after the specified duration

void Screen::showSimpleBanner(const char *message, uint32_t durationMs)
{
    BannerOverlayOptions options;
    options.message = message;
    options.durationMs = durationMs;
    options.notificationType = notificationTypeEnum::text_banner;
    showOverlayBanner(options);
}

// Called to trigger a banner with custom message and duration
void Screen::showOverlayBanner(BannerOverlayOptions banner_overlay_options)
{
#ifdef USE_EINK
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Skip full refresh for all overlay menus
#endif
    // Store the message and set the expiration timestamp
    strncpy(NotificationRenderer::alertBannerMessage, banner_overlay_options.message, 255);
    NotificationRenderer::alertBannerMessage[255] = '\0'; // Ensure null termination
    NotificationRenderer::alertBannerUntil =
        (banner_overlay_options.durationMs == 0) ? 0 : millis() + banner_overlay_options.durationMs;
    NotificationRenderer::optionsArrayPtr = banner_overlay_options.optionsArrayPtr;
    NotificationRenderer::optionsEnumPtr = banner_overlay_options.optionsEnumPtr;
    NotificationRenderer::alertBannerOptions = banner_overlay_options.optionsCount;
    NotificationRenderer::alertBannerCallback = banner_overlay_options.bannerCallback;
    NotificationRenderer::curSelected = banner_overlay_options.InitialSelected;
    NotificationRenderer::pauseBanner = false;
    NotificationRenderer::current_notification_type = notificationTypeEnum::selection_picker;
    static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
    ui->setTargetFPS(60);
    ui->update();
}

// Called to trigger a banner with custom message and duration
void Screen::showNodePicker(const char *message, uint32_t durationMs, std::function<void(uint32_t)> bannerCallback)
{
#ifdef USE_EINK
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Skip full refresh for all overlay menus
#endif
    nodeDB->pause_sort(true);
    // Store the message and set the expiration timestamp
    strncpy(NotificationRenderer::alertBannerMessage, message, 255);
    NotificationRenderer::alertBannerMessage[255] = '\0'; // Ensure null termination
    NotificationRenderer::alertBannerUntil = (durationMs == 0) ? 0 : millis() + durationMs;
    NotificationRenderer::alertBannerCallback = bannerCallback;
    NotificationRenderer::pauseBanner = false;
    NotificationRenderer::curSelected = 0;
    NotificationRenderer::current_notification_type = notificationTypeEnum::node_picker;

    static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
    ui->setTargetFPS(60);
    ui->update();
}

// Called to trigger a banner with custom message and duration
void Screen::showNumberPicker(const char *message, uint32_t durationMs, uint8_t digits,
                              std::function<void(uint32_t)> bannerCallback)
{
#ifdef USE_EINK
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Skip full refresh for all overlay menus
#endif
    // Store the message and set the expiration timestamp
    strncpy(NotificationRenderer::alertBannerMessage, message, 255);
    NotificationRenderer::alertBannerMessage[255] = '\0'; // Ensure null termination
    NotificationRenderer::alertBannerUntil = (durationMs == 0) ? 0 : millis() + durationMs;
    NotificationRenderer::alertBannerCallback = bannerCallback;
    NotificationRenderer::pauseBanner = false;
    NotificationRenderer::curSelected = 0;
    NotificationRenderer::current_notification_type = notificationTypeEnum::number_picker;
    NotificationRenderer::numDigits = digits;
    NotificationRenderer::currentNumber = 0;

    static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
    ui->setTargetFPS(60);
    ui->update();
}

static void drawModuleFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    uint8_t module_frame;
    // there's a little but in the UI transition code
    // where it invokes the function at the correct offset
    // in the array of "drawScreen" functions; however,
    // the passed-state doesn't quite reflect the "current"
    // screen, so we have to detect it.
    if (state->frameState == IN_TRANSITION && state->transitionFrameRelationship == TransitionRelationship_INCOMING) {
        // if we're transitioning from the end of the frame list back around to the first
        // frame, then we want this to be `0`
        module_frame = state->transitionFrameTarget;
    } else {
        // otherwise, just display the module frame that's aligned with the current frame
        module_frame = state->currentFrame;
        // LOG_DEBUG("Screen is not in transition.  Frame: %d", module_frame);
    }
    // LOG_DEBUG("Draw Module Frame %d", module_frame);
    MeshModule &pi = *moduleFrames.at(module_frame);
    pi.drawFrame(display, state, x, y);
}

// Ignore messages originating from phone (from the current node 0x0) unless range test or store and forward module are enabled
static bool shouldDrawMessage(const meshtastic_MeshPacket *packet)
{
    return packet->from != 0 && !moduleConfig.store_forward.enabled;
}

/**
 * Given a recent lat/lon return a guess of the heading the user is walking on.
 *
 * We keep a series of "after you've gone 10 meters, what is your heading since
 * the last reference point?"
 */
float Screen::estimatedHeading(double lat, double lon)
{
    static double oldLat, oldLon;
    static float b;

    if (oldLat == 0) {
        // just prepare for next time
        oldLat = lat;
        oldLon = lon;

        return b;
    }

    float d = GeoCoord::latLongToMeter(oldLat, oldLon, lat, lon);
    if (d < 10) // haven't moved enough, just keep current bearing
        return b;

    b = GeoCoord::bearing(oldLat, oldLon, lat, lon) * RAD_TO_DEG;
    oldLat = lat;
    oldLon = lon;

    return b;
}

/// We will skip one node - the one for us, so we just blindly loop over all
/// nodes
static int8_t prevFrame = -1;

// Combined dynamic node list frame cycling through LastHeard, HopSignal, and Distance modes
// Uses a single frame and changes data every few seconds (E-Ink variant is separate)

#if defined(ESP_PLATFORM) && defined(USE_ST7789)
SPIClass SPI1(HSPI);
#endif

Screen::Screen(ScanI2C::DeviceAddress address, meshtastic_Config_DisplayConfig_OledType screenType, OLEDDISPLAY_GEOMETRY geometry)
    : concurrency::OSThread("Screen"), address_found(address), model(screenType), geometry(geometry), cmdQueue(32)
{
    graphics::normalFrames = new FrameCallback[MAX_NUM_NODES + NUM_EXTRA_FRAMES];

    LOG_INFO("Protobuf Value uiconfig.screen_rgb_color: %d", uiconfig.screen_rgb_color);
    int32_t rawRGB = uiconfig.screen_rgb_color;
    if (rawRGB > 0 && rawRGB <= 255255255) {
        uint8_t TFT_MESH_r = (rawRGB >> 16) & 0xFF;
        uint8_t TFT_MESH_g = (rawRGB >> 8) & 0xFF;
        uint8_t TFT_MESH_b = rawRGB & 0xFF;
        LOG_INFO("Values of r,g,b: %d, %d, %d", TFT_MESH_r, TFT_MESH_g, TFT_MESH_b);

        if (TFT_MESH_r <= 255 && TFT_MESH_g <= 255 && TFT_MESH_b <= 255) {
            TFT_MESH = COLOR565(TFT_MESH_r, TFT_MESH_g, TFT_MESH_b);
        }
    }

#if defined(USE_SH1106) || defined(USE_SH1107) || defined(USE_SH1107_128_64)
    dispdev = new SH1106Wire(address.address, -1, -1, geometry,
                             (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_ST7789)
#ifdef ESP_PLATFORM
    dispdev = new ST7789Spi(&SPI1, ST7789_RESET, ST7789_RS, ST7789_NSS, GEOMETRY_RAWMODE, TFT_WIDTH, TFT_HEIGHT, ST7789_SDA,
                            ST7789_MISO, ST7789_SCK);
#else
    dispdev = new ST7789Spi(&SPI1, ST7789_RESET, ST7789_RS, ST7789_NSS, GEOMETRY_RAWMODE, TFT_WIDTH, TFT_HEIGHT);
#endif
    static_cast<ST7789Spi *>(dispdev)->setRGB(TFT_MESH);
#elif defined(USE_SSD1306)
    dispdev = new SSD1306Wire(address.address, -1, -1, geometry,
                              (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(ST7735_CS) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7789_CS) ||    \
    defined(RAK14014) || defined(HX8357_CS) || defined(ILI9488_CS)
    dispdev = new TFTDisplay(address.address, -1, -1, geometry,
                             (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_EINK) && !defined(USE_EINK_DYNAMICDISPLAY)
    dispdev = new EInkDisplay(address.address, -1, -1, geometry,
                              (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_EINK) && defined(USE_EINK_DYNAMICDISPLAY)
    dispdev = new EInkDynamicDisplay(address.address, -1, -1, geometry,
                                     (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_ST7567)
    dispdev = new ST7567Wire(address.address, -1, -1, geometry,
                             (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif ARCH_PORTDUINO
    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
        if (settingsMap[displayPanel] != no_screen) {
            LOG_DEBUG("Make TFTDisplay!");
            dispdev = new TFTDisplay(address.address, -1, -1, geometry,
                                     (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
        } else {
            dispdev = new AutoOLEDWire(address.address, -1, -1, geometry,
                                       (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
            isAUTOOled = true;
        }
    }
#else
    dispdev = new AutoOLEDWire(address.address, -1, -1, geometry,
                               (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
    isAUTOOled = true;
#endif

    ui = new OLEDDisplayUi(dispdev);
    cmdQueue.setReader(this);
}

Screen::~Screen()
{
    delete[] graphics::normalFrames;
}

/**
 * Prepare the display for the unit going to the lowest power mode possible.  Most screens will just
 * poweroff, but eink screens will show a "I'm sleeping" graphic, possibly with a QR code
 */
void Screen::doDeepSleep()
{
#ifdef USE_EINK
    setOn(false, graphics::UIRenderer::drawDeepSleepFrame);
#else
    // Without E-Ink display:
    setOn(false);
#endif
}

void Screen::handleSetOn(bool on, FrameCallback einkScreensaver)
{
    if (!useDisplay)
        return;

    if (on != screenOn) {
        if (on) {
            LOG_INFO("Turn on screen");
            powerMon->setState(meshtastic_PowerMon_State_Screen_On);
#ifdef T_WATCH_S3
            PMU->enablePowerOutput(XPOWERS_ALDO2);
#endif

#if !ARCH_PORTDUINO
            dispdev->displayOn();
#endif

#ifdef PIN_EINK_EN
            if (uiconfig.screen_brightness == 1)
                digitalWrite(PIN_EINK_EN, HIGH);
#elif defined(PCA_PIN_EINK_EN)
            if (uiconfig.screen_brightness == 1)
                io.digitalWrite(PCA_PIN_EINK_EN, HIGH);
#endif

#if defined(ST7789_CS) &&                                                                                                        \
    !defined(M5STACK) // set display brightness when turning on screens. Just moved function from TFTDisplay to here.
            static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#endif

            dispdev->displayOn();
#ifdef HELTEC_TRACKER_V1_X
            ui->init();
#endif
#ifdef USE_ST7789
            pinMode(VTFT_CTRL, OUTPUT);
            digitalWrite(VTFT_CTRL, LOW);
            ui->init();
#ifdef ESP_PLATFORM
            analogWrite(VTFT_LEDA, BRIGHTNESS_DEFAULT);
#else
            pinMode(VTFT_LEDA, OUTPUT);
            digitalWrite(VTFT_LEDA, TFT_BACKLIGHT_ON);
#endif
#endif
            enabled = true;
            setInterval(0); // Draw ASAP
            runASAP = true;
        } else {
            powerMon->clearState(meshtastic_PowerMon_State_Screen_On);
#ifdef USE_EINK
            // eInkScreensaver parameter is usually NULL (default argument), default frame used instead
            setScreensaverFrames(einkScreensaver);
#endif

#ifdef PIN_EINK_EN
            digitalWrite(PIN_EINK_EN, LOW);
#elif defined(PCA_PIN_EINK_EN)
            io.digitalWrite(PCA_PIN_EINK_EN, LOW);
#endif

            dispdev->displayOff();
#ifdef USE_ST7789
            SPI1.end();
#if defined(ARCH_ESP32)
            pinMode(VTFT_LEDA, ANALOG);
            pinMode(VTFT_CTRL, ANALOG);
            pinMode(ST7789_RESET, ANALOG);
            pinMode(ST7789_RS, ANALOG);
            pinMode(ST7789_NSS, ANALOG);
#else
            nrf_gpio_cfg_default(VTFT_LEDA);
            nrf_gpio_cfg_default(VTFT_CTRL);
            nrf_gpio_cfg_default(ST7789_RESET);
            nrf_gpio_cfg_default(ST7789_RS);
            nrf_gpio_cfg_default(ST7789_NSS);
#endif
#endif

#ifdef T_WATCH_S3
            PMU->disablePowerOutput(XPOWERS_ALDO2);
#endif
            enabled = false;
        }
        screenOn = on;
    }
}

void Screen::setup()
{

    // === Enable display rendering ===
    useDisplay = true;

    // === Load saved brightness from UI config ===
    // For OLED displays (SSD1306), default brightness is 255 if not set
    if (uiconfig.screen_brightness == 0) {
#if defined(USE_OLED) || defined(USE_SSD1306) || defined(USE_SH1106) || defined(USE_SH1107)
        brightness = 255; // Default for OLED
#else
        brightness = BRIGHTNESS_DEFAULT;
#endif
    } else {
        brightness = uiconfig.screen_brightness;
    }

    // === Detect OLED subtype (if supported by board variant) ===
#ifdef AutoOLEDWire_h
    if (isAUTOOled)
        static_cast<AutoOLEDWire *>(dispdev)->setDetected(model);
#endif

#ifdef USE_SH1107_128_64
    static_cast<SH1106Wire *>(dispdev)->setSubtype(7);
#endif

#if defined(USE_ST7789) && defined(TFT_MESH)
    // Apply custom RGB color (e.g. Heltec T114/T190)
    static_cast<ST7789Spi *>(dispdev)->setRGB(TFT_MESH);
#endif

    // === Initialize display and UI system ===
    ui->init();
    displayWidth = dispdev->width();
    displayHeight = dispdev->height();

    ui->setTimePerTransition(0);           // Disable animation delays
    ui->setIndicatorPosition(BOTTOM);      // Not used (indicators disabled below)
    ui->setIndicatorDirection(LEFT_RIGHT); // Not used (indicators disabled below)
    ui->setFrameAnimation(SLIDE_LEFT);     // Used only when indicators are active
    ui->disableAllIndicators();            // Disable page indicator dots
    ui->getUiState()->userData = this;     // Allow static callbacks to access Screen instance

    // === Apply loaded brightness ===
#if defined(ST7789_CS)
    static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#elif defined(USE_OLED) || defined(USE_SSD1306) || defined(USE_SH1106) || defined(USE_SH1107)
    dispdev->setBrightness(brightness);
#endif
    LOG_INFO("Applied screen brightness: %d", brightness);

    // === Set custom overlay callbacks ===
    static OverlayCallback overlays[] = {
        graphics::UIRenderer::drawNavigationBar // Custom indicator icons for each frame
    };
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));

    // === Enable UTF-8 to display mapping ===
    dispdev->setFontTableLookupFunction(customFontTableLookup);

#ifdef USERPREFS_OEM_TEXT
    logo_timeout *= 2; // Give more time for branded boot logos
#endif

    // === Configure alert frames (e.g., "Resuming..." or region name) ===
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Skip slow refresh
    alertFrames[0] = [this](OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
#ifdef ARCH_ESP32
        if (wakeCause == ESP_SLEEP_WAKEUP_TIMER || wakeCause == ESP_SLEEP_WAKEUP_EXT1)
            graphics::UIRenderer::drawFrameText(display, state, x, y, "Resuming...");
        else
#endif
        {
            const char *region = myRegion ? myRegion->name : nullptr;
            graphics::UIRenderer::drawIconScreen(region, display, state, x, y);
        }
    };
    ui->setFrames(alertFrames, 1);
    ui->disableAutoTransition(); // Require manual navigation between frames

    // === Log buffer for on-screen logs (3 lines max) ===
    dispdev->setLogBuffer(3, 32);

    // === Optional screen mirroring or flipping (e.g. for T-Beam orientation) ===
#ifdef SCREEN_MIRROR
    dispdev->mirrorScreen();
#else
    if (!config.display.flip_screen) {
#if defined(ST7701_CS) || defined(ST7735_CS) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7789_CS) ||      \
    defined(RAK14014) || defined(HX8357_CS) || defined(ILI9488_CS)
        static_cast<TFTDisplay *>(dispdev)->flipScreenVertically();
#elif defined(USE_ST7789)
        static_cast<ST7789Spi *>(dispdev)->flipScreenVertically();
#else
        dispdev->flipScreenVertically();
#endif
    }
#endif

    // === Generate device ID from MAC address ===
    uint8_t dmac[6];
    getMacAddr(dmac);
    snprintf(screen->ourId, sizeof(screen->ourId), "%02x%02x", dmac[4], dmac[5]);

#if ARCH_PORTDUINO
    handleSetOn(false); // Ensure proper init for Arduino targets
#endif

    // === Turn on display and trigger first draw ===
    handleSetOn(true);
    determineResolution(dispdev->height(), dispdev->width());
    ui->update();
#ifndef USE_EINK
    ui->update(); // Some SSD1306 clones drop the first draw, so run twice
#endif
    serialSinceMsec = millis();

#if ARCH_PORTDUINO
    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
        if (settingsMap[touchscreenModule]) {
            touchScreenImpl1 =
                new TouchScreenImpl1(dispdev->getWidth(), dispdev->getHeight(), static_cast<TFTDisplay *>(dispdev)->getTouch);
            touchScreenImpl1->init();
        }
    }
#elif HAS_TOUCHSCREEN && !defined(USE_EINK)
    touchScreenImpl1 =
        new TouchScreenImpl1(dispdev->getWidth(), dispdev->getHeight(), static_cast<TFTDisplay *>(dispdev)->getTouch);
    touchScreenImpl1->init();
#endif

    // === Subscribe to device status updates ===
    powerStatusObserver.observe(&powerStatus->onNewStatus);
    gpsStatusObserver.observe(&gpsStatus->onNewStatus);
    nodeStatusObserver.observe(&nodeStatus->onNewStatus);

#if !MESHTASTIC_EXCLUDE_ADMIN
    adminMessageObserver.observe(adminModule);
#endif
    if (textMessageModule)
        textMessageObserver.observe(textMessageModule);
    if (inputBroker)
        inputObserver.observe(inputBroker);

    // === Notify modules that support UI events ===
    MeshModule::observeUIEvents(&uiFrameEventObserver);
}

void Screen::forceDisplay(bool forceUiUpdate)
{
    // Nasty hack to force epaper updates for 'key' frames.  FIXME, cleanup.
#ifdef USE_EINK
    // If requested, make sure queued commands are run, and UI has rendered a new frame
    if (forceUiUpdate) {
        // Force a display refresh, in addition to the UI update
        // Changing the GPS status bar icon apparently doesn't register as a change in image
        // (False negative of the image hashing algorithm used to skip identical frames)
        EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST);

        // No delay between UI frame rendering
        setFastFramerate();

        // Make sure all CMDs have run first
        while (!cmdQueue.isEmpty())
            runOnce();

        // Ensure at least one frame has drawn
        uint64_t startUpdate;
        do {
            startUpdate = millis(); // Handle impossibly unlikely corner case of a millis() overflow..
            delay(10);
            ui->update();
        } while (ui->getUiState()->lastUpdate < startUpdate);

        // Return to normal frame rate
        targetFramerate = IDLE_FRAMERATE;
        ui->setTargetFPS(targetFramerate);
    }

    // Tell EInk class to update the display
    static_cast<EInkDisplay *>(dispdev)->forceDisplay();
#else
    // No delay between UI frame rendering
    if (forceUiUpdate) {
        setFastFramerate();
    }
#endif
}

static uint32_t lastScreenTransition;

int32_t Screen::runOnce()
{
    // If we don't have a screen, don't ever spend any CPU for us.
    if (!useDisplay) {
        enabled = false;
        return RUN_SAME;
    }

    if (displayHeight == 0) {
        displayHeight = dispdev->getHeight();
    }
    menuHandler::handleMenuSwitch(dispdev);

    // Show boot screen for first logo_timeout seconds, then switch to normal operation.
    // serialSinceMsec adjusts for additional serial wait time during nRF52 bootup
    static bool showingBootScreen = true;
    if (showingBootScreen && (millis() > (logo_timeout + serialSinceMsec))) {
        LOG_INFO("Done with boot screen");
        stopBootScreen();
        showingBootScreen = false;
    }

#ifdef USERPREFS_OEM_TEXT
    static bool showingOEMBootScreen = true;
    if (showingOEMBootScreen && (millis() > ((logo_timeout / 2) + serialSinceMsec))) {
        LOG_INFO("Switch to OEM screen...");
        // Change frames.
        static FrameCallback bootOEMFrames[] = {graphics::UIRenderer::drawOEMBootScreen};
        static const int bootOEMFrameCount = sizeof(bootOEMFrames) / sizeof(bootOEMFrames[0]);
        ui->setFrames(bootOEMFrames, bootOEMFrameCount);
        ui->update();
#ifndef USE_EINK
        ui->update();
#endif
        showingOEMBootScreen = false;
    }
#endif

#ifndef DISABLE_WELCOME_UNSET
    if (!NotificationRenderer::isOverlayBannerShowing() && config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
        menuHandler::OnboardMessage();
    }
#endif
    if (!NotificationRenderer::isOverlayBannerShowing() && rebootAtMsec != 0) {
        showSimpleBanner("Rebooting...", 0);
    }

    // Process incoming commands.
    for (;;) {
        ScreenCmd cmd;
        if (!cmdQueue.dequeue(&cmd, 0)) {
            break;
        }
        switch (cmd.cmd) {
        case Cmd::SET_ON:
            handleSetOn(true);
            break;
        case Cmd::SET_OFF:
            handleSetOn(false);
            break;
        case Cmd::ON_PRESS:
            handleOnPress();
            break;
        case Cmd::SHOW_PREV_FRAME:
            handleShowPrevFrame();
            break;
        case Cmd::SHOW_NEXT_FRAME:
            handleShowNextFrame();
            break;
        case Cmd::START_ALERT_FRAME: {
            showingBootScreen = false; // this should avoid the edge case where an alert triggers before the boot screen goes away
            showingNormalScreen = false;
            NotificationRenderer::pauseBanner = true;
            alertFrames[0] = alertFrame;
#ifdef USE_EINK
            EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Use fast-refresh for next frame, no skip please
            EINK_ADD_FRAMEFLAG(dispdev, BLOCKING);    // Edge case: if this frame is promoted to COSMETIC, wait for update
            handleSetOn(true); // Ensure power-on to receive deep-sleep screensaver (PowerFSM should handle?)
#endif
            setFrameImmediateDraw(alertFrames);
            break;
        }
        case Cmd::START_FIRMWARE_UPDATE_SCREEN:
            handleStartFirmwareUpdateScreen();
            break;
        case Cmd::STOP_ALERT_FRAME:
            NotificationRenderer::pauseBanner = false;
        case Cmd::STOP_BOOT_SCREEN:
            EINK_ADD_FRAMEFLAG(dispdev, COSMETIC); // E-Ink: Explicitly use full-refresh for next frame
            setFrames();
            break;
        case Cmd::NOOP:
            break;
        default:
            LOG_ERROR("Invalid screen cmd");
        }
    }

    if (!screenOn) { // If we didn't just wake and the screen is still off, then
                     // stop updating until it is on again
        enabled = false;
        return 0;
    }

    // this must be before the frameState == FIXED check, because we always
    // want to draw at least one FIXED frame before doing forceDisplay
    ui->update();

    // Switch to a low framerate (to save CPU) when we are not in transition
    // but we should only call setTargetFPS when framestate changes, because
    // otherwise that breaks animations.

    if (targetFramerate != IDLE_FRAMERATE && ui->getUiState()->frameState == FIXED) {
        // oldFrameState = ui->getUiState()->frameState;
        targetFramerate = IDLE_FRAMERATE;

        ui->setTargetFPS(targetFramerate);
        forceDisplay();
    }

    // While showing the bootscreen or Bluetooth pair screen all of our
    // standard screen switching is stopped.
    if (showingNormalScreen) {
        // standard screen loop handling here
        if (config.display.auto_screen_carousel_secs > 0 &&
            !Throttle::isWithinTimespanMs(lastScreenTransition, config.display.auto_screen_carousel_secs * 1000)) {

            // If an E-Ink display struggles with fast refresh, force carousel to use full refresh instead
            // Carousel is potentially a major source of E-Ink display wear
#if !defined(EINK_BACKGROUND_USES_FAST)
            EINK_ADD_FRAMEFLAG(dispdev, COSMETIC);
#endif

            LOG_DEBUG("LastScreenTransition exceeded %ums transition to next frame", (millis() - lastScreenTransition));
            handleOnPress();
        }
    }

    // LOG_DEBUG("want fps %d, fixed=%d", targetFramerate,
    // ui->getUiState()->frameState); If we are scrolling we need to be called
    // soon, otherwise just 1 fps (to save CPU) We also ask to be called twice
    // as fast as we really need so that any rounding errors still result with
    // the correct framerate
    return (1000 / targetFramerate);
}

/* show a message that the SSL cert is being built
 * it is expected that this will be used during the boot phase */
void Screen::setSSLFrames()
{
    if (address_found.address) {
        // LOG_DEBUG("Show SSL frames");
        static FrameCallback sslFrames[] = {NotificationRenderer::drawSSLScreen};
        ui->setFrames(sslFrames, 1);
        ui->update();
    }
}

#ifdef USE_EINK
/// Determine which screensaver frame to use, then set the FrameCallback
void Screen::setScreensaverFrames(FrameCallback einkScreensaver)
{
    // Retain specified frame / overlay callback beyond scope of this method
    static FrameCallback screensaverFrame;
    static OverlayCallback screensaverOverlay;

#if defined(HAS_EINK_ASYNCFULL) && defined(USE_EINK_DYNAMICDISPLAY)
    // Join (await) a currently running async refresh, then run the post-update code.
    // Avoid skipping of screensaver frame. Would otherwise be handled by NotifiedWorkerThread.
    EINK_JOIN_ASYNCREFRESH(dispdev);
#endif

    // If: one-off screensaver frame passed as argument. Handles doDeepSleep()
    if (einkScreensaver != NULL) {
        screensaverFrame = einkScreensaver;
        ui->setFrames(&screensaverFrame, 1);
    }

    // Else, display the usual "overlay" screensaver
    else {
        screensaverOverlay = graphics::UIRenderer::drawScreensaverOverlay;
        ui->setOverlays(&screensaverOverlay, 1);
    }

    // Request new frame, ASAP
    setFastFramerate();
    uint64_t startUpdate;
    do {
        startUpdate = millis(); // Handle impossibly unlikely corner case of a millis() overflow..
        delay(1);
        ui->update();
    } while (ui->getUiState()->lastUpdate < startUpdate);

    // Old EInkDisplay class
#if !defined(USE_EINK_DYNAMICDISPLAY)
    static_cast<EInkDisplay *>(dispdev)->forceDisplay(0); // Screen::forceDisplay(), but override rate-limit
#endif

    // Prepare now for next frame, shown when display wakes
    ui->setOverlays(NULL, 0);  // Clear overlay
    setFrames(FOCUS_PRESERVE); // Return to normal display updates, showing same frame as before screensaver, ideally

    // Pick a refresh method, for when display wakes
#ifdef EINK_HASQUIRK_GHOSTING
    EINK_ADD_FRAMEFLAG(dispdev, COSMETIC); // Really ugly to see ghosting from "screen paused"
#else
    EINK_ADD_FRAMEFLAG(dispdev, RESPONSIVE); // Really nice to wake screen with a fast-refresh
#endif
}
#endif

// Regenerate the normal set of frames, focusing a specific frame if requested
// Called when a frame should be added / removed, or custom frames should be cleared
void Screen::setFrames(FrameFocus focus)
{
    uint8_t originalPosition = ui->getUiState()->currentFrame;
    uint8_t previousFrameCount = framesetInfo.frameCount;
    FramesetInfo fsi; // Location of specific frames, for applying focus parameter

    graphics::UIRenderer::rebuildFavoritedNodes();

    LOG_DEBUG("Show standard frames");
    showingNormalScreen = true;

    indicatorIcons.clear();

    size_t numframes = 0;

    // If we have a critical fault, show it first
    fsi.positions.fault = numframes;
    if (error_code) {
        normalFrames[numframes++] = NotificationRenderer::drawCriticalFaultFrame;
        indicatorIcons.push_back(icon_error);
        focus = FOCUS_FAULT; // Change our "focus" parameter, to ensure we show the fault frame
    }

#if defined(DISPLAY_CLOCK_FRAME)
    fsi.positions.clock = numframes;
    normalFrames[numframes++] = uiconfig.is_clockface_analog ? graphics::ClockRenderer::drawAnalogClockFrame
                                                             : graphics::ClockRenderer::drawDigitalClockFrame;
    indicatorIcons.push_back(digital_icon_clock);
#endif

    // Declare this early so it’s available in FOCUS_PRESERVE block
    bool willInsertTextMessage = shouldDrawMessage(&devicestate.rx_text_message);

    fsi.positions.home = numframes;
    normalFrames[numframes++] = graphics::UIRenderer::drawDeviceFocused;
    indicatorIcons.push_back(icon_home);

    fsi.positions.textMessage = numframes;
    normalFrames[numframes++] = graphics::MessageRenderer::drawTextMessageFrame;
    indicatorIcons.push_back(icon_mail);

#ifndef USE_EINK
    fsi.positions.nodelist = numframes;
    normalFrames[numframes++] = graphics::NodeListRenderer::drawDynamicNodeListScreen;
    indicatorIcons.push_back(icon_nodes);
#endif

// Show detailed node views only on E-Ink builds
#ifdef USE_EINK
    fsi.positions.nodelist_lastheard = numframes;
    normalFrames[numframes++] = graphics::NodeListRenderer::drawLastHeardScreen;
    indicatorIcons.push_back(icon_nodes);

    fsi.positions.nodelist_hopsignal = numframes;
    normalFrames[numframes++] = graphics::NodeListRenderer::drawHopSignalScreen;
    indicatorIcons.push_back(icon_signal);

    fsi.positions.nodelist_distance = numframes;
    normalFrames[numframes++] = graphics::NodeListRenderer::drawDistanceScreen;
    indicatorIcons.push_back(icon_distance);
#endif
#if HAS_GPS
    fsi.positions.nodelist_bearings = numframes;
    normalFrames[numframes++] = graphics::NodeListRenderer::drawNodeListWithCompasses;
    indicatorIcons.push_back(icon_list);

    fsi.positions.gps = numframes;
    normalFrames[numframes++] = graphics::UIRenderer::drawCompassAndLocationScreen;
    indicatorIcons.push_back(icon_compass);
#endif
    if (RadioLibInterface::instance) {
        fsi.positions.lora = numframes;
        normalFrames[numframes++] = graphics::DebugRenderer::drawLoRaFocused;
        indicatorIcons.push_back(icon_radio);
    }
    if (!dismissedFrames.memory) {
        fsi.positions.memory = numframes;
        normalFrames[numframes++] = graphics::DebugRenderer::drawMemoryUsage;
        indicatorIcons.push_back(icon_memory);
    }
#if !defined(DISPLAY_CLOCK_FRAME)
    fsi.positions.clock = numframes;
    normalFrames[numframes++] = uiconfig.is_clockface_analog ? graphics::ClockRenderer::drawAnalogClockFrame
                                                             : graphics::ClockRenderer::drawDigitalClockFrame;
    indicatorIcons.push_back(digital_icon_clock);
#endif

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
    if (!dismissedFrames.wifi && isWifiAvailable()) {
        fsi.positions.wifi = numframes;
        normalFrames[numframes++] = graphics::DebugRenderer::drawDebugInfoWiFiTrampoline;
        indicatorIcons.push_back(icon_wifi);
    }
#endif

    // Beware of what changes you make in this code!
    // We pass numframes into GetMeshModulesWithUIFrames() which is highly important!
    // Inside of that callback, goes over to MeshModule.cpp and we run
    // modulesWithUIFrames.resize(startIndex, nullptr), to insert nullptr
    // entries until we're ready to start building the matching entries.
    // We are doing our best to keep the normalFrames vector
    // and the moduleFrames vector in lock step.
    moduleFrames = MeshModule::GetMeshModulesWithUIFrames(numframes);
    LOG_DEBUG("Show %d module frames", moduleFrames.size());

    for (auto i = moduleFrames.begin(); i != moduleFrames.end(); ++i) {
        // Draw the module frame, using the hack described above
        if (*i != nullptr) {
            normalFrames[numframes] = drawModuleFrame;

            // Check if the module being drawn has requested focus
            // We will honor this request later, if setFrames was triggered by a UIFrameEvent
            MeshModule *m = *i;
            if (m && m->isRequestingFocus())
                fsi.positions.focusedModule = numframes;
            if (m && m == waypointModule)
                fsi.positions.waypoint = numframes;

            indicatorIcons.push_back(icon_module);
            numframes++;
        }
    }

    LOG_DEBUG("Added modules.  numframes: %d", numframes);

    // We don't show the node info of our node (if we have it yet - we should)
    size_t numMeshNodes = nodeDB->getNumMeshNodes();
    if (numMeshNodes > 0)
        numMeshNodes--;

    // Temporary array to hold favorite node frames
    std::vector<FrameCallback> favoriteFrames;

    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        const meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
        if (n && n->num != nodeDB->getNodeNum() && n->is_favorite) {
            favoriteFrames.push_back(graphics::UIRenderer::drawNodeInfo);
        }
    }

    // Insert favorite frames *after* collecting them all
    if (!favoriteFrames.empty()) {
        fsi.positions.firstFavorite = numframes;
        for (const auto &f : favoriteFrames) {
            normalFrames[numframes++] = f;
            indicatorIcons.push_back(icon_node);
        }
        fsi.positions.lastFavorite = numframes - 1;
    } else {
        fsi.positions.firstFavorite = 255;
        fsi.positions.lastFavorite = 255;
    }

    fsi.frameCount = numframes;   // Total framecount is used to apply FOCUS_PRESERVE
    this->frameCount = numframes; // ✅ Save frame count for use in custom overlay
    LOG_DEBUG("Finished build frames. numframes: %d", numframes);

    ui->setFrames(normalFrames, numframes);
    ui->disableAllIndicators();

    // Add overlays: frame icons and alert banner)
    static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));

    prevFrame = -1; // Force drawNodeInfo to pick a new node (because our list just changed)

    // Focus on a specific frame, in the frame set we just created
    switch (focus) {
    case FOCUS_DEFAULT:
        ui->switchToFrame(fsi.positions.deviceFocused);
        break;
    case FOCUS_FAULT:
        ui->switchToFrame(fsi.positions.fault);
        break;
    case FOCUS_TEXTMESSAGE:
        hasUnreadMessage = false; // ✅ Clear when message is *viewed*
        ui->switchToFrame(fsi.positions.textMessage);
        break;
    case FOCUS_MODULE:
        // Whichever frame was marked by MeshModule::requestFocus(), if any
        // If no module requested focus, will show the first frame instead
        ui->switchToFrame(fsi.positions.focusedModule);
        break;
    case FOCUS_CLOCK:
        // Whichever frame was marked by MeshModule::requestFocus(), if any
        // If no module requested focus, will show the first frame instead
        ui->switchToFrame(fsi.positions.clock);
        break;
    case FOCUS_SYSTEM:
        ui->switchToFrame(fsi.positions.memory);
        break;

    case FOCUS_PRESERVE:
        //  No more adjustment — force stay on same index
        if (previousFrameCount > fsi.frameCount) {
            ui->switchToFrame(originalPosition - 1);
        } else if (previousFrameCount < fsi.frameCount) {
            ui->switchToFrame(originalPosition + 1);
        } else {
            ui->switchToFrame(originalPosition);
        }
        break;
    }

    // Store the info about this frameset, for future setFrames calls
    this->framesetInfo = fsi;

    setFastFramerate(); // Draw ASAP
}

void Screen::setFrameImmediateDraw(FrameCallback *drawFrames)
{
    ui->disableAllIndicators();
    ui->setFrames(drawFrames, 1);
    setFastFramerate();
}

// Dismisses the currently displayed screen frame, if possible
// Relevant for text message, waypoint, others in future?
// Triggered with a CardKB keycombo
void Screen::dismissCurrentFrame()
{
    uint8_t currentFrame = ui->getUiState()->currentFrame;
    bool dismissed = false;

    if (currentFrame == framesetInfo.positions.textMessage && devicestate.has_rx_text_message) {
        LOG_INFO("Dismiss Text Message");
        devicestate.has_rx_text_message = false;
        memset(&devicestate.rx_text_message, 0, sizeof(devicestate.rx_text_message));
    } else if (currentFrame == framesetInfo.positions.waypoint && devicestate.has_rx_waypoint) {
        LOG_DEBUG("Dismiss Waypoint");
        devicestate.has_rx_waypoint = false;
        dismissedFrames.waypoint = true;
        dismissed = true;
    } else if (currentFrame == framesetInfo.positions.wifi) {
        LOG_DEBUG("Dismiss WiFi Screen");
        dismissedFrames.wifi = true;
        dismissed = true;
    } else if (currentFrame == framesetInfo.positions.memory) {
        LOG_INFO("Dismiss Memory");
        dismissedFrames.memory = true;
        dismissed = true;
    }

    if (dismissed) {
        setFrames(FOCUS_DEFAULT); // You could also use FOCUS_PRESERVE
    }
}

void Screen::handleStartFirmwareUpdateScreen()
{
    LOG_DEBUG("Show firmware screen");
    showingNormalScreen = false;
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // E-Ink: Explicitly use fast-refresh for next frame

    static FrameCallback frames[] = {graphics::NotificationRenderer::drawFrameFirmware};
    setFrameImmediateDraw(frames);
}

void Screen::blink()
{
    setFastFramerate();
    uint8_t count = 10;
    dispdev->setBrightness(254);
    while (count > 0) {
        dispdev->fillRect(0, 0, dispdev->getWidth(), dispdev->getHeight());
        dispdev->display();
        delay(50);
        dispdev->clear();
        dispdev->display();
        delay(50);
        count = count - 1;
    }
    // The dispdev->setBrightness does not work for t-deck display, it seems to run the setBrightness function in OLEDDisplay.
    dispdev->setBrightness(brightness);
}

void Screen::increaseBrightness()
{
    brightness = ((brightness + 62) > 254) ? brightness : (brightness + 62);

#if defined(ST7789_CS)
    // run the setDisplayBrightness function. This works on t-decks
    static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#endif

    /* TO DO: add little popup in center of screen saying what brightness level it is set to*/
}

void Screen::decreaseBrightness()
{
    brightness = (brightness < 70) ? brightness : (brightness - 62);

#if defined(ST7789_CS)
    static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#endif

    /* TO DO: add little popup in center of screen saying what brightness level it is set to*/
}

void Screen::setFunctionSymbol(std::string sym)
{
    if (std::find(functionSymbol.begin(), functionSymbol.end(), sym) == functionSymbol.end()) {
        functionSymbol.push_back(sym);
        functionSymbolString = "";
        for (auto symbol : functionSymbol) {
            functionSymbolString = symbol + " " + functionSymbolString;
        }
        setFastFramerate();
    }
}

void Screen::removeFunctionSymbol(std::string sym)
{
    functionSymbol.erase(std::remove(functionSymbol.begin(), functionSymbol.end(), sym), functionSymbol.end());
    functionSymbolString = "";
    for (auto symbol : functionSymbol) {
        functionSymbolString = symbol + " " + functionSymbolString;
    }
    setFastFramerate();
}

void Screen::handleOnPress()
{
    // If screen was off, just wake it, otherwise advance to next frame
    // If we are in a transition, the press must have bounced, drop it.
    if (ui->getUiState()->frameState == FIXED) {
        ui->nextFrame();
        lastScreenTransition = millis();
        setFastFramerate();
    }
}

void Screen::handleShowPrevFrame()
{
    // If screen was off, just wake it, otherwise go back to previous frame
    // If we are in a transition, the press must have bounced, drop it.
    if (ui->getUiState()->frameState == FIXED) {
        ui->previousFrame();
        lastScreenTransition = millis();
        setFastFramerate();
    }
}

void Screen::handleShowNextFrame()
{
    // If screen was off, just wake it, otherwise advance to next frame
    // If we are in a transition, the press must have bounced, drop it.
    if (ui->getUiState()->frameState == FIXED) {
        ui->nextFrame();
        lastScreenTransition = millis();
        setFastFramerate();
    }
}

#ifndef SCREEN_TRANSITION_FRAMERATE
#define SCREEN_TRANSITION_FRAMERATE 30 // fps
#endif

void Screen::setFastFramerate()
{
    // We are about to start a transition so speed up fps
    targetFramerate = SCREEN_TRANSITION_FRAMERATE;

    ui->setTargetFPS(targetFramerate);
    setInterval(0); // redraw ASAP
    runASAP = true;
}

int Screen::handleStatusUpdate(const meshtastic::Status *arg)
{
    // LOG_DEBUG("Screen got status update %d", arg->getStatusType());
    switch (arg->getStatusType()) {
    case STATUS_TYPE_NODE:
        if (showingNormalScreen && nodeStatus->getLastNumTotal() != nodeStatus->getNumTotal()) {
            setFrames(FOCUS_PRESERVE); // Regen the list of screen frames (returning to same frame, if possible)
        }
        nodeDB->updateGUI = false;
        break;
    }

    return 0;
}

// Handles when message is received; will jump to text message frame.
int Screen::handleTextMessage(const meshtastic_MeshPacket *packet)
{
    if (showingNormalScreen) {
        if (packet->from == 0) {
            // Outgoing message (likely sent from phone)
            devicestate.has_rx_text_message = false;
            memset(&devicestate.rx_text_message, 0, sizeof(devicestate.rx_text_message));
            dismissedFrames.textMessage = true;
            hasUnreadMessage = false; // Clear unread state when user replies

            setFrames(FOCUS_PRESERVE); // Stay on same frame, silently update frame list
        } else {
            // Incoming message
            devicestate.has_rx_text_message = true; // Needed to include the message frame
            hasUnreadMessage = true;                // Enables mail icon in the header
            setFrames(FOCUS_PRESERVE);              // Refresh frame list without switching view

            // Only wake/force display if the configuration allows it
            if (shouldWakeOnReceivedMessage()) {
                setOn(true);    // Wake up the screen first
                forceDisplay(); // Forces screen redraw
            }
            // === Prepare banner content ===
            const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(packet->from);
            const char *longName = (node && node->has_user) ? node->user.long_name : nullptr;

            const char *msgRaw = reinterpret_cast<const char *>(packet->decoded.payload.bytes);

            char banner[256];

            // Check for bell character in message to determine alert type
            bool isAlert = false;
            for (size_t i = 0; i < packet->decoded.payload.size && i < 100; i++) {
                if (msgRaw[i] == '\x07') {
                    isAlert = true;
                    break;
                }
            }

            if (isAlert) {
                if (longName && longName[0]) {
                    snprintf(banner, sizeof(banner), "Alert Received from\n%s", longName);
                } else {
                    strcpy(banner, "Alert Received");
                }
            } else {
                if (longName && longName[0]) {
                    snprintf(banner, sizeof(banner), "New Message from\n%s", longName);
                } else {
                    strcpy(banner, "New Message");
                }
            }

            screen->showSimpleBanner(banner, 3000);
        }
    }

    return 0;
}

// Triggered by MeshModules
int Screen::handleUIFrameEvent(const UIFrameEvent *event)
{
    if (showingNormalScreen) {
        // Regenerate the frameset, potentially honoring a module's internal requestFocus() call
        if (event->action == UIFrameEvent::Action::REGENERATE_FRAMESET)
            setFrames(FOCUS_MODULE);

        // Regenerate the frameset, while Attempt to maintain focus on the current frame
        else if (event->action == UIFrameEvent::Action::REGENERATE_FRAMESET_BACKGROUND)
            setFrames(FOCUS_PRESERVE);

        // Don't regenerate the frameset, just re-draw whatever is on screen ASAP
        else if (event->action == UIFrameEvent::Action::REDRAW_ONLY)
            setFastFramerate();
    }

    return 0;
}

int Screen::handleInputEvent(const InputEvent *event)
{
    if (!screenOn)
        return 0;

#ifdef USE_EINK // the screen is the last input handler, so if an event makes it here, we can assume it will prompt a screen draw.
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Use fast-refresh for next frame, no skip please
    EINK_ADD_FRAMEFLAG(dispdev, BLOCKING);    // Edge case: if this frame is promoted to COSMETIC, wait for update
    handleSetOn(true);                        // Ensure power-on to receive deep-sleep screensaver (PowerFSM should handle?)
    setFastFramerate();                       // Draw ASAP
#endif
    if (NotificationRenderer::isOverlayBannerShowing()) {
        NotificationRenderer::inEvent = *event;
        static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
        ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
        setFastFramerate(); // Draw ASAP
        ui->update();

        menuHandler::handleMenuSwitch(dispdev);
        return 0;
    }

    // Use left or right input from a keyboard to move between frames,
    // so long as a mesh module isn't using these events for some other purpose
    if (showingNormalScreen) {

        // Ask any MeshModules if they're handling keyboard input right now
        bool inputIntercepted = false;
        for (MeshModule *module : moduleFrames) {
            if (module && module->interceptingKeyboardInput())
                inputIntercepted = true;
        }

        // If no modules are using the input, move between frames
        if (!inputIntercepted) {
            if (event->inputEvent == INPUT_BROKER_LEFT || event->inputEvent == INPUT_BROKER_ALT_PRESS) {
                showPrevFrame();
            } else if (event->inputEvent == INPUT_BROKER_RIGHT || event->inputEvent == INPUT_BROKER_USER_PRESS) {
                showNextFrame();
            } else if (event->inputEvent == INPUT_BROKER_SELECT) {
                if (this->ui->getUiState()->currentFrame == framesetInfo.positions.home) {
                    menuHandler::homeBaseMenu();
                } else if (this->ui->getUiState()->currentFrame == framesetInfo.positions.memory) {
                    menuHandler::systemBaseMenu();
#if HAS_GPS
                } else if (this->ui->getUiState()->currentFrame == framesetInfo.positions.gps && gps) {
                    menuHandler::positionBaseMenu();
#endif
                } else if (this->ui->getUiState()->currentFrame == framesetInfo.positions.clock) {
                    menuHandler::clockMenu();
                } else if (this->ui->getUiState()->currentFrame == framesetInfo.positions.lora) {
                    menuHandler::LoraRegionPicker();
                } else if (this->ui->getUiState()->currentFrame == framesetInfo.positions.textMessage) {
                    if (devicestate.rx_text_message.from) {
                        menuHandler::messageResponseMenu();
                    } else {
                        menuHandler::textMessageBaseMenu();
                    }
                } else if (framesetInfo.positions.firstFavorite != 255 &&
                           this->ui->getUiState()->currentFrame >= framesetInfo.positions.firstFavorite &&
                           this->ui->getUiState()->currentFrame <= framesetInfo.positions.lastFavorite) {
                    menuHandler::favoriteBaseMenu();
                } else if (this->ui->getUiState()->currentFrame == framesetInfo.positions.nodelist ||
                           this->ui->getUiState()->currentFrame == framesetInfo.positions.nodelist_lastheard ||
                           this->ui->getUiState()->currentFrame == framesetInfo.positions.nodelist_hopsignal ||
                           this->ui->getUiState()->currentFrame == framesetInfo.positions.nodelist_distance ||
                           this->ui->getUiState()->currentFrame == framesetInfo.positions.nodelist_hopsignal ||
                           this->ui->getUiState()->currentFrame == framesetInfo.positions.nodelist_bearings) {
                    menuHandler::nodeListMenu();
                } else if (this->ui->getUiState()->currentFrame == framesetInfo.positions.wifi) {
                    menuHandler::wifiBaseMenu();
                }
            } else if (event->inputEvent == INPUT_BROKER_BACK) {
                showPrevFrame();
            } else if (event->inputEvent == INPUT_BROKER_CANCEL) {
                setOn(false);
            }
        }
    }

    return 0;
}

int Screen::handleAdminMessage(AdminModule_ObserverData *arg)
{
    switch (arg->request->which_payload_variant) {
    // Node removed manually (i.e. via app)
    case meshtastic_AdminMessage_remove_by_nodenum_tag:
        setFrames(FOCUS_PRESERVE);
        *arg->result = AdminMessageHandleResult::HANDLED;
        break;

    // Default no-op, in case the admin message observable gets used by other classes in future
    default:
        break;
    }
    return 0;
}

bool Screen::isOverlayBannerShowing()
{
    return NotificationRenderer::isOverlayBannerShowing();
}

} // namespace graphics

#else
graphics::Screen::Screen(ScanI2C::DeviceAddress, meshtastic_Config_DisplayConfig_OledType, OLEDDISPLAY_GEOMETRY) {}
#endif // HAS_SCREEN

bool shouldWakeOnReceivedMessage()
{
    /*
    The goal here is to determine when we do NOT wake up the screen on message received:
    - Any ext. notifications are turned on
    - If role is not client / client_mute
    - If the battery level is very low
    */
    if (moduleConfig.external_notification.enabled) {
        return false;
    }
    if (!meshtastic_Config_DeviceConfig_Role_CLIENT && !meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE) {
        return false;
    }
    if (powerStatus && powerStatus->getBatteryChargePercent() < 10) {
        return false;
    }
    return true;
}
