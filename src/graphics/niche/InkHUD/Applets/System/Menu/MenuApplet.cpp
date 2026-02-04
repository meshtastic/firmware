#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./MenuApplet.h"

#include "DisplayFormatters.h"
#include "GPS.h"
#include "MeshService.h"
#include "RTC.h"
#include "Router.h"
#include "airtime.h"
#include "main.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "power.h"
#include <RadioLibInterface.h>
#include <target_specific.h>
#if defined(ARCH_ESP32) && HAS_WIFI
#include "mesh/wifi/WiFiAPClient.h"
#include <WiFi.h>
#include <esp_wifi.h>
#endif

using namespace NicheGraphics;

static constexpr uint8_t MENU_TIMEOUT_SEC = 60; // How many seconds before menu auto-closes

// Options for the "Recents" menu
// These are offered to users as possible values for settings.recentlyActiveSeconds
static constexpr uint8_t RECENTS_OPTIONS_MINUTES[] = {2, 5, 10, 30, 60, 120};

struct PositionPrecisionOption {
    uint8_t value; // proto value
    const char *metric;
    const char *imperial;
};

static constexpr PositionPrecisionOption POSITION_PRECISION_OPTIONS[] = {
    {32, "Precise", "Precise"}, {19, "50 m", "150 ft"},  {18, "90 m", "300 ft"},   {17, "200 m", "600 ft"},
    {16, "350 m", "0.2 mi"},    {15, "700 m", "0.5 mi"}, {14, "1.5 km", "0.9 mi"}, {13, "2.9 km", "1.8 mi"},
    {12, "5.8 km", "3.6 mi"},   {11, "12 km", "7.3 mi"}, {10, "23 km", "15 mi"},
};

InkHUD::MenuApplet::MenuApplet() : concurrency::OSThread("MenuApplet")
{
    // No timer tasks at boot
    OSThread::disable();

    // Note: don't get instance if we're not actually using the backlight,
    // or else you will unintentionally instantiate it
    if (settings->optionalMenuItems.backlight) {
        backlight = Drivers::LatchingBacklight::getInstance();
    }

    // Initialize the Canned Message store
    // This is a shared nicheGraphics component
    // - handles loading & parsing the canned messages
    // - handles setting / getting of canned messages via apps (Client API Admin Messages)
    cm.store = CannedMessageStore::getInstance();
}

void InkHUD::MenuApplet::onForeground()
{
    // We do need this before we render, but we can optimize by just calculating it once now
    systemInfoPanelHeight = getSystemInfoPanelHeight();

    // Force Region page ONLY when explicitly requested (one-shot)
    if (inkhud->forceRegionMenu) {

        inkhud->forceRegionMenu = false; // consume one-shot flag
        showPage(MenuPage::REGION);

    } else {
        showPage(MenuPage::ROOT);
    }

    // If device has a backlight which isn't controlled by aux button:
    // backlight on always when menu opens.
    // Courtesy to T-Echo users who removed the capacitive touch button
    if (settings->optionalMenuItems.backlight) {
        assert(backlight);
        if (!backlight->isOn())
            backlight->peek();
    }

    // Prevent user applets requesting update while menu is open
    // Handle button input with this applet
    SystemApplet::lockRequests = true;
    SystemApplet::handleInput = true;

    // Begin the auto-close timeout
    OSThread::setIntervalFromNow(MENU_TIMEOUT_SEC * 1000UL);
    OSThread::enabled = true;

    freeTextMode = false;

    // Upgrade the refresh to FAST, for guaranteed responsiveness
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

void InkHUD::MenuApplet::onBackground()
{
    // Discard any data we generated while selecting a canned message
    // Frees heap mem
    freeCannedMessageResources();

    // If device has a backlight which isn't controlled by aux button:
    // Item in options submenu allows keeping backlight on after menu is closed
    // If this item is deselected we will turn backlight off again, now that menu is closing
    if (settings->optionalMenuItems.backlight) {
        assert(backlight);
        if (!backlight->isLatched())
            backlight->off();
    }

    // Stop the auto-timeout
    OSThread::disable();

    // Resume normal rendering and button behavior of user applets
    SystemApplet::lockRequests = false;
    SystemApplet::handleInput = false;

    handleFreeText = false;

    // Restore the user applet whose tile we borrowed
    if (borrowedTileOwner)
        borrowedTileOwner->bringToForeground();
    Tile *t = getTile();
    t->assignApplet(borrowedTileOwner); // Break our link with the tile, (and relink it with real owner, if it had one)
    borrowedTileOwner = nullptr;

    // Need to force an update, as a polite request wouldn't be honored, seeing how we are now in the background
    // We're only updating here to upgrade from UNSPECIFIED to FAST, to ensure responsiveness when exiting menu
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

// Open the menu
// Parameter specifies which user-tile the menu will use
// The user applet originally on this tile will be restored when the menu closes
void InkHUD::MenuApplet::show(Tile *t)
{
    // Remember who *really* owns this tile
    borrowedTileOwner = t->getAssignedApplet();

    // Hide the owner, if it is a valid applet
    if (borrowedTileOwner)
        borrowedTileOwner->sendToBackground();

    // Break the owner's link with tile
    // Relink it to menu applet
    t->assignApplet(this);

    // Show menu
    bringToForeground();
}

// Auto-exit the menu applet after a period of inactivity
// The values shown on the root menu are only a snapshot: they are not re-rendered while the menu remains open.
// By exiting the menu, we prevent users mistakenly believing that the data will update.
int32_t InkHUD::MenuApplet::runOnce()
{
    // runOnce's interval is pushed back when a button is pressed
    // If we do actually run, it means no button input occurred within MENU_TIMEOUT_SEC,
    // so we close the menu.
    showPage(EXIT);

    // Timer should disable after firing
    // This is redundant, as onBackground() will also disable
    return OSThread::disable();
}

static void applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode region)
{
    if (config.lora.region == region)
        return;

    config.lora.region = region;

    auto changes = SEGMENT_CONFIG;

#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN || MESHTASTIC_EXCLUDE_PKI)
    if (!owner.is_licensed) {
        bool keygenSuccess = false;

        if (config.security.private_key.size == 32) {
            if (crypto->regeneratePublicKey(config.security.public_key.bytes, config.security.private_key.bytes)) {
                keygenSuccess = true;
            }
        } else {
            crypto->generateKeyPair(config.security.public_key.bytes, config.security.private_key.bytes);
            keygenSuccess = true;
        }

        if (keygenSuccess) {
            config.security.public_key.size = 32;
            config.security.private_key.size = 32;
            owner.public_key.size = 32;
            memcpy(owner.public_key.bytes, config.security.public_key.bytes, 32);
        }
    }
#endif

    config.lora.tx_enabled = true;

    initRegion();

    if (myRegion && myRegion->dutyCycle < 100) {
        config.lora.ignore_mqtt = true;
    }

    if (strncmp(moduleConfig.mqtt.root, default_mqtt_root, strlen(default_mqtt_root)) == 0) {
        sprintf(moduleConfig.mqtt.root, "%s/%s", default_mqtt_root, myRegion->name);
        changes |= SEGMENT_MODULECONFIG;
    }
    // Notify UI that changes are being applied
    InkHUD::InkHUD::getInstance()->notifyApplyingChanges();
    service->reloadConfig(changes);

    rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
}

static void applyDeviceRole(meshtastic_Config_DeviceConfig_Role role)
{
    if (config.device.role == role)
        return;

    config.device.role = role;

    nodeDB->saveToDisk(SEGMENT_CONFIG);

    service->reloadConfig(SEGMENT_CONFIG);

    // Notify UI that changes are being applied
    InkHUD::InkHUD::getInstance()->notifyApplyingChanges();

    rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
}

static void applyLoRaPreset(meshtastic_Config_LoRaConfig_ModemPreset preset)
{
    if (config.lora.modem_preset == preset)
        return;

    config.lora.use_preset = true;
    config.lora.modem_preset = preset;

    nodeDB->saveToDisk(SEGMENT_CONFIG);
    service->reloadConfig(SEGMENT_CONFIG);

    // Notify UI that changes are being applied
    InkHUD::InkHUD::getInstance()->notifyApplyingChanges();

    rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
}

static const char *getTimezoneLabelFromValue(const char *tzdef)
{
    if (!tzdef || !*tzdef)
        return "Unset";

    // Must match TIMEZONE menu entries
    if (strcmp(tzdef, "HST10") == 0)
        return "US/Hawaii";
    if (strcmp(tzdef, "AKST9AKDT,M3.2.0,M11.1.0") == 0)
        return "US/Alaska";
    if (strcmp(tzdef, "PST8PDT,M3.2.0,M11.1.0") == 0)
        return "US/Pacific";
    if (strcmp(tzdef, "MST7") == 0)
        return "US/Arizona";
    if (strcmp(tzdef, "MST7MDT,M3.2.0,M11.1.0") == 0)
        return "US/Mountain";
    if (strcmp(tzdef, "CST6CDT,M3.2.0,M11.1.0") == 0)
        return "US/Central";
    if (strcmp(tzdef, "EST5EDT,M3.2.0,M11.1.0") == 0)
        return "US/Eastern";
    if (strcmp(tzdef, "BRT3") == 0)
        return "BR/Brasilia";
    if (strcmp(tzdef, "UTC0") == 0)
        return "UTC";
    if (strcmp(tzdef, "GMT0BST,M3.5.0/1,M10.5.0") == 0)
        return "EU/Western";
    if (strcmp(tzdef, "CET-1CEST,M3.5.0,M10.5.0/3") == 0)
        return "EU/Central";
    if (strcmp(tzdef, "EET-2EEST,M3.5.0/3,M10.5.0/4") == 0)
        return "EU/Eastern";
    if (strcmp(tzdef, "IST-5:30") == 0)
        return "Asia/Kolkata";
    if (strcmp(tzdef, "HKT-8") == 0)
        return "Asia/Hong Kong";
    if (strcmp(tzdef, "AWST-8") == 0)
        return "AU/AWST";
    if (strcmp(tzdef, "ACST-9:30ACDT,M10.1.0,M4.1.0/3") == 0)
        return "AU/ACST";
    if (strcmp(tzdef, "AEST-10AEDT,M10.1.0,M4.1.0/3") == 0)
        return "AU/AEST";
    if (strcmp(tzdef, "NZST-12NZDT,M9.5.0,M4.1.0/3") == 0)
        return "Pacific/NZ";

    return tzdef; // fallback for unknown/custom values
}

static void applyTimezone(const char *tz)
{
    if (!tz || strcmp(config.device.tzdef, tz) == 0)
        return;

    strncpy(config.device.tzdef, tz, sizeof(config.device.tzdef));
    config.device.tzdef[sizeof(config.device.tzdef) - 1] = '\0';

    setenv("TZ", config.device.tzdef, 1);

    nodeDB->saveToDisk(SEGMENT_CONFIG);
    service->reloadConfig(SEGMENT_CONFIG);
}

// Perform action for a menu item, then change page
// Behaviors for MenuActions are defined here
void InkHUD::MenuApplet::execute(MenuItem item)
{
    // Perform an action
    // ------------------
    switch (item.action) {

    // Open a submenu without performing any action
    // Also handles exit
    case NO_ACTION:
        if (currentPage == MenuPage::NODE_CONFIG_CHANNELS && item.nextPage == MenuPage::NODE_CONFIG_CHANNEL_DETAIL) {

            // cursor - 1 because index 0 is "Back"
            selectedChannelIndex = cursor - 1;
        }
        break;

    case NEXT_TILE:
        inkhud->nextTile();
        // Unselect menu item after tile change
        cursorShown = false;
        cursor = 0;
        break;

    case SEND_PING:
        service->refreshLocalMeshNode();
        service->trySendPosition(NODENUM_BROADCAST, true);

        // Force the next refresh to use FULL, to protect the display, as some users will probably spam this button
        inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FULL);
        break;

    case FREE_TEXT:
        OSThread::enabled = false;
        handleFreeText = true;
        cm.freeTextItem.rawText.erase(); // clear the previous freetext message
        freeTextMode = true;             // render input field instead of normal menu
        // Open the on-screen keyboard if the joystick is enabled
        if (settings->joystick.enabled)
            inkhud->openKeyboard();
        break;

    case STORE_CANNEDMESSAGE_SELECTION:
        if (!settings->joystick.enabled)
            cm.selectedMessageItem = &cm.messageItems.at(cursor - 1); // Minus one: offset for the initial "Send Ping" entry
        else
            cm.selectedMessageItem = &cm.messageItems.at(cursor - 2); // Minus two: offset for the "Send Ping" and free text entry
        break;

    case SEND_CANNEDMESSAGE:
        cm.selectedRecipientItem = &cm.recipientItems.at(cursor);
        // send selected message
        sendText(cm.selectedRecipientItem->dest, cm.selectedRecipientItem->channelIndex, cm.selectedMessageItem->rawText.c_str());
        inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FULL); // Next refresh should be FULL. Lots of button pressing to get here
        break;

    case ROTATE:
        inkhud->rotate();
        break;

    case ALIGN_JOYSTICK:
        inkhud->openAlignStick();
        break;

    case LAYOUT:
        // Todo: smarter incrementing of tile count
        settings->userTiles.count++;

        if (settings->userTiles.count == 3) // Skip 3 tiles: not done yet
            settings->userTiles.count++;

        if (settings->userTiles.count > settings->userTiles.maxCount) // Loop around if tile count now too high
            settings->userTiles.count = 1;

        inkhud->updateLayout();
        break;

    case TOGGLE_APPLET:
        if (item.checkState) {
            *item.checkState = !(*item.checkState);
            inkhud->updateAppletSelection();
        }
        break;

    case TOGGLE_AUTOSHOW_APPLET:
        // Toggle settings.userApplets.autoshow[] value, via MenuItem::checkState pointer set in populateAutoshowPage()
        if (item.checkState) {
            *item.checkState = !(*item.checkState);
        }
        break;

    case TOGGLE_NOTIFICATIONS:
        if (item.checkState) {
            *item.checkState = !(*item.checkState);
        }
        break;

    case TOGGLE_INVERT_COLOR:
        if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED)
            config.display.displaymode = meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT;
        else
            config.display.displaymode = meshtastic_Config_DisplayConfig_DisplayMode_INVERTED;

        nodeDB->saveToDisk(SEGMENT_CONFIG);
        break;

    case SET_RECENTS: {
        // cursor - 1 because index 0 is "Back"
        const uint8_t index = cursor - 1;
        constexpr uint8_t optionCount = sizeof(RECENTS_OPTIONS_MINUTES) / sizeof(RECENTS_OPTIONS_MINUTES[0]);
        assert(index < optionCount);
        settings->recentlyActiveSeconds = RECENTS_OPTIONS_MINUTES[index] * 60;
        break;
    }

    case SHUTDOWN:
        LOG_INFO("Shutting down from menu");
        shutdownAtMsec = millis();
        // Menu is then sent to background via onShutdown
        break;

    case TOGGLE_BATTERY_ICON:
        inkhud->toggleBatteryIcon();
        break;

    case TOGGLE_BACKLIGHT:
        // Note: backlight is already on in this situation
        // We're marking that it should *remain* on once menu closes
        assert(backlight);
        if (backlight->isLatched())
            backlight->off();
        else
            backlight->latch();
        break;

    case TOGGLE_12H_CLOCK:
        config.display.use_12h_clock = !config.display.use_12h_clock;
        nodeDB->saveToDisk(SEGMENT_CONFIG);
        break;

    case TOGGLE_GPS:
#if !MESHTASTIC_EXCLUDE_GPS && HAS_GPS
        if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_DISABLED) {
            config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_ENABLED;
        } else if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
            config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_DISABLED;
        } else {
            // NOT_PRESENT do nothing
            break;
        }
        nodeDB->saveToDisk(SEGMENT_CONFIG);
        service->reloadConfig(SEGMENT_CONFIG);
#endif
        break;

    case ENABLE_BLUETOOTH:
        // This helps users recover from a bad wifi config
        LOG_INFO("Enabling Bluetooth");
        config.network.wifi_enabled = false;
        config.bluetooth.enabled = true;
        nodeDB->saveToDisk(SEGMENT_CONFIG);
        InkHUD::InkHUD::getInstance()->notifyApplyingChanges();
        rebootAtMsec = millis() + 2000;
        break;

        // Power / Network (ESP32-only)
#if defined(ARCH_ESP32)
    case TOGGLE_POWER_SAVE:
        config.power.is_power_saving = !config.power.is_power_saving;
        nodeDB->saveToDisk(SEGMENT_CONFIG);
        InkHUD::InkHUD::getInstance()->notifyApplyingChanges();
        rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
        break;

    case TOGGLE_WIFI:
        config.network.wifi_enabled = !config.network.wifi_enabled;

        if (config.network.wifi_enabled) {
            // Switch behavior: WiFi ON forces Bluetooth OFF
            config.bluetooth.enabled = false;
        }

        nodeDB->saveToDisk(SEGMENT_CONFIG);
        InkHUD::InkHUD::getInstance()->notifyApplyingChanges();
        rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
        break;
#endif
    // ADC Calibration
    case CALIBRATE_ADC: {
        // Read current measured voltage
        float measuredV = powerStatus->getBatteryVoltageMv() / 1000.0f;

        // Sanity check
        if (measuredV < 3.0f || measuredV > 4.5f) {
            LOG_WARN("ADC calibration aborted, unreasonable voltage: %.2fV", measuredV);
            break;
        }

        // Determine the base multiplier currently in effect
        float baseMult = 0.0f;

        if (config.power.adc_multiplier_override > 0.0f) {
            baseMult = config.power.adc_multiplier_override;
        }
#ifdef ADC_MULTIPLIER
        else {
            baseMult = ADC_MULTIPLIER;
        }
#endif

        if (baseMult <= 0.0f) {
            LOG_WARN("ADC calibration failed: no base multiplier");
            break;
        }

        // Target voltage considered 100% by UI
        constexpr float TARGET_VOLTAGE = 4.19f;

        // Calculate new multiplier
        float newMult = baseMult * (TARGET_VOLTAGE / measuredV);

        config.power.adc_multiplier_override = newMult;

        nodeDB->saveToDisk(SEGMENT_CONFIG);

        LOG_INFO("ADC calibrated: measured=%.3fV base=%.4f new=%.4f", measuredV, baseMult, newMult);

        break;
    }

    // Display
    case TOGGLE_DISPLAY_UNITS:
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL)
            config.display.units = meshtastic_Config_DisplayConfig_DisplayUnits_METRIC;
        else
            config.display.units = meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL;

        nodeDB->saveToDisk(SEGMENT_CONFIG);
        break;

    // Bluetooth
    case TOGGLE_BLUETOOTH:
        config.bluetooth.enabled = !config.bluetooth.enabled;

        if (config.bluetooth.enabled) {
            // Switch behavior: Bluetooth ON forces WiFi OFF
            config.network.wifi_enabled = false;
        }

        nodeDB->saveToDisk(SEGMENT_CONFIG);
        InkHUD::InkHUD::getInstance()->notifyApplyingChanges();
        rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
        break;

    case TOGGLE_BLUETOOTH_PAIR_MODE:
        config.bluetooth.fixed_pin = !config.bluetooth.fixed_pin;
        nodeDB->saveToDisk(SEGMENT_CONFIG);
        break;

    // Regions
    case SET_REGION_US:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_US);
        break;

    case SET_REGION_EU_868:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_868);
        break;

    case SET_REGION_EU_433:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_433);
        break;

    case SET_REGION_CN:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_CN);
        break;

    case SET_REGION_JP:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_JP);
        break;

    case SET_REGION_ANZ:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_ANZ);
        break;
    case SET_REGION_KR:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_KR);
        break;

    case SET_REGION_TW:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_TW);
        break;

    case SET_REGION_RU:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_RU);
        break;

    case SET_REGION_IN:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_IN);
        break;

    case SET_REGION_NZ_865:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_NZ_865);
        break;

    case SET_REGION_TH:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_TH);
        break;

    case SET_REGION_LORA_24:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_LORA_24);
        break;

    case SET_REGION_UA_433:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_UA_433);
        break;

    case SET_REGION_UA_868:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_UA_868);
        break;

    case SET_REGION_MY_433:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_MY_433);
        break;

    case SET_REGION_MY_919:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_MY_919);
        break;

    case SET_REGION_SG_923:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_SG_923);
        break;

    case SET_REGION_PH_433:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_PH_433);
        break;

    case SET_REGION_PH_868:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_PH_868);
        break;

    case SET_REGION_PH_915:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_PH_915);
        break;

    case SET_REGION_ANZ_433:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_ANZ_433);
        break;

    case SET_REGION_KZ_433:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_KZ_433);
        break;

    case SET_REGION_KZ_863:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_KZ_863);
        break;

    case SET_REGION_NP_865:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_NP_865);
        break;

    case SET_REGION_BR_902:
        applyLoRaRegion(meshtastic_Config_LoRaConfig_RegionCode_BR_902);
        break;

    // Roles
    case SET_ROLE_CLIENT:
        applyDeviceRole(meshtastic_Config_DeviceConfig_Role_CLIENT);
        break;

    case SET_ROLE_CLIENT_MUTE:
        applyDeviceRole(meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE);
        break;

    case SET_ROLE_ROUTER:
        applyDeviceRole(meshtastic_Config_DeviceConfig_Role_ROUTER);
        break;

    case SET_ROLE_REPEATER:
        applyDeviceRole(meshtastic_Config_DeviceConfig_Role_REPEATER);
        break;

    // Presets
    case SET_PRESET_LONG_SLOW:
        applyLoRaPreset(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW);
        break;

    case SET_PRESET_LONG_MODERATE:
        applyLoRaPreset(meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE);
        break;

    case SET_PRESET_LONG_FAST:
        applyLoRaPreset(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);
        break;

    case SET_PRESET_MEDIUM_SLOW:
        applyLoRaPreset(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW);
        break;

    case SET_PRESET_MEDIUM_FAST:
        applyLoRaPreset(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST);
        break;

    case SET_PRESET_SHORT_SLOW:
        applyLoRaPreset(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW);
        break;

    case SET_PRESET_SHORT_FAST:
        applyLoRaPreset(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST);
        break;

    case SET_PRESET_SHORT_TURBO:
        applyLoRaPreset(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO);
        break;

    // Timezones
    case SET_TZ_US_HAWAII:
        applyTimezone("HST10");
        break;

    case SET_TZ_US_ALASKA:
        applyTimezone("AKST9AKDT,M3.2.0,M11.1.0");
        break;

    case SET_TZ_US_PACIFIC:
        applyTimezone("PST8PDT,M3.2.0,M11.1.0");
        break;

    case SET_TZ_US_ARIZONA:
        applyTimezone("MST7");
        break;

    case SET_TZ_US_MOUNTAIN:
        applyTimezone("MST7MDT,M3.2.0,M11.1.0");
        break;

    case SET_TZ_US_CENTRAL:
        applyTimezone("CST6CDT,M3.2.0,M11.1.0");
        break;

    case SET_TZ_US_EASTERN:
        applyTimezone("EST5EDT,M3.2.0,M11.1.0");
        break;

    case SET_TZ_BR_BRAZILIA:
        applyTimezone("BRT3");
        break;

    case SET_TZ_UTC:
        applyTimezone("UTC0");
        break;

    case SET_TZ_EU_WESTERN:
        applyTimezone("GMT0BST,M3.5.0/1,M10.5.0");
        break;

    case SET_TZ_EU_CENTRAL:
        applyTimezone("CET-1CEST,M3.5.0,M10.5.0/3");
        break;

    case SET_TZ_EU_EASTERN:
        applyTimezone("EET-2EEST,M3.5.0/3,M10.5.0/4");
        break;

    case SET_TZ_ASIA_KOLKATA:
        applyTimezone("IST-5:30");
        break;

    case SET_TZ_ASIA_HONG_KONG:
        applyTimezone("HKT-8");
        break;

    case SET_TZ_AU_AWST:
        applyTimezone("AWST-8");
        break;

    case SET_TZ_AU_ACST:
        applyTimezone("ACST-9:30ACDT,M10.1.0,M4.1.0/3");
        break;

    case SET_TZ_AU_AEST:
        applyTimezone("AEST-10AEDT,M10.1.0,M4.1.0/3");
        break;

    case SET_TZ_PACIFIC_NZ:
        applyTimezone("NZST-12NZDT,M9.5.0,M4.1.0/3");
        break;

    // Channels
    case TOGGLE_CHANNEL_UPLINK: {
        auto &ch = channels.getByIndex(selectedChannelIndex);
        ch.settings.uplink_enabled = !ch.settings.uplink_enabled;
        nodeDB->saveToDisk(SEGMENT_CHANNELS);
        service->reloadConfig(SEGMENT_CHANNELS);
        break;
    }

    case TOGGLE_CHANNEL_DOWNLINK: {
        auto &ch = channels.getByIndex(selectedChannelIndex);
        ch.settings.downlink_enabled = !ch.settings.downlink_enabled;
        nodeDB->saveToDisk(SEGMENT_CHANNELS);
        service->reloadConfig(SEGMENT_CHANNELS);
        break;
    }

    case TOGGLE_CHANNEL_POSITION: {
        auto &ch = channels.getByIndex(selectedChannelIndex);

        if (!ch.settings.has_module_settings)
            ch.settings.has_module_settings = true;

        if (ch.settings.module_settings.position_precision > 0)
            ch.settings.module_settings.position_precision = 0;
        else
            ch.settings.module_settings.position_precision = 13; // default

        nodeDB->saveToDisk(SEGMENT_CHANNELS);
        service->reloadConfig(SEGMENT_CHANNELS);
        break;
    }

    case SET_CHANNEL_PRECISION: {
        auto &ch = channels.getByIndex(selectedChannelIndex);

        if (!ch.settings.has_module_settings)
            ch.settings.has_module_settings = true;

        // Cursor - 1 because of "Back"
        uint8_t index = cursor - 1;

        constexpr uint8_t optionCount = sizeof(POSITION_PRECISION_OPTIONS) / sizeof(POSITION_PRECISION_OPTIONS[0]);

        if (index < optionCount) {
            ch.settings.module_settings.position_precision = POSITION_PRECISION_OPTIONS[index].value;
        }

        nodeDB->saveToDisk(SEGMENT_CHANNELS);
        service->reloadConfig(SEGMENT_CHANNELS);
        break;
    }

    case RESET_NODEDB_ALL:
        InkHUD::getInstance()->notifyApplyingChanges();
        nodeDB->resetNodes();
        rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
        break;

    case RESET_NODEDB_KEEP_FAVORITES:
        InkHUD::getInstance()->notifyApplyingChanges();
        nodeDB->resetNodes(1);
        rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
        break;

    default:
        LOG_WARN("Action not implemented");
    }

    // Move to next page, as defined for the MenuItem
    showPage(item.nextPage);
}

// Display a new page of MenuItems
// May reload same page, or exit menu applet entirely
// Fills the MenuApplet::items vector
void InkHUD::MenuApplet::showPage(MenuPage page)
{
    items.clear();
    items.shrink_to_fit();
    nodeConfigLabels.clear();

    switch (page) {
    case ROOT:
        previousPage = MenuPage::EXIT;
        // Optional: next applet
        if (settings->optionalMenuItems.nextTile && settings->userTiles.count > 1)
            items.push_back(MenuItem("Next Tile", MenuAction::NEXT_TILE, MenuPage::ROOT)); // Only if multiple applets shown

        items.push_back(MenuItem("Send", MenuPage::SEND));
        items.push_back(MenuItem("Options", MenuPage::OPTIONS));
        // items.push_back(MenuItem("Display Off", MenuPage::EXIT)); // TODO
        items.push_back(MenuItem("Node Config", MenuPage::NODE_CONFIG));
        items.push_back(MenuItem("Save & Shut Down", MenuAction::SHUTDOWN));
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;

    case SEND:
        populateSendPage();
        previousPage = MenuPage::ROOT;
        break;

    case CANNEDMESSAGE_RECIPIENT:
        populateRecipientPage();
        previousPage = MenuPage::SEND;
        break;

    case OPTIONS:
        previousPage = MenuPage::ROOT;
        items.push_back(MenuItem("Back", previousPage));
        // Optional: backlight
        if (settings->optionalMenuItems.backlight)
            items.push_back(MenuItem(backlight->isLatched() ? "Backlight Off" : "Keep Backlight On", // Label
                                     MenuAction::TOGGLE_BACKLIGHT,                                   // Action
                                     MenuPage::EXIT                                                  // Exit once complete
                                     ));

        // Options Toggles
        items.push_back(MenuItem("Applets", MenuPage::APPLETS));
        items.push_back(MenuItem("Auto-show", MenuPage::AUTOSHOW));
        items.push_back(MenuItem("Recents Duration", MenuPage::RECENTS));
        if (settings->userTiles.maxCount > 1)
            items.push_back(MenuItem("Layout", MenuAction::LAYOUT, MenuPage::OPTIONS));
        items.push_back(MenuItem("Rotate", MenuAction::ROTATE, MenuPage::OPTIONS));
        if (settings->joystick.enabled)
            items.push_back(MenuItem("Align Joystick", MenuAction::ALIGN_JOYSTICK, MenuPage::EXIT));
        items.push_back(MenuItem("Notifications", MenuAction::TOGGLE_NOTIFICATIONS, MenuPage::OPTIONS,
                                 &settings->optionalFeatures.notifications));
        items.push_back(MenuItem("Battery Icon", MenuAction::TOGGLE_BATTERY_ICON, MenuPage::OPTIONS,
                                 &settings->optionalFeatures.batteryIcon));
        invertedColors = (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED);
        items.push_back(MenuItem("Invert Color", MenuAction::TOGGLE_INVERT_COLOR, MenuPage::OPTIONS, &invertedColors));
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;

    case APPLETS:
        previousPage = MenuPage::OPTIONS;
        populateAppletPage(); // must be first
        items.insert(items.begin(), MenuItem("Back", previousPage));
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;

    case AUTOSHOW:
        previousPage = MenuPage::OPTIONS;
        populateAutoshowPage(); // must be first
        items.insert(items.begin(), MenuItem("Back", previousPage));
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;

    case RECENTS:
        previousPage = MenuPage::OPTIONS;
        populateRecentsPage(); // builds only the options
        items.insert(items.begin(), MenuItem("Back", previousPage));
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;

    case NODE_CONFIG:
        previousPage = MenuPage::ROOT;
        items.push_back(MenuItem("Back", previousPage));
        // Radio Config Section
        items.push_back(MenuItem::Header("Radio Config"));
        items.push_back(MenuItem("LoRa", MenuPage::NODE_CONFIG_LORA));
        items.push_back(MenuItem("Channel", MenuPage::NODE_CONFIG_CHANNELS));
        // Device Config Section
        items.push_back(MenuItem::Header("Device Config"));
        items.push_back(MenuItem("Device", MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("Position", MenuPage::NODE_CONFIG_POSITION));
        items.push_back(MenuItem("Power", MenuPage::NODE_CONFIG_POWER));
#if defined(ARCH_ESP32)
        items.push_back(MenuItem("Network", MenuPage::NODE_CONFIG_NETWORK));
#endif
        items.push_back(MenuItem("Display", MenuPage::NODE_CONFIG_DISPLAY));
        items.push_back(MenuItem("Bluetooth", MenuPage::NODE_CONFIG_BLUETOOTH));

        // Administration Section
        items.push_back(MenuItem::Header("Administration"));
        items.push_back(MenuItem("Reset NodeDB", MenuPage::NODE_CONFIG_ADMIN_RESET));

        // Exit
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;

    case NODE_CONFIG_DEVICE: {
        previousPage = MenuPage::NODE_CONFIG;
        items.push_back(MenuItem("Back", previousPage));

        const char *role = DisplayFormatters::getDeviceRole(config.device.role);
        nodeConfigLabels.emplace_back("Role: " + std::string(role));
        items.push_back(MenuItem(nodeConfigLabels.back().c_str(), MenuAction::NO_ACTION, MenuPage::NODE_CONFIG_DEVICE_ROLE));

        const char *tzLabel = getTimezoneLabelFromValue(config.device.tzdef);
        nodeConfigLabels.emplace_back("Timezone: " + std::string(tzLabel));
        items.push_back(MenuItem(nodeConfigLabels.back().c_str(), MenuAction::NO_ACTION, MenuPage::TIMEZONE));

        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;
    }

    case NODE_CONFIG_POSITION: {
        previousPage = MenuPage::NODE_CONFIG;
        items.push_back(MenuItem("Back", previousPage));
#if !MESHTASTIC_EXCLUDE_GPS && HAS_GPS
        const auto mode = config.position.gps_mode;
        if (mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT) {
            items.push_back(MenuItem("GPS None", MenuAction::NO_ACTION, MenuPage::NODE_CONFIG_POSITION));
        } else {
            gpsEnabled = (mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED);
            items.push_back(MenuItem("GPS", MenuAction::TOGGLE_GPS, MenuPage::NODE_CONFIG_POSITION, &gpsEnabled));
        }
#endif
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;
    }

    case NODE_CONFIG_POWER: {
        previousPage = MenuPage::NODE_CONFIG;
        items.push_back(MenuItem("Back", previousPage));
#if defined(ARCH_ESP32)
        items.push_back(MenuItem("Powersave", MenuAction::TOGGLE_POWER_SAVE, MenuPage::EXIT, &config.power.is_power_saving));
#endif
        // ADC Multiplier
        float effectiveMult = 0.0f;

        // User override always shows if it exists
        if (config.power.adc_multiplier_override > 0.0f) {
            effectiveMult = config.power.adc_multiplier_override;
        }
#ifdef ADC_MULTIPLIER
        else {
            // Fallback to variant defined
            effectiveMult = ADC_MULTIPLIER;
        }
#endif

        // Only show if we actually have a value
        if (effectiveMult > 0.0f) {
            char buf[32];
            snprintf(buf, sizeof(buf), "ADC Mult: %.3f", effectiveMult);
            nodeConfigLabels.emplace_back(buf);

            items.push_back(
                MenuItem(nodeConfigLabels.back().c_str(), MenuAction::NO_ACTION, MenuPage::NODE_CONFIG_POWER_ADC_CAL));
        }

        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;
    }

    case NODE_CONFIG_POWER_ADC_CAL: {
        previousPage = MenuPage::NODE_CONFIG_POWER;
        items.push_back(MenuItem("Back", previousPage));

        // Instruction text (header-style, non-selectable)
        items.push_back(MenuItem::Header("Run on full charge Only"));

        // Action
        items.push_back(MenuItem("Calibrate ADC", MenuAction::CALIBRATE_ADC, MenuPage::NODE_CONFIG_POWER));

        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;
    }

    case NODE_CONFIG_NETWORK: {
        previousPage = MenuPage::NODE_CONFIG;
        items.push_back(MenuItem("Back", previousPage));

        const char *wifiLabel = config.network.wifi_enabled ? "WiFi: On" : "WiFi: Off";

        items.push_back(MenuItem(wifiLabel, MenuAction::TOGGLE_WIFI, MenuPage::EXIT));

#if HAS_WIFI && defined(ARCH_ESP32)
        if (config.network.wifi_enabled) {

            // Status
            if (WiFi.status() == WL_CONNECTED) {
                nodeConfigLabels.emplace_back("Status: Connected");
            } else {
                nodeConfigLabels.emplace_back("Status: Not Connected");
            }
            items.push_back(MenuItem(nodeConfigLabels.back().c_str(), MenuAction::NO_ACTION, MenuPage::NODE_CONFIG_NETWORK));

            // Signal
            if (WiFi.status() == WL_CONNECTED) {
                int rssi = WiFi.RSSI();
                int quality = constrain(2 * (rssi + 100), 0, 100);

                char sigBuf[32];
                snprintf(sigBuf, sizeof(sigBuf), "Signal: %d%%", quality);
                nodeConfigLabels.emplace_back(sigBuf);
                items.push_back(MenuItem(nodeConfigLabels.back().c_str(), MenuAction::NO_ACTION, MenuPage::NODE_CONFIG_NETWORK));

                char ipBuf[64];
                snprintf(ipBuf, sizeof(ipBuf), "IP: %s", WiFi.localIP().toString().c_str());
                nodeConfigLabels.emplace_back(ipBuf);
                items.push_back(MenuItem(nodeConfigLabels.back().c_str(), MenuAction::NO_ACTION, MenuPage::NODE_CONFIG_NETWORK));
            }

            // SSID
            if (config.network.wifi_ssid && strlen(config.network.wifi_ssid) > 0) {
                std::string ssidLabel = "SSID: ";
                ssidLabel += config.network.wifi_ssid;
                nodeConfigLabels.emplace_back(ssidLabel);
                items.push_back(MenuItem(nodeConfigLabels.back().c_str(), MenuAction::NO_ACTION, MenuPage::NODE_CONFIG_NETWORK));
            }

            // Hostname
            const char *host = WiFi.getHostname();
            if (host && strlen(host) > 0) {
                std::string hostLabel = "Host: ";
                hostLabel += host;
                nodeConfigLabels.emplace_back(hostLabel);
                items.push_back(MenuItem(nodeConfigLabels.back().c_str(), MenuAction::NO_ACTION, MenuPage::NODE_CONFIG_NETWORK));
            }
        }
#endif

        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;
    }

    case NODE_CONFIG_DISPLAY: {
        previousPage = MenuPage::NODE_CONFIG;
        items.push_back(MenuItem("Back", previousPage));

        items.push_back(MenuItem("12-Hour Clock", MenuAction::TOGGLE_12H_CLOCK, MenuPage::NODE_CONFIG_DISPLAY,
                                 &config.display.use_12h_clock));

        const char *unitsLabel =
            (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) ? "Units: Imperial" : "Units: Metric";

        items.push_back(MenuItem(unitsLabel, MenuAction::TOGGLE_DISPLAY_UNITS, MenuPage::NODE_CONFIG_DISPLAY));

        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;
    }

    case NODE_CONFIG_BLUETOOTH: {
        previousPage = MenuPage::NODE_CONFIG;
        items.push_back(MenuItem("Back", previousPage));

        const char *btLabel = config.bluetooth.enabled ? "Bluetooth: On" : "Bluetooth: Off";
        items.push_back(MenuItem(btLabel, MenuAction::TOGGLE_BLUETOOTH, MenuPage::EXIT));

        const char *pairLabel = config.bluetooth.fixed_pin ? "Pair Mode: Fixed" : "Pair Mode: Random";
        items.push_back(MenuItem(pairLabel, MenuAction::TOGGLE_BLUETOOTH_PAIR_MODE, MenuPage::NODE_CONFIG_BLUETOOTH));

        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;
    }

    case NODE_CONFIG_LORA: {
        previousPage = MenuPage::NODE_CONFIG;
        items.push_back(MenuItem("Back", previousPage));

        const char *region = myRegion ? myRegion->name : "Unset";
        nodeConfigLabels.emplace_back("Region: " + std::string(region));
        items.push_back(MenuItem(nodeConfigLabels.back().c_str(), MenuAction::NO_ACTION, MenuPage::REGION));

        const char *preset =
            DisplayFormatters::getModemPresetDisplayName(config.lora.modem_preset, false, config.lora.use_preset);
        nodeConfigLabels.emplace_back("Preset: " + std::string(preset));
        items.push_back(MenuItem(nodeConfigLabels.back().c_str(), MenuAction::NO_ACTION, MenuPage::NODE_CONFIG_PRESET));

        char freqBuf[32];
        float freq = RadioLibInterface::instance->getFreq();
        snprintf(freqBuf, sizeof(freqBuf), "Freq: %.3f MHz", freq);
        nodeConfigLabels.emplace_back(freqBuf);
        items.push_back(MenuItem(nodeConfigLabels.back().c_str(), MenuAction::NO_ACTION, MenuPage::NODE_CONFIG_LORA));

        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;
    }

    case NODE_CONFIG_CHANNELS: {
        previousPage = MenuPage::NODE_CONFIG;
        items.push_back(MenuItem("Back", previousPage));

        for (uint8_t i = 0; i < MAX_NUM_CHANNELS; i++) {
            meshtastic_Channel &ch = channels.getByIndex(i);

            if (!ch.has_settings)
                continue;

            if (ch.role == meshtastic_Channel_Role_DISABLED)
                continue;

            std::string label = "#";

            if (ch.role == meshtastic_Channel_Role_PRIMARY) {
                label += "Primary";
            } else if (strlen(ch.settings.name) > 0) {
                label += parse(ch.settings.name);
            } else {
                label += "Channel" + to_string(i + 1);
            }

            nodeConfigLabels.push_back(label);
            items.push_back(
                MenuItem(nodeConfigLabels.back().c_str(), MenuAction::NO_ACTION, MenuPage::NODE_CONFIG_CHANNEL_DETAIL));
        }

        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;
    }

    case NODE_CONFIG_CHANNEL_DETAIL: {
        previousPage = MenuPage::NODE_CONFIG_CHANNELS;
        items.push_back(MenuItem("Back", previousPage));

        meshtastic_Channel &ch = channels.getByIndex(selectedChannelIndex);

        // Name (read-only)
        const char *name = strlen(ch.settings.name) > 0 ? ch.settings.name : "Unnamed";
        nodeConfigLabels.emplace_back("Ch: " + parse(name));
        items.push_back(MenuItem(nodeConfigLabels.back().c_str(), MenuAction::NO_ACTION, MenuPage::NODE_CONFIG_CHANNEL_DETAIL));

        // Uplink
        items.push_back(MenuItem("Uplink", MenuAction::TOGGLE_CHANNEL_UPLINK, MenuPage::NODE_CONFIG_CHANNEL_DETAIL,
                                 &ch.settings.uplink_enabled));

        items.push_back(MenuItem("Downlink", MenuAction::TOGGLE_CHANNEL_DOWNLINK, MenuPage::NODE_CONFIG_CHANNEL_DETAIL,
                                 &ch.settings.downlink_enabled));

        // Position
        channelPositionEnabled = ch.settings.has_module_settings && ch.settings.module_settings.position_precision > 0;

        items.push_back(MenuItem("Position", MenuAction::TOGGLE_CHANNEL_POSITION, MenuPage::NODE_CONFIG_CHANNEL_DETAIL,
                                 &channelPositionEnabled));

        // Precision
        if (channelPositionEnabled) {

            std::string precisionLabel = "Unknown";

            for (const auto &opt : POSITION_PRECISION_OPTIONS) {
                if (opt.value == ch.settings.module_settings.position_precision) {
                    precisionLabel = (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL)
                                         ? opt.imperial
                                         : opt.metric;
                    break;
                }
            }
            nodeConfigLabels.emplace_back("Precision: " + precisionLabel);
            items.push_back(
                MenuItem(nodeConfigLabels.back().c_str(), MenuAction::NO_ACTION, MenuPage::NODE_CONFIG_CHANNEL_PRECISION));
        }

        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;
    }

    case NODE_CONFIG_CHANNEL_PRECISION: {
        previousPage = MenuPage::NODE_CONFIG_CHANNEL_DETAIL;
        items.push_back(MenuItem("Back", previousPage));
        meshtastic_Channel &ch = channels.getByIndex(selectedChannelIndex);
        if (!ch.settings.has_module_settings || ch.settings.module_settings.position_precision == 0) {
            items.push_back(MenuItem("Position is Off", MenuPage::NODE_CONFIG_CHANNEL_DETAIL));
            break;
        }
        constexpr uint8_t optionCount = sizeof(POSITION_PRECISION_OPTIONS) / sizeof(POSITION_PRECISION_OPTIONS[0]);
        for (uint8_t i = 0; i < optionCount; i++) {
            const auto &opt = POSITION_PRECISION_OPTIONS[i];
            const char *label =
                (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) ? opt.imperial : opt.metric;
            nodeConfigLabels.emplace_back(label);

            items.push_back(MenuItem(nodeConfigLabels.back().c_str(), MenuAction::SET_CHANNEL_PRECISION,
                                     MenuPage::NODE_CONFIG_CHANNEL_DETAIL));
        }
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;
    }

    case NODE_CONFIG_DEVICE_ROLE: {
        previousPage = MenuPage::NODE_CONFIG_DEVICE;
        items.push_back(MenuItem("Back", previousPage));
        items.push_back(MenuItem("Client", MenuAction::SET_ROLE_CLIENT, MenuPage::EXIT));
        items.push_back(MenuItem("Client Mute", MenuAction::SET_ROLE_CLIENT_MUTE, MenuPage::EXIT));
        items.push_back(MenuItem("Router", MenuAction::SET_ROLE_ROUTER, MenuPage::EXIT));
        items.push_back(MenuItem("Repeater", MenuAction::SET_ROLE_REPEATER, MenuPage::EXIT));
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;
    }

    case TIMEZONE:
        previousPage = MenuPage::NODE_CONFIG_DEVICE;
        items.push_back(MenuItem("Back", previousPage));
        items.push_back(MenuItem("US/Hawaii", SET_TZ_US_HAWAII, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("US/Alaska", SET_TZ_US_ALASKA, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("US/Pacific", SET_TZ_US_PACIFIC, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("US/Arizona", SET_TZ_US_ARIZONA, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("US/Mountain", SET_TZ_US_MOUNTAIN, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("US/Central", SET_TZ_US_CENTRAL, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("US/Eastern", SET_TZ_US_EASTERN, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("BR/Brasilia", SET_TZ_BR_BRAZILIA, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("UTC", SET_TZ_UTC, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("EU/Western", SET_TZ_EU_WESTERN, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("EU/Central", SET_TZ_EU_CENTRAL, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("EU/Eastern", SET_TZ_EU_EASTERN, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("Asia/Kolkata", SET_TZ_ASIA_KOLKATA, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("Asia/Hong Kong", SET_TZ_ASIA_HONG_KONG, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("AU/AWST", SET_TZ_AU_AWST, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("AU/ACST", SET_TZ_AU_ACST, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("AU/AEST", SET_TZ_AU_AEST, MenuPage::NODE_CONFIG_DEVICE));
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;

    case REGION:
        previousPage = MenuPage::NODE_CONFIG_LORA;
        items.push_back(MenuItem("Back", previousPage));
        items.push_back(MenuItem("US", MenuAction::SET_REGION_US, MenuPage::EXIT));
        items.push_back(MenuItem("EU 868", MenuAction::SET_REGION_EU_868, MenuPage::EXIT));
        items.push_back(MenuItem("EU 433", MenuAction::SET_REGION_EU_433, MenuPage::EXIT));
        items.push_back(MenuItem("CN", MenuAction::SET_REGION_CN, MenuPage::EXIT));
        items.push_back(MenuItem("JP", MenuAction::SET_REGION_JP, MenuPage::EXIT));
        items.push_back(MenuItem("ANZ", MenuAction::SET_REGION_ANZ, MenuPage::EXIT));
        items.push_back(MenuItem("KR", MenuAction::SET_REGION_KR, MenuPage::EXIT));
        items.push_back(MenuItem("TW", MenuAction::SET_REGION_TW, MenuPage::EXIT));
        items.push_back(MenuItem("RU", MenuAction::SET_REGION_RU, MenuPage::EXIT));
        items.push_back(MenuItem("IN", MenuAction::SET_REGION_IN, MenuPage::EXIT));
        items.push_back(MenuItem("NZ 865", MenuAction::SET_REGION_NZ_865, MenuPage::EXIT));
        items.push_back(MenuItem("TH", MenuAction::SET_REGION_TH, MenuPage::EXIT));
        items.push_back(MenuItem("LoRa 2.4", MenuAction::SET_REGION_LORA_24, MenuPage::EXIT));
        items.push_back(MenuItem("UA 433", MenuAction::SET_REGION_UA_433, MenuPage::EXIT));
        items.push_back(MenuItem("UA 868", MenuAction::SET_REGION_UA_868, MenuPage::EXIT));
        items.push_back(MenuItem("MY 433", MenuAction::SET_REGION_MY_433, MenuPage::EXIT));
        items.push_back(MenuItem("MY 919", MenuAction::SET_REGION_MY_919, MenuPage::EXIT));
        items.push_back(MenuItem("SG 923", MenuAction::SET_REGION_SG_923, MenuPage::EXIT));
        items.push_back(MenuItem("PH 433", MenuAction::SET_REGION_PH_433, MenuPage::EXIT));
        items.push_back(MenuItem("PH 868", MenuAction::SET_REGION_PH_868, MenuPage::EXIT));
        items.push_back(MenuItem("PH 915", MenuAction::SET_REGION_PH_915, MenuPage::EXIT));
        items.push_back(MenuItem("ANZ 433", MenuAction::SET_REGION_ANZ_433, MenuPage::EXIT));
        items.push_back(MenuItem("KZ 433", MenuAction::SET_REGION_KZ_433, MenuPage::EXIT));
        items.push_back(MenuItem("KZ 863", MenuAction::SET_REGION_KZ_863, MenuPage::EXIT));
        items.push_back(MenuItem("NP 865", MenuAction::SET_REGION_NP_865, MenuPage::EXIT));
        items.push_back(MenuItem("BR 902", MenuAction::SET_REGION_BR_902, MenuPage::EXIT));
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;

    case NODE_CONFIG_PRESET: {
        previousPage = MenuPage::NODE_CONFIG_LORA;
        items.push_back(MenuItem("Back", previousPage));
        items.push_back(MenuItem("Long Moderate", MenuAction::SET_PRESET_LONG_MODERATE, MenuPage::EXIT));
        items.push_back(MenuItem("Long Fast", MenuAction::SET_PRESET_LONG_FAST, MenuPage::EXIT));
        items.push_back(MenuItem("Medium Slow", MenuAction::SET_PRESET_MEDIUM_SLOW, MenuPage::EXIT));
        items.push_back(MenuItem("Medium Fast", MenuAction::SET_PRESET_MEDIUM_FAST, MenuPage::EXIT));
        items.push_back(MenuItem("Short Slow", MenuAction::SET_PRESET_SHORT_SLOW, MenuPage::EXIT));
        items.push_back(MenuItem("Short Fast", MenuAction::SET_PRESET_SHORT_FAST, MenuPage::EXIT));
        items.push_back(MenuItem("Short Turbo", MenuAction::SET_PRESET_SHORT_TURBO, MenuPage::EXIT));
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;
    }
    // Administration Section
    case NODE_CONFIG_ADMIN_RESET:
        previousPage = MenuPage::NODE_CONFIG;
        items.push_back(MenuItem("Back", previousPage));
        items.push_back(MenuItem("Reset All", MenuAction::RESET_NODEDB_ALL, MenuPage::EXIT));
        items.push_back(MenuItem("Keep Favorites Only", MenuAction::RESET_NODEDB_KEEP_FAVORITES, MenuPage::EXIT));
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;

    // Exit
    case EXIT:
        sendToBackground(); // Menu applet dismissed, allow normal behavior to resume
        break;

    default:
        LOG_WARN("Page not implemented");
    }

    // Reset the cursor, unless reloading same page
    // (or now out-of-bounds)
    if (page != currentPage || cursor >= items.size()) {
        cursor = 0;

        // ROOT menu has special handling: unselected at first, to emphasise the system info panel
        if (page == ROOT)
            cursorShown = false;
    }

    // Ensure cursor never rests on a header
    if (cursorShown) {
        while (cursor < items.size() && items.at(cursor).isHeader) {
            cursor++;
        }
        if (cursor >= items.size())
            cursor = 0;
    }

    // Remember which page we are on now
    currentPage = page;
}

void InkHUD::MenuApplet::onRender(bool full)
{
    // Free text mode draws a text input field and skips the normal rendering
    if (freeTextMode) {
        drawInputField(0, fontSmall.lineHeight(), X(1.0), Y(1.0) - fontSmall.lineHeight() - 1, cm.freeTextItem.rawText);
        return;
    }

    if (items.size() == 0)
        LOG_ERROR("Empty Menu");

    // Dimensions for the slots where we will draw menuItems
    const float padding = 0.05;
    const uint16_t itemH = fontSmall.lineHeight() * 1.6;
    const int16_t selectInsetY = 2;
    const int16_t itemW = width() - X(padding) - X(padding);
    const int16_t itemL = X(padding);
    const int16_t itemR = X(1 - padding);
    int16_t itemT = 0; // Top (y px of current slot). Incremented as we draw. Adjusted to fit system info panel on ROOT menu.

    // How many full menuItems will fit on screen
    uint8_t slotCount = (height() - itemT) / itemH;

    // System info panel at the top of the menu
    // =========================================

    uint16_t &siH = systemInfoPanelHeight;                   // System info - height. Calculated at onForeground
    const uint8_t slotsObscured = ceilf(siH / (float)itemH); // How many slots are obscured by system info panel

    // System info - top
    // Remain at 0px, until cursor reaches bottom of screen, then begin to scroll off screen.
    // This is the same behavior we expect from the non-root menus.
    // Implementing this with the systemp panel is slightly annoying though,
    // and required adding the MenuApplet::getSystemInfoPanelHeight method
    int16_t siT;
    if (cursor < slotCount - slotsObscured - 1) // (Minus 1: comparing zero based index with a count)
        siT = 0;
    else
        siT = 0 - ((cursor - (slotCount - slotsObscured - 1)) * itemH);

    // If showing ROOT menu,
    // and the panel isn't yet scrolled off screen top
    if (currentPage == ROOT) {
        drawSystemInfoPanel(0, siT, width()); // Draw the panel.
        itemT = max(siT + siH, 0);            // Offset the first menu entry, so menu starts below the system info panel
    }

    // Draw menu items
    // ===================

    // Which item will be drawn to the top-most slot?
    // Initially, this is the item 0, but may increase once we begin scrolling
    uint8_t firstItem;
    if (cursor < slotCount)
        firstItem = 0;
    else
        firstItem = cursor - (slotCount - 1);

    // Which item will be drawn to the bottom-most slot?
    // This may be beyond the slot-count, to draw a partially off-screen item below the bottom-most slow
    // This may be less than the slot-count, if we are reaching the end of the menuItems
    uint8_t lastItem = min((uint8_t)firstItem + slotCount, (uint8_t)items.size() - 1);

    // -- Loop: draw each (visible) menu item --
    for (uint8_t i = firstItem; i <= lastItem; i++) {

        // Grab the menu item
        MenuItem &item = items.at(i);

        // Vertical center of this slot
        int16_t center = itemT + (itemH / 2);

        // Header (non-selectable section label)
        if (item.isHeader) {
            setFont(fontSmall);

            // Header text (flush left)
            printAt(itemL + X(padding), center, item.label, LEFT, MIDDLE);

            // Subtle underline
            int16_t underlineY = itemT + itemH - 2;
            drawLine(itemL + X(padding), underlineY, itemR - X(padding), underlineY, BLACK);
        } else {
            // Box, if currently selected
            if (cursorShown && i == cursor)
                drawRect(itemL, itemT + selectInsetY, itemW, itemH - (selectInsetY * 2), BLACK);

            // Indented normal item text
            printAt(itemL + X(padding * 2), center, item.label, LEFT, MIDDLE);
        }

        // Checkbox, if relevant
        if (item.checkState) {
            const uint16_t cbWH = fontSmall.lineHeight();  // Checkbox: width / height
            const int16_t cbL = itemR - X(padding) - cbWH; // Checkbox: left
            const int16_t cbT = center - (cbWH / 2);       // Checkbox : top
            // Checkbox ticked
            if (*(item.checkState)) {
                drawRect(cbL, cbT, cbWH, cbWH, BLACK);
                // First point of tick: pen down
                const int16_t t1Y = center;
                const int16_t t1X = cbL + 3;
                // Second point of tick: base
                const int16_t t2Y = center + (cbWH / 2) - 2;
                const int16_t t2X = cbL + (cbWH / 2);
                // Third point of tick: end of tail
                const int16_t t3Y = center - (cbWH / 2) - 2;
                const int16_t t3X = cbL + cbWH + 2;
                // Draw twice: faux bold
                drawLine(t1X, t1Y, t2X, t2Y, BLACK);
                drawLine(t2X, t2Y, t3X, t3Y, BLACK);
                drawLine(t1X + 1, t1Y, t2X + 1, t2Y, BLACK);
                drawLine(t2X + 1, t2Y, t3X + 1, t3Y, BLACK);
            }
            // Checkbox ticked
            else
                drawRect(cbL, cbT, cbWH, cbWH, BLACK);
        }

        // Increment the y value (top) as we go
        itemT += itemH;
    }
}

void InkHUD::MenuApplet::onButtonShortPress()
{
    if (!freeTextMode) {
        // Push the auto-close timer back
        OSThread::setIntervalFromNow(MENU_TIMEOUT_SEC * 1000UL);

        if (!settings->joystick.enabled) {
            if (!cursorShown) {
                cursorShown = true;
                cursor = 0;
            } else {
                do {
                    cursor = (cursor + 1) % items.size();
                } while (items.at(cursor).isHeader);
            }
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        } else {
            if (cursorShown)
                execute(items.at(cursor));
            else
                showPage(MenuPage::EXIT);
            if (!wantsToRender())
                requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        }
    }
}

void InkHUD::MenuApplet::onButtonLongPress()
{
    if (!freeTextMode) {
        // Push the auto-close timer back
        OSThread::setIntervalFromNow(MENU_TIMEOUT_SEC * 1000UL);

        if (cursorShown)
            execute(items.at(cursor));
        else
            showPage(MenuPage::EXIT); // Special case: Peek at root-menu; longpress again to close

        // If we didn't already request a specialized update, when handling a menu action,
        // then perform the usual fast update.
        // FAST keeps things responsive: important because we're dealing with user input
        if (!wantsToRender())
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
    }
}

void InkHUD::MenuApplet::onExitShort()
{
    // Exit the menu
    showPage(MenuPage::EXIT);

    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::MenuApplet::onNavUp()
{
    if (!freeTextMode) {
        OSThread::setIntervalFromNow(MENU_TIMEOUT_SEC * 1000UL);

        if (!cursorShown) {
            cursorShown = true;
            cursor = 0;
        } else {
            do {
                if (cursor == 0)
                    cursor = items.size() - 1;
                else
                    cursor--;
            } while (items.at(cursor).isHeader);
        }

        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
    }
}

void InkHUD::MenuApplet::onNavDown()
{
    if (!freeTextMode) {
        OSThread::setIntervalFromNow(MENU_TIMEOUT_SEC * 1000UL);

        if (!cursorShown) {
            cursorShown = true;
            cursor = 0;
        } else {
            do {
                cursor = (cursor + 1) % items.size();
            } while (items.at(cursor).isHeader);
        }

        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
    }
}

void InkHUD::MenuApplet::onNavLeft()
{
    if (!freeTextMode) {
        OSThread::setIntervalFromNow(MENU_TIMEOUT_SEC * 1000UL);

        // Go to the previous menu page
        showPage(previousPage);
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
    }
}

void InkHUD::MenuApplet::onNavRight()
{
    if (!freeTextMode) {
        OSThread::setIntervalFromNow(MENU_TIMEOUT_SEC * 1000UL);
        if (cursorShown)
            execute(items.at(cursor));
        if (!wantsToRender())
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
    }
}

void InkHUD::MenuApplet::onFreeText(char c)
{
    if (cm.freeTextItem.rawText.length() >= menuTextLimit && c != '\b')
        return;
    if (c == '\b') {
        if (!cm.freeTextItem.rawText.empty())
            cm.freeTextItem.rawText.pop_back();
    } else {
        cm.freeTextItem.rawText += c;
    }
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::MenuApplet::onFreeTextDone()
{
    // Restart the auto-close timeout
    OSThread::setIntervalFromNow(MENU_TIMEOUT_SEC * 1000UL);
    OSThread::enabled = true;

    handleFreeText = false;
    freeTextMode = false;

    if (!cm.freeTextItem.rawText.empty()) {
        cm.selectedMessageItem = &cm.freeTextItem;
        showPage(MenuPage::CANNEDMESSAGE_RECIPIENT);
    }
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::MenuApplet::onFreeTextCancel()
{
    // Restart the auto-close timeout
    OSThread::setIntervalFromNow(MENU_TIMEOUT_SEC * 1000UL);
    OSThread::enabled = true;

    handleFreeText = false;
    freeTextMode = false;

    // Clear the free text message
    cm.freeTextItem.rawText.erase();

    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

// Dynamically create MenuItem entries for activating / deactivating Applets, for the "Applet Selection" submenu
void InkHUD::MenuApplet::populateAppletPage()
{
    assert(items.size() == 0);

    for (uint8_t i = 0; i < inkhud->userApplets.size(); i++) {
        const char *name = inkhud->userApplets.at(i)->name;
        bool *isActive = &(settings->userApplets.active[i]);
        items.push_back(MenuItem(name, MenuAction::TOGGLE_APPLET, MenuPage::APPLETS, isActive));
    }
}

// Dynamically create MenuItem entries for selecting which applets will automatically come to foreground when they have new data
// We only populate this menu page with applets which are actually active
// We use the MenuItem::checkState pointer to toggle the setting in MenuApplet::execute. Bit of a hack, but convenient.
void InkHUD::MenuApplet::populateAutoshowPage()
{
    assert(items.size() == 0);

    for (uint8_t i = 0; i < inkhud->userApplets.size(); i++) {
        // Only add a menu item if applet is active
        if (settings->userApplets.active[i]) {
            const char *name = inkhud->userApplets.at(i)->name;
            bool *isActive = &(settings->userApplets.autoshow[i]);
            items.push_back(MenuItem(name, MenuAction::TOGGLE_AUTOSHOW_APPLET, MenuPage::AUTOSHOW, isActive));
        }
    }
}

// Create MenuItem entries to select our definition of "Recent"
// Controls how long data will remain in any "Recents" flavored applets
void InkHUD::MenuApplet::populateRecentsPage()
{
    // How many values are shown for use to choose from
    constexpr uint8_t optionCount = sizeof(RECENTS_OPTIONS_MINUTES) / sizeof(RECENTS_OPTIONS_MINUTES[0]);

    // Create an entry for each item in RECENTS_OPTIONS_MINUTES array
    // (Defined at top of this file)
    for (uint8_t i = 0; i < optionCount; i++) {
        std::string label = to_string(RECENTS_OPTIONS_MINUTES[i]) + " mins";
        recentsSelected[i] = (settings->recentlyActiveSeconds == RECENTS_OPTIONS_MINUTES[i] * 60);
        items.push_back(MenuItem(label.c_str(), MenuAction::SET_RECENTS, MenuPage::OPTIONS, &recentsSelected[i]));
    }
}

// MenuItem entries for the "send" page
// Dynamically creates menu items based on available canned messages
void InkHUD::MenuApplet::populateSendPage()
{
    // Position / NodeInfo packet
    items.push_back(MenuItem("Ping", MenuAction::SEND_PING, MenuPage::EXIT));

    // If joystick is available, include the Free Text option
    if (settings->joystick.enabled)
        items.push_back(MenuItem("Free Text", MenuAction::FREE_TEXT, MenuPage::SEND));

    // One menu item for each canned message
    uint8_t count = cm.store->size();
    for (uint8_t i = 0; i < count; i++) {
        // Gather the information for this item
        CannedMessages::MessageItem messageItem;
        messageItem.rawText = cm.store->at(i);
        messageItem.label = parse(messageItem.rawText);

        // Store the item (until the menu closes)
        cm.messageItems.push_back(messageItem);

        // Create a menu item
        const char *itemText = cm.messageItems.back().label.c_str();
        items.push_back(MenuItem(itemText, MenuAction::STORE_CANNEDMESSAGE_SELECTION, MenuPage::CANNEDMESSAGE_RECIPIENT));
    }

    items.push_back(MenuItem("Exit", MenuPage::EXIT));
}

// Dynamically create MenuItem entries for possible canned message destinations
// All available channels are shown
// Favorite nodes are shown, provided we don't have an *excessive* amount
void InkHUD::MenuApplet::populateRecipientPage()
{
    // Create recipient data (and menu items) for any channels
    // --------------------------------------------------------

    for (uint8_t i = 0; i < MAX_NUM_CHANNELS; i++) {
        // Get the channel, and check if it's enabled
        meshtastic_Channel &channel = channels.getByIndex(i);
        if (!channel.has_settings || channel.role == meshtastic_Channel_Role_DISABLED)
            continue;

        CannedMessages::RecipientItem r;

        // Set index
        r.channelIndex = channel.index;

        // Set a label for the menu item
        r.label = "Ch " + to_string(i) + ": ";
        if (channel.role == meshtastic_Channel_Role_PRIMARY)
            r.label += "Primary";
        else
            r.label += parse(channel.settings.name);

        // Add to the list of recipients
        cm.recipientItems.push_back(r);

        // Add a menu item for this recipient
        const char *itemText = cm.recipientItems.back().label.c_str();
        items.push_back(MenuItem(itemText, SEND_CANNEDMESSAGE, MenuPage::EXIT));
    }

    // Create recipient data (and menu items) for favorite nodes
    // ---------------------------------------------------------

    uint32_t nodeCount = nodeDB->getNumMeshNodes();
    uint32_t favoriteCount = 0;

    // Count favorites
    for (uint32_t i = 0; i < nodeCount; i++) {
        if (nodeDB->getMeshNodeByIndex(i)->is_favorite)
            favoriteCount++;
    }

    // Only add favorites if the number is reasonable
    // Don't want some monstrous list that takes 100 clicks to reach exit
    if (favoriteCount < 20) {
        for (uint32_t i = 0; i < nodeCount; i++) {
            meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);

            // Skip node if not a favorite
            if (!node->is_favorite)
                continue;

            CannedMessages::RecipientItem r;

            r.dest = node->num;
            r.channelIndex = nodeDB->getMeshNodeChannel(node->num); // Channel index only relevant if encrypted DM not possible(?)

            // Set a label for the menu item
            r.label = "DM: ";
            if (node->has_user)
                r.label += parse(node->user.long_name);
            else
                r.label += hexifyNodeNum(node->num); // Unsure if it's possible to favorite a node without NodeInfo?

            // Add to the list of recipients
            cm.recipientItems.push_back(r);

            // Add a menu item for this recipient
            const char *itemText = cm.recipientItems.back().label.c_str();
            items.push_back(MenuItem(itemText, SEND_CANNEDMESSAGE, MenuPage::EXIT));
        }
    }

    items.push_back(MenuItem("Exit", MenuPage::EXIT));
}

void InkHUD::MenuApplet::drawInputField(uint16_t left, uint16_t top, uint16_t width, uint16_t height, std::string text)
{
    setFont(fontSmall);
    uint16_t wrapMaxH = 0;

    // Draw the text, input box, and cursor
    // Adjusting the box for screen height
    while (wrapMaxH < height - fontSmall.lineHeight()) {
        wrapMaxH += fontSmall.lineHeight();
    }

    // If the text is so long that it goes outside of the input box, the text is actually rendered off screen.
    uint32_t textHeight = getWrappedTextHeight(0, width - 5, text);
    if (!text.empty()) {
        uint16_t textPadding = X(1.0) > Y(1.0) ? wrapMaxH - textHeight : wrapMaxH - textHeight + 1;
        if (textHeight > wrapMaxH)
            printWrapped(2, textPadding, width - 5, text);
        else
            printWrapped(2, top + 2, width - 5, text);
    }

    uint16_t textCursorX = text.empty() ? 1 : getCursorX();
    uint16_t textCursorY = text.empty() ? fontSmall.lineHeight() + 2 : getCursorY() - fontSmall.lineHeight() + 3;

    if (textCursorX + 1 > width - 5) {
        textCursorX = getCursorX() - width + 5;
        textCursorY += fontSmall.lineHeight();
    }

    fillRect(textCursorX + 1, textCursorY, 1, fontSmall.lineHeight(), BLACK);

    // A white rectangle clears the top part of the screen for any text that's printed beyond the input box
    fillRect(0, 0, X(1.0), top, WHITE);

    // Draw character limit
    std::string ftlen = std::to_string(text.length()) + "/" + to_string(menuTextLimit);
    uint16_t textLen = getTextWidth(ftlen);
    printAt(X(1.0) - textLen - 2, 0, ftlen);

    // Draw the border
    drawRect(0, top, width, wrapMaxH + 5, BLACK);
}
// Renders the panel shown at the top of the root menu.
// Displays the clock, and several other pieces of instantaneous system info,
// which we'd prefer not to have displayed in a normal applet, as they update too frequently.
void InkHUD::MenuApplet::drawSystemInfoPanel(int16_t left, int16_t top, uint16_t width, uint16_t *renderedHeight)
{
    // Reset the height
    // We'll add to this as we add elements
    uint16_t height = 0;

    // Clock (potentially)
    // ====================
    std::string clockString = getTimeString();
    if (clockString.length() > 0) {
        setFont(fontMedium);
        printAt(width / 2, top, clockString, CENTER, TOP);

        height += fontMedium.lineHeight();
        height += fontMedium.lineHeight() * 0.1; // Padding below clock
    }

    // Stats
    // ===================

    setFont(fontSmall);

    // Position of the label row for the system info
    const int16_t labelT = top + height;
    height += fontSmall.lineHeight() * 1.1; // Slightly increased spacing

    // Position of the data row for the system info
    const int16_t valT = top + height;
    height += fontSmall.lineHeight() * 1.1; // Slightly increased spacing (between bottom line and divider)

    // Position of divider between the info panel and the menu entries
    const int16_t divY = top + height;
    height += fontSmall.lineHeight() * 0.2; // Padding *below* the divider. (Above first menu item)

    // Create a variable number of columns
    // Either 3 or 4, depending on whether we have GPS
    // Todo
    constexpr uint8_t N_COL = 3;
    int16_t colL[N_COL];
    int16_t colC[N_COL];
    int16_t colR[N_COL];
    for (uint8_t i = 0; i < N_COL; i++) {
        colL[i] = left + ((width / N_COL) * i);
        colC[i] = colL[i] + ((width / N_COL) / 2);
        colR[i] = colL[i] + (width / N_COL);
    }

    // Info blocks, left to right

    // Voltage
    float voltage = powerStatus->getBatteryVoltageMv() / 1000.0;
    char voltageStr[6]; // "XX.XV"
    sprintf(voltageStr, "%.2fV", voltage);
    printAt(colC[0], labelT, "Bat", CENTER, TOP);
    printAt(colC[0], valT, voltageStr, CENTER, TOP);

    // Divider
    for (int16_t y = valT; y <= divY; y += 3)
        drawPixel(colR[0], y, BLACK);

    // Channel Util
    char chUtilStr[4]; // "XX%"
    sprintf(chUtilStr, "%2.f%%", airTime->channelUtilizationPercent());
    printAt(colC[1], labelT, "Ch", CENTER, TOP);
    printAt(colC[1], valT, chUtilStr, CENTER, TOP);

    // Divider
    for (int16_t y = valT; y <= divY; y += 3)
        drawPixel(colR[1], y, BLACK);

    // Duty Cycle (AirTimeTx)
    char dutyUtilStr[4]; // "XX%"
    sprintf(dutyUtilStr, "%2.f%%", airTime->utilizationTXPercent());
    printAt(colC[2], labelT, "Duty", CENTER, TOP);
    printAt(colC[2], valT, dutyUtilStr, CENTER, TOP);

    /*
    // Divider
    for (int16_t y = valT; y <= divY; y += 3)
        drawPixel(colR[2], y, BLACK);

    // GPS satellites - todo
    printAt(colC[3], labelT, "Sats", CENTER, TOP);
    printAt(colC[3], valT, "ToDo", CENTER, TOP);
    */

    // Horizontal divider, at bottom of system info panel
    for (int16_t x = 0; x < width; x += 2) // Divider, centered in the padding between first system panel and first item
        drawPixel(x, divY, BLACK);

    if (renderedHeight != nullptr)
        *renderedHeight = height;
}

// Get the height of the the panel drawn at the top of the menu
// This is inefficient, as we do actually have to render the panel to determine the height
// It solves a catch-22 situation, where slotCount needs to know panel height, and panel height needs to know slotCount
uint16_t InkHUD::MenuApplet::getSystemInfoPanelHeight()
{
    // Render *far* off screen
    uint16_t height = 0;
    drawSystemInfoPanel(INT16_MIN, INT16_MIN, 1, &height);

    return height;
}

// Send a text message to the mesh
// Used to send our canned messages
void InkHUD::MenuApplet::sendText(NodeNum dest, ChannelIndex channel, const char *message)
{
    meshtastic_MeshPacket *p = router->allocForSending();
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->to = dest;
    p->channel = channel;
    p->want_ack = true;
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);

    // Tack on a bell character if requested
    if (moduleConfig.canned_message.send_bell && p->decoded.payload.size < meshtastic_Constants_DATA_PAYLOAD_LEN) {
        p->decoded.payload.bytes[p->decoded.payload.size] = 7;        // Bell character
        p->decoded.payload.bytes[p->decoded.payload.size + 1] = '\0'; // Append Null Terminator
        p->decoded.payload.size++;
    }

    LOG_INFO("Send message id=%d, dest=%x, msg=%.*s", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);

    service->sendToMesh(p, RX_SRC_LOCAL, true); // Send to mesh, cc to phone
}

// Free up any heap mmemory we'd used while selecting / sending canned messages
void InkHUD::MenuApplet::freeCannedMessageResources()
{
    cm.selectedMessageItem = nullptr;
    cm.selectedRecipientItem = nullptr;
    cm.messageItems.clear();
    cm.recipientItems.clear();
}
#endif // MESHTASTIC_INCLUDE_INKHUD
