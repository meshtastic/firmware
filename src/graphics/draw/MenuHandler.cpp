#include "configuration.h"
#if HAS_SCREEN
#include "ClockRenderer.h"
#include "GPS.h"
#include "MenuHandler.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "buzz.h"
#include "graphics/Screen.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/draw/UIRenderer.h"
#include "input/RotaryEncoderInterruptImpl1.h"
#include "input/UpDownInterruptImpl1.h"
#include "main.h"
#include "mesh/MeshTypes.h"
#include "modules/AdminModule.h"
#include "modules/CannedMessageModule.h"
#include "modules/KeyVerificationModule.h"
#include "modules/ChatHistoryStore.h"
#include "FSCommon.h"

#endif
#include "modules/TraceRouteModule.h"
#include "NotificationRenderer.h"
#include <functional>
#include <set>
#include <map>
#include "input/cardKbI2cImpl.h"

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
#include <WiFi.h>
#endif

// External variables from Screen.cpp
struct ScrollState {
    int sel = 0;            // selected line (0..visible-1)
    int scrollIndex = 0;    // first visible message (sliding window)
    int offset = 0;         // horizontal offset (characters)
    uint32_t lastMs = 0;    // last update
};

extern std::string g_pendingKeyboardHeader;
extern std::set<uint8_t> g_favChannelTabs;
extern std::map<uint32_t, ScrollState> g_nodeScroll;
extern std::map<uint8_t, ScrollState> g_chanScroll;


#include <algorithm>
#include <vector>

extern CannedMessageModule *cannedMessageModule;

// External variables for chat functionality
extern bool g_chatSilentMode;

// Toggle global scroll for chat frames
bool g_chatScrollByPress = false;
bool g_chatScrollUpDown = false;  // true = Up, false = Down

extern uint16_t TFT_MESH;

namespace graphics
{

menuHandler::screenMenus menuHandler::menuQueue = menu_none;
bool test_enabled = false;
uint8_t test_count = 0;

// SSID password required for WiFi config menu
static String s_wifiPendingSSID;

void menuHandler::loraMenu()
{
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
    static const char *optionsArray[] = {"Back", "Region Picker", "Device Role", "WiFi Config", "MQTT Config"};
    enum optionsNumbers { Back = 0, lora_picker = 1, device_role_picker = 2, wifi_config = 3, mqtt_config = 4 };
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "LoRa Actions";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 5;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Back) {
            // No action
        } else if (selected == lora_picker) {
            menuHandler::menuQueue = menuHandler::lora_picker;
        } else if (selected == device_role_picker) {
            menuHandler::menuQueue = menuHandler::device_role_picker;
        }
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
        else if (selected == wifi_config) {
            // Close this banner and launch the WiFi menu in the loop
            NotificationRenderer::pauseBanner          = true;
            NotificationRenderer::alertBannerUntil     = 1;
            NotificationRenderer::optionsArrayPtr      = nullptr;
            NotificationRenderer::optionsEnumPtr       = nullptr;
            NotificationRenderer::alertBannerOptions   = 0;
            menuHandler::menuQueue           = menuHandler::wifi_config_menu;
            if (screen) screen->forceDisplay(true);
            return;
        }
        else if (selected == mqtt_config) {
            menuQueue = mqtt_base_menu;
            screen->runNow();
        }
#endif
    };
    screen->showOverlayBanner(bannerOptions);
#else
    static const char *optionsArray[] = {"Back", "Region Picker", "Device Role"};
    enum optionsNumbers { Back = 0, lora_picker = 1, device_role_picker = 2 };
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "LoRa Actions";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Back) {
            // No action
        } else if (selected == lora_picker) {
            menuHandler::menuQueue = menuHandler::lora_picker;
        } else if (selected == device_role_picker) {
            menuHandler::menuQueue = menuHandler::device_role_picker;
        }
    };
    screen->showOverlayBanner(bannerOptions);
#endif
}

void menuHandler::OnboardMessage()
{
    static const char *optionsArray[] = {"OK", "Got it!"};
    enum optionsNumbers { OK, got };
    BannerOverlayOptions bannerOptions;
#if HAS_TFT
    bannerOptions.message = "Welcome to Meshtastic!\nSwipe to navigate and\nlong press to select\nor open a menu.";
#elif defined(BUTTON_PIN)
    bannerOptions.message = "Welcome to Meshtastic!\nClick to navigate and\nlong press to select\nor open a menu.";
#else
    bannerOptions.message = "Welcome to Meshtastic!\nUse the Select button\nto open menus\nand make selections.";
#endif
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        menuHandler::menuQueue = menuHandler::no_timeout_lora_picker;
        screen->runNow();
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::LoraRegionPicker(uint32_t duration)
{
    static const char *optionsArray[] = {"Back",
                                         "US",
                                         "EU_433",
                                         "EU_868",
                                         "CN",
                                         "JP",
                                         "ANZ",
                                         "KR",
                                         "TW",
                                         "RU",
                                         "IN",
                                         "NZ_865",
                                         "TH",
                                         "LORA_24",
                                         "UA_433",
                                         "UA_868",
                                         "MY_433",
                                         "MY_"
                                         "919",
                                         "SG_"
                                         "923",
                                         "PH_433",
                                         "PH_868",
                                         "PH_915",
                                         "ANZ_433",
                                         "KZ_433",
                                         "KZ_863",
                                         "NP_865",
                                         "BR_902"};
    BannerOverlayOptions bannerOptions;
#if defined(M5STACK_UNITC6L)
    bannerOptions.message = "LoRa Region";
#else
    bannerOptions.message = "Set the LoRa region";
#endif
    bannerOptions.durationMs = duration;
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 27;
    bannerOptions.InitialSelected = 0;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected != 0 && config.lora.region != _meshtastic_Config_LoRaConfig_RegionCode(selected)) {
            config.lora.region = _meshtastic_Config_LoRaConfig_RegionCode(selected);
            // This is needed as we wait til picking the LoRa region to generate keys for the first time.
            if (!owner.is_licensed) {
                bool keygenSuccess = false;
                if (config.security.private_key.size == 32) {
                    // public key is derived from private, so this will always have the same result.
                    if (crypto->regeneratePublicKey(config.security.public_key.bytes, config.security.private_key.bytes)) {
                        keygenSuccess = true;
                    }
                } else {
                    LOG_INFO("Generate new PKI keys");
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
            config.lora.tx_enabled = true;
            initRegion();
            if (myRegion->dutyCycle < 100) {
                config.lora.ignore_mqtt = true; // Ignore MQTT by default if region has a duty cycle limit
            }
            service->reloadConfig(SEGMENT_CONFIG);
            rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::DeviceRolePicker()
{
    static const char *optionsArray[] = {"Back", "Client", "Client Mute", "Lost and Found", "Tracker"};
    enum optionsNumbers {
        Back = 0,
        devicerole_client = 1,
        devicerole_clientmute = 2,
        devicerole_lostandfound = 3,
        devicerole_tracker = 4
    };
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Device Role";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 5;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Back) {
            menuHandler::menuQueue = menuHandler::lora_Menu;
            screen->runNow();
            return;
        } else if (selected == devicerole_client) {
            config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
        } else if (selected == devicerole_clientmute) {
            config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE;
        } else if (selected == devicerole_lostandfound) {
            config.device.role = meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND;
        } else if (selected == devicerole_tracker) {
            config.device.role = meshtastic_Config_DeviceConfig_Role_TRACKER;
        }
        service->reloadConfig(SEGMENT_CONFIG);
        rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::TwelveHourPicker()
{
    static const char *optionsArray[] = {"Back", "12-hour", "24-hour"};
    enum optionsNumbers { Back = 0, twelve = 1, twentyfour = 2 };
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Time Format";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Back) {
            menuHandler::menuQueue = menuHandler::clock_menu;
            screen->runNow();
        } else if (selected == twelve) {
            config.display.use_12h_clock = true;
        } else {
            config.display.use_12h_clock = false;
        }
        service->reloadConfig(SEGMENT_CONFIG);
    };
    screen->showOverlayBanner(bannerOptions);
}

// Reusable confirmation prompt function
void menuHandler::showConfirmationBanner(const char *message, std::function<void()> onConfirm)
{
    static const char *confirmOptions[] = {"No", "Yes"};
    BannerOverlayOptions confirmBanner;
    confirmBanner.message = message;
    confirmBanner.optionsArrayPtr = confirmOptions;
    confirmBanner.optionsCount = 2;
    confirmBanner.bannerCallback = [onConfirm](int confirmSelected) -> void {
        if (confirmSelected == 1) {
            onConfirm();
        }
    };
    screen->showOverlayBanner(confirmBanner);
}

void menuHandler::ClockFacePicker()
{
    static const char *optionsArray[] = {"Back", "Digital", "Analog"};
    enum optionsNumbers { Back = 0, Digital = 1, Analog = 2 };
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Which Face?";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Back) {
            menuHandler::menuQueue = menuHandler::clock_menu;
            screen->runNow();
        } else if (selected == Digital) {
            uiconfig.is_clockface_analog = false;
            saveUIConfig();
            screen->setFrames(Screen::FOCUS_CLOCK);
        } else {
            uiconfig.is_clockface_analog = true;
            saveUIConfig();
            screen->setFrames(Screen::FOCUS_CLOCK);
        }
    };
    bannerOptions.InitialSelected = uiconfig.is_clockface_analog ? 2 : 1;
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::TZPicker()
{
    static const char *optionsArray[] = {"Back",
                                         "US/Hawaii",
                                         "US/Alaska",
                                         "US/Pacific",
                                         "US/Arizona",
                                         "US/Mountain",
                                         "US/Central",
                                         "US/Eastern",
                                         "BR/Brasilia",
                                         "UTC",
                                         "EU/Western",
                                         "EU/"
                                         "Central",
                                         "EU/Eastern",
                                         "Asia/Kolkata",
                                         "Asia/Hong_Kong",
                                         "AU/AWST",
                                         "AU/ACST",
                                         "AU/AEST",
                                         "Pacific/NZ"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Pick Timezone";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 19;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 0) {
            menuHandler::menuQueue = menuHandler::clock_menu;
            screen->runNow();
        } else if (selected == 1) { // Hawaii
            strncpy(config.device.tzdef, "HST10", sizeof(config.device.tzdef));
        } else if (selected == 2) { // Alaska
            strncpy(config.device.tzdef, "AKST9AKDT,M3.2.0,M11.1.0", sizeof(config.device.tzdef));
        } else if (selected == 3) { // Pacific
            strncpy(config.device.tzdef, "PST8PDT,M3.2.0,M11.1.0", sizeof(config.device.tzdef));
        } else if (selected == 4) { // Arizona
            strncpy(config.device.tzdef, "MST7", sizeof(config.device.tzdef));
        } else if (selected == 5) { // Mountain
            strncpy(config.device.tzdef, "MST7MDT,M3.2.0,M11.1.0", sizeof(config.device.tzdef));
        } else if (selected == 6) { // Central
            strncpy(config.device.tzdef, "CST6CDT,M3.2.0,M11.1.0", sizeof(config.device.tzdef));
        } else if (selected == 7) { // Eastern
            strncpy(config.device.tzdef, "EST5EDT,M3.2.0,M11.1.0", sizeof(config.device.tzdef));
        } else if (selected == 8) { // Brazil
            strncpy(config.device.tzdef, "BRT3", sizeof(config.device.tzdef));
        } else if (selected == 9) { // UTC
            strncpy(config.device.tzdef, "UTC0", sizeof(config.device.tzdef));
        } else if (selected == 10) { // EU/Western
            strncpy(config.device.tzdef, "GMT0BST,M3.5.0/1,M10.5.0", sizeof(config.device.tzdef));
        } else if (selected == 11) { // EU/Central
            strncpy(config.device.tzdef, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(config.device.tzdef));
        } else if (selected == 12) { // EU/Eastern
            strncpy(config.device.tzdef, "EET-2EEST,M3.5.0/3,M10.5.0/4", sizeof(config.device.tzdef));
        } else if (selected == 13) { // Asia/Kolkata
            strncpy(config.device.tzdef, "IST-5:30", sizeof(config.device.tzdef));
        } else if (selected == 14) { // China
            strncpy(config.device.tzdef, "HKT-8", sizeof(config.device.tzdef));
        } else if (selected == 15) { // AU/AWST
            strncpy(config.device.tzdef, "AWST-8", sizeof(config.device.tzdef));
        } else if (selected == 16) { // AU/ACST
            strncpy(config.device.tzdef, "ACST-9:30ACDT,M10.1.0,M4.1.0/3", sizeof(config.device.tzdef));
        } else if (selected == 17) { // AU/AEST
            strncpy(config.device.tzdef, "AEST-10AEDT,M10.1.0,M4.1.0/3", sizeof(config.device.tzdef));
        } else if (selected == 18) { // NZ
            strncpy(config.device.tzdef, "NZST-12NZDT,M9.5.0,M4.1.0/3", sizeof(config.device.tzdef));
        }
        if (selected != 0) {
            setenv("TZ", config.device.tzdef, 1);
            service->reloadConfig(SEGMENT_CONFIG);
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::clockMenu()
{
#if defined(M5STACK_UNITC6L)
    static const char *optionsArray[] = {"Back", "Time Format", "Timezone"};
#else
    static const char *optionsArray[] = {"Back", "Clock Face", "Time Format", "Timezone"};
#endif
    enum optionsNumbers { Back = 0, Clock = 1, Time = 2, Timezone = 3 };
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Clock Action";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 4;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Clock) {
            menuHandler::menuQueue = menuHandler::clock_face_picker;
            screen->runNow();
        } else if (selected == Time) {
            menuHandler::menuQueue = menuHandler::twelve_hour_picker;
            screen->runNow();
        } else if (selected == Timezone) {
            menuHandler::menuQueue = menuHandler::TZ_picker;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::messageResponseMenu()
{
    enum optionsNumbers { Back = 0, Dismiss = 1, Preset = 2, Freetext = 3, Aloud = 4, enumEnd = 5 };
#if defined(M5STACK_UNITC6L)
    static const char *optionsArray[enumEnd] = {"Back", "Dismiss", "Reply Preset"};
#else
    static const char *optionsArray[enumEnd] = {"Back", "Dismiss", "Reply via Preset"};
#endif
    static int optionsEnumArray[enumEnd] = {Back, Dismiss, Preset};
    int options = 3;

    if (kb_found) {
        optionsArray[options] = "Reply via Freetext";
        optionsEnumArray[options++] = Freetext;
    }

#ifdef HAS_I2S
    optionsArray[options] = "Read Aloud";
    optionsEnumArray[options++] = Aloud;
#endif
    BannerOverlayOptions bannerOptions;
#if defined(M5STACK_UNITC6L)
    bannerOptions.message = "Message";
#else
    bannerOptions.message = "Message Action";
#endif
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Dismiss) {
            screen->hideCurrentFrame();
        } else if (selected == Preset) {
            if (devicestate.rx_text_message.to == NODENUM_BROADCAST) {
                cannedMessageModule->LaunchWithDestination(NODENUM_BROADCAST, devicestate.rx_text_message.channel);
            } else {
                cannedMessageModule->LaunchWithDestination(devicestate.rx_text_message.from);
            }
        } else if (selected == Freetext) {
            if (devicestate.rx_text_message.to == NODENUM_BROADCAST) {
                cannedMessageModule->LaunchFreetextWithDestination(NODENUM_BROADCAST, devicestate.rx_text_message.channel);
            } else {
                cannedMessageModule->LaunchFreetextWithDestination(devicestate.rx_text_message.from);
            }
        }
#ifdef HAS_I2S
        else if (selected == Aloud) {
            const meshtastic_MeshPacket &mp = devicestate.rx_text_message;
            const char *msg = reinterpret_cast<const char *>(mp.decoded.payload.bytes);

            audioThread->readAloud(msg);
        }
#endif
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::homeBaseMenu()
{
    enum optionsNumbers { Back, Backlight, Position, Preset, Freetext, Sleep, enumEnd };

    static const char *optionsArray[enumEnd] = {"Back"};
    static int optionsEnumArray[enumEnd] = {Back};
    int options = 1;

#if defined(PIN_EINK_EN) || defined(PCA_PIN_EINK_EN)
    optionsArray[options] = "Toggle Backlight";
    optionsEnumArray[options++] = Backlight;
#else
    optionsArray[options] = "Sleep Screen";
    optionsEnumArray[options++] = Sleep;
#endif
    if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        optionsArray[options] = "Send Position";
    } else {
        optionsArray[options] = "Send Node Info";
    }
    optionsEnumArray[options++] = Position;
#if defined(M5STACK_UNITC6L)
    optionsArray[options] = "New Preset";
#else
    optionsArray[options] = "New Preset Msg";
#endif
    optionsEnumArray[options++] = Preset;
    if (kb_found) {
        optionsArray[options] = "New Freetext Msg";
        optionsEnumArray[options++] = Freetext;
    }

    BannerOverlayOptions bannerOptions;
#if defined(M5STACK_UNITC6L)
    bannerOptions.message = "Home";
#else
    bannerOptions.message = "Home Action";
#endif
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Backlight) {
#if defined(PIN_EINK_EN)
            if (uiconfig.screen_brightness == 1) {
                uiconfig.screen_brightness = 0;
                digitalWrite(PIN_EINK_EN, LOW);
            } else {
                uiconfig.screen_brightness = 1;
                digitalWrite(PIN_EINK_EN, HIGH);
            }
            saveUIConfig();
#elif defined(PCA_PIN_EINK_EN)
            if (uiconfig.screen_brightness == 1) {
                uiconfig.screen_brightness = 0;
                io.digitalWrite(PCA_PIN_EINK_EN, LOW);
            } else {
                uiconfig.screen_brightness = 1;
                io.digitalWrite(PCA_PIN_EINK_EN, HIGH);
            }
            saveUIConfig();
#endif
        } else if (selected == Sleep) {
            menuHandler::menuQueue = menuHandler::sleep_menu;
            screen->runNow();
        } else if (selected == Position) {
            InputEvent event = {.inputEvent = (input_broker_event)INPUT_BROKER_SEND_PING, .kbchar = 0, .touchX = 0, .touchY = 0};
            inputBroker->injectInputEvent(&event);
        } else if (selected == Preset) {
            cannedMessageModule->LaunchWithDestination(NODENUM_BROADCAST);
        } else if (selected == Freetext) {
            cannedMessageModule->LaunchFreetextWithDestination(NODENUM_BROADCAST);
        }
    };
    screen->showOverlayBanner(bannerOptions);
}



void menuHandler::textMessageBaseMenu()
{
    enum optionsNumbers { Back, Preset, Freetext, enumEnd };

    static const char *optionsArray[enumEnd] = {"Back"};
    static int optionsEnumArray[enumEnd] = {Back};
    int options = 1;
    optionsArray[options] = "New Preset Msg";
    optionsEnumArray[options++] = Preset;
    if (kb_found) {
        optionsArray[options] = "New Freetext Msg";
        optionsEnumArray[options++] = Freetext;
    }

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Message Action";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Preset) {
            cannedMessageModule->LaunchWithDestination(NODENUM_BROADCAST);
        } else if (selected == Freetext) {
            cannedMessageModule->LaunchFreetextWithDestination(NODENUM_BROADCAST);
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::systemBaseMenu()
{
    enum optionsNumbers { Back, SilentMode, Notifications, ScreenOptions, Bluetooth, PowerMenu, FrameToggles, Test, enumEnd };
    static const char *optionsArray[enumEnd] = {"Back"};
    static int optionsEnumArray[enumEnd] = {Back};
    int options = 1;

    // Silent Mode for chats
    optionsArray[options] = g_chatSilentMode ? "Silent Mode: ON" : "Silent Mode: OFF";
    optionsEnumArray[options++] = SilentMode;

    optionsArray[options] = "Notifications";
    optionsEnumArray[options++] = Notifications;
#if defined(ST7789_CS) || defined(ST7796_CS) || defined(USE_OLED) || defined(USE_SSD1306) || defined(USE_SH1106) ||              \
    defined(USE_SH1107) || defined(HELTEC_MESH_NODE_T114) || defined(HELTEC_VISION_MASTER_T190) || HAS_TFT
    optionsArray[options] = "Screen Options";
    optionsEnumArray[options++] = ScreenOptions;
#endif

    optionsArray[options] = "Frame Visiblity Toggle";
    optionsEnumArray[options++] = FrameToggles;
#if defined(M5STACK_UNITC6L)
    optionsArray[options] = "Bluetooth";
#else
    optionsArray[options] = "Bluetooth Toggle";
#endif
    optionsEnumArray[options++] = Bluetooth;
#if defined(M5STACK_UNITC6L)
    optionsArray[options] = "Power";
#else
    optionsArray[options] = "Reboot/Shutdown";
#endif
    optionsEnumArray[options++] = PowerMenu;

    if (test_enabled) {
        optionsArray[options] = "Test Menu";
        optionsEnumArray[options++] = Test;
    }

    BannerOverlayOptions bannerOptions;
#if defined(M5STACK_UNITC6L)
    bannerOptions.message = "System";
#else
    bannerOptions.message = "System Action";
#endif
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = options;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == SilentMode) {
            g_chatSilentMode = !g_chatSilentMode;
            // Refresh menu to show updated state
            menuHandler::menuQueue = menuHandler::system_base_menu;
            screen->runNow();
        } else if (selected == Notifications) {
            menuHandler::menuQueue = menuHandler::notifications_menu;
            screen->runNow();
        } else if (selected == ScreenOptions) {
            menuHandler::menuQueue = menuHandler::screen_options_menu;
            screen->runNow();
        } else if (selected == PowerMenu) {
            menuHandler::menuQueue = menuHandler::power_menu;
            screen->runNow();
        } else if (selected == FrameToggles) {
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        } else if (selected == Test) {
            menuHandler::menuQueue = menuHandler::test_menu;
            screen->runNow();
        } else if (selected == Bluetooth) {
            menuQueue = bluetooth_toggle_menu;
            screen->runNow();
        } else if (selected == Back && !test_enabled) {
            test_count++;
            if (test_count > 4) {
                test_enabled = true;
            }
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::favoriteBaseMenu()
{
    enum optionsNumbers { Back, Preset, Freetext, Remove, TraceRoute, enumEnd };
#if defined(M5STACK_UNITC6L)
    static const char *optionsArray[enumEnd] = {"Back", "New Preset"};
#else
    static const char *optionsArray[enumEnd] = {"Back", "New Preset Msg"};
#endif
    static int optionsEnumArray[enumEnd] = {Back, Preset};
    int options = 2;

    if (kb_found) {
        optionsArray[options] = "New Freetext Msg";
        optionsEnumArray[options++] = Freetext;
    }
#if !defined(M5STACK_UNITC6L)
    optionsArray[options] = "Trace Route";
    optionsEnumArray[options++] = TraceRoute;
#endif
    optionsArray[options] = "Remove Favorite";
    optionsEnumArray[options++] = Remove;

    BannerOverlayOptions bannerOptions;
#if defined(M5STACK_UNITC6L)
    bannerOptions.message = "Favorites";
#else
    bannerOptions.message = "Favorites Action";
#endif
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Preset) {
            cannedMessageModule->LaunchWithDestination(graphics::UIRenderer::currentFavoriteNodeNum);
        } else if (selected == Freetext) {
            cannedMessageModule->LaunchFreetextWithDestination(graphics::UIRenderer::currentFavoriteNodeNum);
        } else if (selected == Remove) {
            menuHandler::menuQueue = menuHandler::remove_favorite;
            screen->runNow();
        } else if (selected == TraceRoute) {
            if (traceRouteModule) {
                traceRouteModule->launch(graphics::UIRenderer::currentFavoriteNodeNum);
            }
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::positionBaseMenu()
{
    enum optionsNumbers { Back, GPSToggle, GPSFormat, CompassMenu, CompassCalibrate, enumEnd };

    static const char *optionsArray[enumEnd] = {"Back", "GPS Toggle", "GPS Format", "Compass"};
    static int optionsEnumArray[enumEnd] = {Back, GPSToggle, GPSFormat, CompassMenu};
    int options = 4;

    if (accelerometerThread) {
        optionsArray[options] = "Compass Calibrate";
        optionsEnumArray[options++] = CompassCalibrate;
    }

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Position Action";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == GPSToggle) {
            menuQueue = gps_toggle_menu;
            screen->runNow();
        } else if (selected == GPSFormat) {
            menuQueue = gps_format_menu;
            screen->runNow();
        } else if (selected == CompassMenu) {
            menuQueue = compass_point_north_menu;
            screen->runNow();
        } else if (selected == CompassCalibrate) {
            accelerometerThread->calibrate(30);
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::nodeListMenu()
{
    enum optionsNumbers { Back, Favorite, TraceRoute, Verify, Reset, enumEnd };
#if defined(M5STACK_UNITC6L)
    static const char *optionsArray[] = {"Back", "Add Favorite", "Reset Node"};
#else
    static const char *optionsArray[] = {"Back", "Add Favorite", "Trace Route", "Key Verification", "Reset NodeDB"};
#endif
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Node Action";
    bannerOptions.optionsArrayPtr = optionsArray;
#if defined(M5STACK_UNITC6L)
    bannerOptions.optionsCount = 3;
#else
    bannerOptions.optionsCount = 5;
#endif
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Favorite) {
            menuQueue = add_favorite;
            screen->runNow();
        } else if (selected == Verify) {
            menuQueue = key_verification_init;
            screen->runNow();
        } else if (selected == Reset) {
            menuQueue = reset_node_db_menu;
            screen->runNow();
        } else if (selected == TraceRoute) {
            menuQueue = trace_route_menu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::resetNodeDBMenu()
{
    static const char *optionsArray[] = {"Back", "Confirm"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Confirm Reset NodeDB";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            disableBluetooth();
            LOG_INFO("Initiate node-db reset");
            nodeDB->resetNodes();
            rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::compassNorthMenu()
{
    enum optionsNumbers { Back, Dynamic, Fixed, Freeze };
    static const char *optionsArray[] = {"Back", "Dynamic", "Fixed Ring", "Freeze Heading"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "North Directions?";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 4;
    bannerOptions.InitialSelected = uiconfig.compass_mode + 1;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Dynamic) {
            if (uiconfig.compass_mode != meshtastic_CompassMode_DYNAMIC) {
                uiconfig.compass_mode = meshtastic_CompassMode_DYNAMIC;
                saveUIConfig();
                screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
            }
        } else if (selected == Fixed) {
            if (uiconfig.compass_mode != meshtastic_CompassMode_FIXED_RING) {
                uiconfig.compass_mode = meshtastic_CompassMode_FIXED_RING;
                saveUIConfig();
                screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
            }
        } else if (selected == Freeze) {
            if (uiconfig.compass_mode != meshtastic_CompassMode_FREEZE_HEADING) {
                uiconfig.compass_mode = meshtastic_CompassMode_FREEZE_HEADING;
                saveUIConfig();
                screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
            }
        } else if (selected == Back) {
            menuQueue = position_base_menu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

#if !MESHTASTIC_EXCLUDE_GPS
void menuHandler::GPSToggleMenu()
{

    static const char *optionsArray[] = {"Back", "Enabled", "Disabled"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Toggle GPS";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_ENABLED;
            playGPSEnableBeep();
            gps->enable();
            service->reloadConfig(SEGMENT_CONFIG);
        } else if (selected == 2) {
            config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_DISABLED;
            playGPSDisableBeep();
            gps->disable();
            service->reloadConfig(SEGMENT_CONFIG);
        } else {
            menuQueue = position_base_menu;
            screen->runNow();
        }
    };
    bannerOptions.InitialSelected = config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED ? 1 : 2;
    screen->showOverlayBanner(bannerOptions);
}
void menuHandler::GPSFormatMenu()
{

    static const char *optionsArray[] = {"Back",
                                         isHighResolution ? "Decimal Degrees" : "DEC",
                                         isHighResolution ? "Degrees Minutes Seconds" : "DMS",
                                         isHighResolution ? "Universal Transverse Mercator" : "UTM",
                                         isHighResolution ? "Military Grid Reference System" : "MGRS",
                                         isHighResolution ? "Open Location Code" : "OLC",
                                         isHighResolution ? "Ordnance Survey Grid Ref" : "OSGR",
                                         isHighResolution ? "Maidenhead Locator" : "MLS"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "GPS Format";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 8;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            uiconfig.gps_format = meshtastic_DeviceUIConfig_GpsCoordinateFormat_DEC;
            service->reloadConfig(SEGMENT_CONFIG);
        } else if (selected == 2) {
            uiconfig.gps_format = meshtastic_DeviceUIConfig_GpsCoordinateFormat_DMS;
            service->reloadConfig(SEGMENT_CONFIG);
        } else if (selected == 3) {
            uiconfig.gps_format = meshtastic_DeviceUIConfig_GpsCoordinateFormat_UTM;
            service->reloadConfig(SEGMENT_CONFIG);
        } else if (selected == 4) {
            uiconfig.gps_format = meshtastic_DeviceUIConfig_GpsCoordinateFormat_MGRS;
            service->reloadConfig(SEGMENT_CONFIG);
        } else if (selected == 5) {
            uiconfig.gps_format = meshtastic_DeviceUIConfig_GpsCoordinateFormat_OLC;
            service->reloadConfig(SEGMENT_CONFIG);
        } else if (selected == 6) {
            uiconfig.gps_format = meshtastic_DeviceUIConfig_GpsCoordinateFormat_OSGR;
            service->reloadConfig(SEGMENT_CONFIG);
        } else if (selected == 7) {
            uiconfig.gps_format = meshtastic_DeviceUIConfig_GpsCoordinateFormat_MLS;
            service->reloadConfig(SEGMENT_CONFIG);
        } else {
            menuQueue = position_base_menu;
            screen->runNow();
        }
    };
    bannerOptions.InitialSelected = uiconfig.gps_format + 1;
    screen->showOverlayBanner(bannerOptions);
}
#endif

void menuHandler::BluetoothToggleMenu()
{
    static const char *optionsArray[] = {"Back", "Enabled", "Disabled"};
    BannerOverlayOptions bannerOptions;
#if defined(M5STACK_UNITC6L)
    bannerOptions.message = "Bluetooth";
#else
    bannerOptions.message = "Toggle Bluetooth";
#endif
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1 || selected == 2) {
            InputEvent event = {.inputEvent = (input_broker_event)170, .kbchar = 170, .touchX = 0, .touchY = 0};
            inputBroker->injectInputEvent(&event);
        }
    };
    bannerOptions.InitialSelected = config.bluetooth.enabled ? 1 : 2;
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::BuzzerModeMenu()
{
    static const char *optionsArray[] = {"All Enabled", "Disabled", "Notifications", "System Only"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Buzzer Mode";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 4;
    bannerOptions.bannerCallback = [](int selected) -> void {
        config.device.buzzer_mode = (meshtastic_Config_DeviceConfig_BuzzerMode)selected;
        service->reloadConfig(SEGMENT_CONFIG);
    };
    bannerOptions.InitialSelected = config.device.buzzer_mode;
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::BrightnessPickerMenu()
{
    static const char *optionsArray[] = {"Back", "Low", "Medium", "High"};

    // Get current brightness level to set initial selection
    int currentSelection = 1; // Default to Medium
    if (uiconfig.screen_brightness >= 255) {
        currentSelection = 3; // Very High
    } else if (uiconfig.screen_brightness >= 128) {
        currentSelection = 2; // High
    } else {
        currentSelection = 1; // Medium
    }

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Brightness";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 4;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) { // Medium
            uiconfig.screen_brightness = 64;
        } else if (selected == 2) { // High
            uiconfig.screen_brightness = 128;
        } else if (selected == 3) { // Very High
            uiconfig.screen_brightness = 255;
        }

        if (selected != 0) { // Not "Back"
                             // Apply brightness immediately
#if defined(HELTEC_MESH_NODE_T114) || defined(HELTEC_VISION_MASTER_T190)
            // For HELTEC devices, use analogWrite to control backlight
            analogWrite(VTFT_LEDA, uiconfig.screen_brightness);
#elif defined(ST7789_CS) || defined(ST7796_CS)
            static_cast<TFTDisplay *>(screen->getDisplayDevice())->setDisplayBrightness(uiconfig.screen_brightness);
#elif defined(USE_OLED) || defined(USE_SSD1306) || defined(USE_SH1106) || defined(USE_SH1107)
            screen->getDisplayDevice()->setBrightness(uiconfig.screen_brightness);
#endif

            // Save to device
            saveUIConfig();

            LOG_INFO("Screen brightness set to %d", uiconfig.screen_brightness);
        }
    };
    bannerOptions.InitialSelected = currentSelection;
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::switchToMUIMenu()
{
    static const char *optionsArray[] = {"No", "Yes"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Switch to MUI?";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            config.display.displaymode = meshtastic_Config_DisplayConfig_DisplayMode_COLOR;
            config.bluetooth.enabled = false;
            service->reloadConfig(SEGMENT_CONFIG);
            rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::TFTColorPickerMenu(OLEDDisplay *display)
{
    static const char *optionsArray[] = {"Back", "Default", "Meshtastic Green", "Yellow", "Red", "Orange", "Purple", "Teal",
                                         "Pink", "White"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Select Screen Color";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 10;
    bannerOptions.bannerCallback = [display](int selected) -> void {
#if defined(HELTEC_MESH_NODE_T114) || defined(HELTEC_VISION_MASTER_T190) || defined(T_DECK) || defined(T_LORA_PAGER) || HAS_TFT
        uint8_t TFT_MESH_r = 0;
        uint8_t TFT_MESH_g = 0;
        uint8_t TFT_MESH_b = 0;
        if (selected == 1) {
            LOG_INFO("Setting color to system default or defined variant");
            // Given just before we set all these to zero, we will allow this to go through
        } else if (selected == 2) {
            LOG_INFO("Setting color to Meshtastic Green");
            TFT_MESH_r = 103;
            TFT_MESH_g = 234;
            TFT_MESH_b = 148;
        } else if (selected == 3) {
            LOG_INFO("Setting color to Yellow");
            TFT_MESH_r = 255;
            TFT_MESH_g = 255;
            TFT_MESH_b = 128;
        } else if (selected == 4) {
            LOG_INFO("Setting color to Red");
            TFT_MESH_r = 255;
            TFT_MESH_g = 64;
            TFT_MESH_b = 64;
        } else if (selected == 5) {
            LOG_INFO("Setting color to Orange");
            TFT_MESH_r = 255;
            TFT_MESH_g = 160;
            TFT_MESH_b = 20;
        } else if (selected == 6) {
            LOG_INFO("Setting color to Purple");
            TFT_MESH_r = 204;
            TFT_MESH_g = 153;
            TFT_MESH_b = 255;
        } else if (selected == 7) {
            LOG_INFO("Setting color to Teal");
            TFT_MESH_r = 64;
            TFT_MESH_g = 224;
            TFT_MESH_b = 208;
        } else if (selected == 8) {
            LOG_INFO("Setting color to Pink");
            TFT_MESH_r = 255;
            TFT_MESH_g = 105;
            TFT_MESH_b = 180;
        } else if (selected == 9) {
            LOG_INFO("Setting color to White");
            TFT_MESH_r = 255;
            TFT_MESH_g = 255;
            TFT_MESH_b = 255;
        } else {
            menuQueue = system_base_menu;
            screen->runNow();
        }

        if (selected != 0) {
            display->setColor(BLACK);
            display->fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
            display->setColor(WHITE);

            if (TFT_MESH_r == 0 && TFT_MESH_g == 0 && TFT_MESH_b == 0) {
#ifdef TFT_MESH_OVERRIDE
                TFT_MESH = TFT_MESH_OVERRIDE;
#else
                TFT_MESH = COLOR565(0x67, 0xEA, 0x94);
#endif
            } else {
                TFT_MESH = COLOR565(TFT_MESH_r, TFT_MESH_g, TFT_MESH_b);
            }

#if defined(HELTEC_MESH_NODE_T114) || defined(HELTEC_VISION_MASTER_T190)
            static_cast<ST7789Spi *>(screen->getDisplayDevice())->setRGB(TFT_MESH);
#endif

            screen->setFrames(graphics::Screen::FOCUS_SYSTEM);
            if (TFT_MESH_r == 0 && TFT_MESH_g == 0 && TFT_MESH_b == 0) {
                uiconfig.screen_rgb_color = 0;
            } else {
                uiconfig.screen_rgb_color = (TFT_MESH_r << 16) | (TFT_MESH_g << 8) | TFT_MESH_b;
            }
            LOG_INFO("Storing Value of %d to uiconfig.screen_rgb_color", uiconfig.screen_rgb_color);
            saveUIConfig();
        }
#endif
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::rebootMenu()
{
    static const char *optionsArray[] = {"Back", "Confirm"};
    BannerOverlayOptions bannerOptions;
#if defined(M5STACK_UNITC6L)
    bannerOptions.message = "Reboot";
#else
    bannerOptions.message = "Reboot Device?";
#endif
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            IF_SCREEN(screen->showSimpleBanner("Rebooting...", 0));
            nodeDB->saveToDisk();
            rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
        } else {
            menuQueue = power_menu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::shutdownMenu()
{
    static const char *optionsArray[] = {"Back", "Confirm"};
    BannerOverlayOptions bannerOptions;
#if defined(M5STACK_UNITC6L)
    bannerOptions.message = "Shutdown";
#else
    bannerOptions.message = "Shutdown Device?";
#endif
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            InputEvent event = {.inputEvent = (input_broker_event)INPUT_BROKER_SHUTDOWN, .kbchar = 0, .touchX = 0, .touchY = 0};
            inputBroker->injectInputEvent(&event);
        } else {
            menuQueue = power_menu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::addFavoriteMenu()
{
#if defined(M5STACK_UNITC6L)
    screen->showNodePicker("Node Favorite", 30000, [](uint32_t nodenum) -> void {
#else
    screen->showNodePicker("Node To Favorite", 30000, [](uint32_t nodenum) -> void {

#endif
        LOG_WARN("Nodenum: %u", nodenum);
        nodeDB->set_favorite(true, nodenum);
        screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
    });
}

void menuHandler::removeFavoriteMenu()
{

    static const char *optionsArray[] = {"Back", "Yes"};
    BannerOverlayOptions bannerOptions;
    std::string message = "Unfavorite This Node?\n";
    auto node = nodeDB->getMeshNode(graphics::UIRenderer::currentFavoriteNodeNum);
    if (node && node->has_user) {
        message += sanitizeString(node->user.long_name).substr(0, 15);
    }
    bannerOptions.message = message.c_str();
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            LOG_INFO("Removing %x as favorite node", graphics::UIRenderer::currentFavoriteNodeNum);
            nodeDB->set_favorite(false, graphics::UIRenderer::currentFavoriteNodeNum);
            screen->setFrames(graphics::Screen::FOCUS_DEFAULT);
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::traceRouteMenu()
{
    screen->showNodePicker("Node to Trace", 30000, [](uint32_t nodenum) -> void {
        LOG_INFO("Menu: Node picker selected node 0x%08x, traceRouteModule=%p", nodenum, traceRouteModule);
        if (traceRouteModule) {
            traceRouteModule->startTraceRoute(nodenum);
        }
    });
}

void menuHandler::testMenu()
{

    enum optionsNumbers { Back, NumberPicker, ShowChirpy };
    static const char *optionsArray[4] = {"Back"};
    static int optionsEnumArray[4] = {Back};
    int options = 1;

    optionsArray[options] = "Number Picker";
    optionsEnumArray[options++] = NumberPicker;

    optionsArray[options] = screen->isFrameHidden("chirpy") ? "Show Chirpy" : "Hide Chirpy";
    optionsEnumArray[options++] = ShowChirpy;

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Hidden Test Menu";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = options;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == NumberPicker) {
            menuQueue = number_test;
            screen->runNow();
        } else if (selected == ShowChirpy) {
            screen->toggleFrameVisibility("chirpy");
            screen->setFrames(Screen::FOCUS_SYSTEM);

        } else {
            menuQueue = system_base_menu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::numberTest()
{
    screen->showNumberPicker("Pick a number\n ", 30000, 4,
                             [](int number_picked) -> void { LOG_WARN("Nodenum: %u", number_picked); });
}

void menuHandler::wifiBaseMenu()
{
    enum optionsNumbers { Back, Wifi_toggle };

    static const char *optionsArray[] = {"Back", "WiFi Toggle"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "WiFi Menu";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Wifi_toggle) {
            menuQueue = wifi_toggle_menu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::wifiConfigMenu()
{
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
    // close any keyboard that might be open
    if (NotificationRenderer::virtualKeyboard) {
        delete NotificationRenderer::virtualKeyboard;
        NotificationRenderer::virtualKeyboard = nullptr;
    }

    // Wifi scan
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(60);
    int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);

    struct AP { String ssid; int rssi; bool open; };
    std::vector<AP> aps;
    aps.reserve(n > 0 ? n : 0);

    for (int i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        int  rssi  = WiFi.RSSI(i);
        bool open  = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);

        auto it = std::find_if(aps.begin(), aps.end(), [&](const AP& a){ return a.ssid == ssid; });
        if (it == aps.end()) aps.push_back({ssid, rssi, open});
        else if (rssi > it->rssi) { it->rssi = rssi; it->open = open; }
    }

    std::sort(aps.begin(), aps.end(), [](const AP& a, const AP& b){ return a.rssi > b.rssi; });
    const int MAX_SHOW = 12;
    static char labels[MAX_SHOW + 3][40];
    static const char* options[MAX_SHOW + 3];

    int count = 0;
    int apShown = (int)std::min(aps.size(), (size_t)MAX_SHOW);
    for (int i = 0; i < apShown; ++i) {
        const auto &ap = aps[i];
        String ss = ap.ssid;
        if (ss.length() > 22) ss = ss.substring(0, 19) + "...";
        snprintf(labels[count], sizeof(labels[count]), "%s", ss.c_str());
        options[count++] = labels[i];
    }

    if (apShown == 0) {
        snprintf(labels[count], sizeof(labels[count]), "No networks");
        options[count++] = labels[count];
    }

    int rescanIdx = count;
    options[count++] = "Rescan";
    int backIdx = count;
    options[count++] = "Back";

    static std::vector<AP> s_aps;
    static int s_apShown, s_rescanIdx, s_backIdx;
    s_aps       = aps;
    s_apShown   = apShown;
    s_rescanIdx = rescanIdx;
    s_backIdx   = backIdx;

    BannerOverlayOptions o;
    o.message         = "WiFi Networks";
    o.durationMs      = 0;
    o.optionsArrayPtr = options;
    o.optionsCount    = count;
    o.optionsEnumPtr  = nullptr; // we return index

    o.bannerCallback = [](int sel) {
        if (sel == s_rescanIdx) {
            menuHandler::menuQueue = menuHandler::wifi_config_menu;
            if (screen) screen->forceDisplay(true);
            return;
        }
        if (sel == s_backIdx) {
            if (screen) screen->setFrames(Screen::FOCUS_PRESERVE);
            return;
        }

        if (s_apShown == 0) return;
        if (sel < 0 || sel >= s_apShown) return;

        const String ssidSel = s_aps[sel].ssid;
        const bool   open    = s_aps[sel].open;

        if (open) {
            menuHandler::showConfirmationBanner("Open network. Connect?", [ssidSel]() {
                config.network.wifi_enabled = true;
                strlcpy(config.network.wifi_ssid, ssidSel.c_str(), sizeof(config.network.wifi_ssid));
                config.network.wifi_psk[0] = '\0';
                service->reloadConfig(SEGMENT_CONFIG);

                WiFi.mode(WIFI_STA);
                WiFi.disconnect(true);
                delay(50);
                WiFi.begin(ssidSel.c_str());
                if (screen) screen->showSimpleBanner("Connecting...", 2000);
            });
            return;
        }

        // required password
        NotificationRenderer::pauseBanner         = true;
        NotificationRenderer::alertBannerUntil    = 1;
        NotificationRenderer::optionsArrayPtr     = nullptr;
        NotificationRenderer::optionsEnumPtr      = nullptr;
        NotificationRenderer::alertBannerOptions  = 0;

        s_wifiPendingSSID = ssidSel;
        menuHandler::menuQueue = graphics::menuHandler::wifi_password_prompt;
        if (screen) screen->forceDisplay(true);
    };

    screen->showOverlayBanner(o);
#else
    if (screen) screen->showSimpleBanner("WiFi not available", 2000);
#endif
}


void menuHandler::wifiToggleMenu()
{
    enum optionsNumbers { Back, Wifi_toggle };

    static const char *optionsArray[] = {"Back", "Disable"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Disable Wifi and\nEnable Bluetooth?";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Wifi_toggle) {
            config.network.wifi_enabled = false;
            config.bluetooth.enabled = true;
            service->reloadConfig(SEGMENT_CONFIG);
            rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::notificationsMenu()
{
    enum optionsNumbers { Back, BuzzerActions };
    static const char *optionsArray[] = {"Back", "Buzzer Actions"};
    static int optionsEnumArray[] = {Back, BuzzerActions};
    int options = 2;

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Notifications";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = options;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == BuzzerActions) {
            menuHandler::menuQueue = menuHandler::buzzermodemenupicker;
            screen->runNow();
        } else {
            menuQueue = system_base_menu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::screenOptionsMenu()
{
    // Check if brightness is supported
    bool hasSupportBrightness = false;
#if defined(ST7789_CS) || defined(USE_OLED) || defined(USE_SSD1306) || defined(USE_SH1106) || defined(USE_SH1107)
    hasSupportBrightness = true;
#endif

#if defined(T_DECK)
    // TDeck Doesn't seem to support brightness at all, at least not reliably
    hasSupportBrightness = false;
#endif

    enum optionsNumbers { Back, Brightness, ScreenColor };
    static const char *optionsArray[4] = {"Back"};
    static int optionsEnumArray[4] = {Back};
    int options = 1;

    // Only show brightness for B&W displays
    if (hasSupportBrightness) {
        optionsArray[options] = "Brightness";
        optionsEnumArray[options++] = Brightness;
    }

    // Only show screen color for TFT displays
#if defined(HELTEC_MESH_NODE_T114) || defined(HELTEC_VISION_MASTER_T190) || defined(T_DECK) || defined(T_LORA_PAGER) || HAS_TFT
    optionsArray[options] = "Screen Color";
    optionsEnumArray[options++] = ScreenColor;
#endif

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Screen Options";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = options;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Brightness) {
            menuHandler::menuQueue = menuHandler::brightness_picker;
            screen->runNow();
        } else if (selected == ScreenColor) {
            menuHandler::menuQueue = menuHandler::tftcolormenupicker;
            screen->runNow();
        } else {
            menuQueue = system_base_menu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::powerMenu()
{

    enum optionsNumbers { Back, Reboot, Shutdown, MUI };
    static const char *optionsArray[4] = {"Back"};
    static int optionsEnumArray[4] = {Back};
    int options = 1;

    optionsArray[options] = "Reboot";
    optionsEnumArray[options++] = Reboot;

    optionsArray[options] = "Shutdown";
    optionsEnumArray[options++] = Shutdown;

#if HAS_TFT
    optionsArray[options] = "Switch to MUI";
    optionsEnumArray[options++] = MUI;
#endif

    BannerOverlayOptions bannerOptions;
#if defined(M5STACK_UNITC6L)
    bannerOptions.message = "Power";
#else
    bannerOptions.message = "Reboot / Shutdown";
#endif
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = options;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Reboot) {
            menuHandler::menuQueue = menuHandler::reboot_menu;
            screen->runNow();
        } else if (selected == Shutdown) {
            menuHandler::menuQueue = menuHandler::shutdown_menu;
            screen->runNow();
        } else if (selected == MUI) {
            menuHandler::menuQueue = menuHandler::mui_picker;
            screen->runNow();
        } else {
            menuQueue = system_base_menu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::keyVerificationInitMenu()
{
    screen->showNodePicker("Node to Verify", 30000,
                           [](uint32_t selected) -> void { keyVerificationModule->sendInitialRequest(selected); });
}

void menuHandler::keyVerificationFinalPrompt()
{
    char message[40] = {0};
    memset(message, 0, sizeof(message));
    sprintf(message, "Verification: \n");
    keyVerificationModule->generateVerificationCode(message + 15); // send the toPhone packet

    if (screen) {
        static const char *optionsArray[] = {"Reject", "Accept"};
        graphics::BannerOverlayOptions options;
        options.message = message;
        options.durationMs = 30000;
        options.optionsArrayPtr = optionsArray;
        options.optionsCount = 2;
        options.notificationType = graphics::notificationTypeEnum::selection_picker;
        options.bannerCallback = [=](int selected) {
            if (selected == 1) {
                auto remoteNodePtr = nodeDB->getMeshNode(keyVerificationModule->getCurrentRemoteNode());
                remoteNodePtr->bitfield |= NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK;
            }
        };
        screen->showOverlayBanner(options);
    }
}

void menuHandler::FrameToggles_menu()
{
    enum optionsNumbers {
        Finish,
        nodelist,
        nodelist_lastheard,
        nodelist_hopsignal,
        nodelist_distance,
        nodelist_bearings,
        gps,
        lora,
        clock,
        show_favorites,
        enumEnd
    };
    static const char *optionsArray[enumEnd] = {"Finish"};
    static int optionsEnumArray[enumEnd] = {Finish};
    int options = 1;

    // Track last selected index (not enum value!)
    static int lastSelectedIndex = 0;

#ifndef USE_EINK
    optionsArray[options] = screen->isFrameHidden("nodelist") ? "Show Node List" : "Hide Node List";
    optionsEnumArray[options++] = nodelist;
#endif
#ifdef USE_EINK
    optionsArray[options] = screen->isFrameHidden("nodelist_lastheard") ? "Show NL - Last Heard" : "Hide NL - Last Heard";
    optionsEnumArray[options++] = nodelist_lastheard;
    optionsArray[options] = screen->isFrameHidden("nodelist_hopsignal") ? "Show NL - Hops/Signal" : "Hide NL - Hops/Signal";
    optionsEnumArray[options++] = nodelist_hopsignal;
    optionsArray[options] = screen->isFrameHidden("nodelist_distance") ? "Show NL - Distance" : "Hide NL - Distance";
    optionsEnumArray[options++] = nodelist_distance;
#endif
#if HAS_GPS
    optionsArray[options] = screen->isFrameHidden("nodelist_bearings") ? "Show Bearings" : "Hide Bearings";
    optionsEnumArray[options++] = nodelist_bearings;

    optionsArray[options] = screen->isFrameHidden("gps") ? "Show Position" : "Hide Position";
    optionsEnumArray[options++] = gps;
#endif

    optionsArray[options] = screen->isFrameHidden("lora") ? "Show LoRa" : "Hide LoRa";
    optionsEnumArray[options++] = lora;

    optionsArray[options] = screen->isFrameHidden("clock") ? "Show Clock" : "Hide Clock";
    optionsEnumArray[options++] = clock;

    optionsArray[options] = screen->isFrameHidden("show_favorites") ? "Show Favorites" : "Hide Favorites";
    optionsEnumArray[options++] = show_favorites;

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Show/Hide Frames";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = options;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.InitialSelected = lastSelectedIndex; // Use index, not enum value

    bannerOptions.bannerCallback = [options](int selected) mutable -> void {
        // Find the index of selected in optionsEnumArray
        int idx = 0;
        for (; idx < options; ++idx) {
            if (optionsEnumArray[idx] == selected)
                break;
        }
        lastSelectedIndex = idx;

        if (selected == Finish) {
            screen->setFrames(Screen::FOCUS_DEFAULT);
        } else if (selected == nodelist) {
            screen->toggleFrameVisibility("nodelist");
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        } else if (selected == nodelist_lastheard) {
            screen->toggleFrameVisibility("nodelist_lastheard");
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        } else if (selected == nodelist_hopsignal) {
            screen->toggleFrameVisibility("nodelist_hopsignal");
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        } else if (selected == nodelist_distance) {
            screen->toggleFrameVisibility("nodelist_distance");
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        } else if (selected == nodelist_bearings) {
            screen->toggleFrameVisibility("nodelist_bearings");
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        } else if (selected == gps) {
            screen->toggleFrameVisibility("gps");
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        } else if (selected == lora) {
            screen->toggleFrameVisibility("lora");
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        } else if (selected == clock) {
            screen->toggleFrameVisibility("clock");
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        } else if (selected == show_favorites) {
            screen->toggleFrameVisibility("show_favorites");
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::handleMenuSwitch(OLEDDisplay *display)
{
    if (menuQueue != menu_none)
        test_count = 0;
    switch (menuQueue) {
    case menu_none:
        break;
    case lora_Menu:
        loraMenu();
        break;
    case lora_picker:
        LoraRegionPicker();
        break;
    case device_role_picker:
        DeviceRolePicker();
        break;
    case no_timeout_lora_picker:
        LoraRegionPicker(0);
        break;
    case TZ_picker:
        TZPicker();
        break;
    case twelve_hour_picker:
        TwelveHourPicker();
        break;
    case clock_face_picker:
        ClockFacePicker();
        break;
    case clock_menu:
        clockMenu();
        break;
    case system_base_menu:
        systemBaseMenu();
        break;
    case silent_mode_toggle:
        silentModeToggle();
        break;
    case position_base_menu:
        positionBaseMenu();
        break;
    case wifi_config_menu:
        wifiConfigMenu();
        menuQueue = menu_none;
        return;
    case node_info_menu:
        // No-op
    case wifi_password_prompt: {
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
        char hdr[48];
        snprintf(hdr, sizeof(hdr), "WiFi: %s", s_wifiPendingSSID.c_str());
        LOG_INFO("WiFi password prompt: header='%s', kb_found=%d", hdr, kb_found ? 1 : 0);

        // WiFi password callback function
        auto wifiPasswordCallback = [](const std::string &pass) {
            // persist config
            config.network.wifi_enabled = true;
            strlcpy(config.network.wifi_ssid, s_wifiPendingSSID.c_str(), sizeof(config.network.wifi_ssid));
            strlcpy(config.network.wifi_psk,  pass.c_str(),             sizeof(config.network.wifi_psk));
            service->reloadConfig(SEGMENT_CONFIG);

            // apply now
            WiFi.mode(WIFI_STA);
            WiFi.disconnect(true);
            delay(50);
            WiFi.begin(s_wifiPendingSSID.c_str(), pass.c_str());
            if (screen) screen->showSimpleBanner("Connecting...", 2000);
        };

        // Use CardKB-friendly input when CardKB is available
        if (kb_found && cannedMessageModule) {
            cannedMessageModule->LaunchFreetextKbPrompt(hdr, "", wifiPasswordCallback);
        } else {
            // Activate CardKB if available for fallback
            #if !defined(ARCH_PORTDUINO) && !MESHTASTIC_EXCLUDE_I2C
            if (!::cardKbI2cImpl) {
                ::cardKbI2cImpl = new CardKbI2cImpl();
                ::cardKbI2cImpl->init();
            }
            #endif
            screen->showTextInput(hdr, "", 0, wifiPasswordCallback);
        }
#endif
        menuQueue = menu_none;
        return;
    }
    case mqtt_server_prompt: {
        char hdr[32] = "MQTT Server:";

        auto serverCallback = [](const std::string &server) {
            strlcpy(moduleConfig.mqtt.address, server.c_str(), sizeof(moduleConfig.mqtt.address));
            service->reloadConfig(SEGMENT_MODULECONFIG);
            if (screen) screen->showSimpleBanner("Server Saved", 2000);
        };

        if (kb_found && cannedMessageModule) {
            cannedMessageModule->LaunchFreetextKbPrompt(hdr, moduleConfig.mqtt.address, serverCallback);
        } else {
            #if !defined(ARCH_PORTDUINO) && !MESHTASTIC_EXCLUDE_I2C
            if (!::cardKbI2cImpl) {
                ::cardKbI2cImpl = new CardKbI2cImpl();
                ::cardKbI2cImpl->init();
            }
            #endif
            screen->showTextInput(hdr, moduleConfig.mqtt.address, 0, serverCallback);
        }
        menuQueue = menu_none;
        return;
    }
    case mqtt_username_prompt: {
        char hdr[32] = "MQTT Username:";

        auto usernameCallback = [](const std::string &username) {
            strlcpy(moduleConfig.mqtt.username, username.c_str(), sizeof(moduleConfig.mqtt.username));
            service->reloadConfig(SEGMENT_MODULECONFIG);
            if (screen) screen->showSimpleBanner("Username Saved", 2000);
        };

        if (kb_found && cannedMessageModule) {
            cannedMessageModule->LaunchFreetextKbPrompt(hdr, moduleConfig.mqtt.username, usernameCallback);
        } else {
            screen->showTextInput(hdr, moduleConfig.mqtt.username, 0, usernameCallback);
        }
        menuQueue = menu_none;
        return;
    }
    case mqtt_password_prompt: {
        char hdr[32] = "MQTT Password:";

        auto passwordCallback = [](const std::string &password) {
            strlcpy(moduleConfig.mqtt.password, password.c_str(), sizeof(moduleConfig.mqtt.password));
            service->reloadConfig(SEGMENT_MODULECONFIG);
            if (screen) screen->showSimpleBanner("Password Saved", 2000);
        };

        if (kb_found && cannedMessageModule) {
            cannedMessageModule->LaunchFreetextKbPrompt(hdr, "********", passwordCallback);
        } else {
            screen->showTextInput(hdr, "", 0, passwordCallback);
        }
        menuQueue = menu_none;
        return;
    }
    case mqtt_root_prompt: {
        char hdr[32] = "MQTT Root Topic:";

        auto rootCallback = [](const std::string &root) {
            strlcpy(moduleConfig.mqtt.root, root.c_str(), sizeof(moduleConfig.mqtt.root));
            service->reloadConfig(SEGMENT_MODULECONFIG);
            if (screen) screen->showSimpleBanner("Root Topic Saved", 2000);
        };

        if (kb_found && cannedMessageModule) {
            cannedMessageModule->LaunchFreetextKbPrompt(hdr, moduleConfig.mqtt.root, rootCallback);
        } else {
            screen->showTextInput(hdr, moduleConfig.mqtt.root, 0, rootCallback);
        }
        menuQueue = menu_none;
        return;
    }
 #if !MESHTASTIC_EXCL
#if !MESHTASTIC_EXCLUDE_GPS
    case gps_toggle_menu:
        GPSToggleMenu();
        break;
    case gps_format_menu:
        GPSFormatMenu();
        break;
#endif
    case compass_point_north_menu:
        compassNorthMenu();
        break;
    case reset_node_db_menu:
        resetNodeDBMenu();
        break;
    case buzzermodemenupicker:
        BuzzerModeMenu();
        break;
    case mui_picker:
        switchToMUIMenu();
        break;
    case tftcolormenupicker:
        TFTColorPickerMenu(display);
        break;
    case brightness_picker:
        BrightnessPickerMenu();
        break;
    case reboot_menu:
        rebootMenu();
        break;
    case shutdown_menu:
        shutdownMenu();
        break;
    case add_favorite:
        addFavoriteMenu();
        break;
    case remove_favorite:
        removeFavoriteMenu();
        break;
    case trace_route_menu:
        traceRouteMenu();
        break;
    case test_menu:
        testMenu();
        break;
    case number_test:
        numberTest();
        break;
    case wifi_toggle_menu:
        wifiToggleMenu();
        break;
    case mqtt_base_menu:
        mqttBaseMenu();
        break;
    case mqtt_toggle_menu:
        mqttToggleMenu();
        break;
    case mqtt_server_config:
        mqttServerConfig();
        break;
    case mqtt_credentials_config:
        mqttCredentialsConfig();
        break;
    case key_verification_init:
        keyVerificationInitMenu();
        break;
    case key_verification_final_prompt:
        keyVerificationFinalPrompt();
        break;
    case bluetooth_toggle_menu:
        BluetoothToggleMenu();
        break;
    case notifications_menu:
        notificationsMenu();
        break;
    case screen_options_menu:
        screenOptionsMenu();
        break;
    case power_menu:
        powerMenu();
        break;
    case FrameToggles:
        FrameToggles_menu();
        break;
    case sleep_menu:
        sleepMenu();
        break;
    case sleep_timer_config:
        sleepTimerConfig();
        break;
    case throttle_message:
        screen->showSimpleBanner("Too Many Attempts\nTry again in 60 seconds.", 5000);
        break;
    }
    menuQueue = menu_none;
}

void menuHandler::mqttBaseMenu()
{
    enum optionsNumbers { Back, Toggle, ServerConfig, Credentials, Status };

    static const char *optionsArray[] = {"Back", "MQTT Toggle", "Server Config", "Credentials", "Status"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "MQTT Menu";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 5;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Toggle) {
            menuQueue = mqtt_toggle_menu;
            screen->runNow();
        } else if (selected == ServerConfig) {
            menuQueue = mqtt_server_config;
            screen->runNow();
        } else if (selected == Credentials) {
            menuQueue = mqtt_credentials_config;
            screen->runNow();
        } else if (selected == Status) {
            // Show detailed MQTT status screen
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
            screen->openMqttInfoScreen();
#endif
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::mqttServerConfig()
{
    enum optionsNumbers { Back, Server, TLS, Encryption };
    static const char *optionsArray[] = {"Back", "Server Address", "TLS Enable", "Encryption"};

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Server Config";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 4;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Server) {
            menuQueue = mqtt_server_prompt;
            screen->runNow();
        } else if (selected == TLS) {
            moduleConfig.mqtt.tls_enabled = !moduleConfig.mqtt.tls_enabled;
            service->reloadConfig(SEGMENT_MODULECONFIG);
            screen->showSimpleBanner(moduleConfig.mqtt.tls_enabled ? "TLS Enabled" : "TLS Disabled", 2000);
        } else if (selected == Encryption) {
            moduleConfig.mqtt.encryption_enabled = !moduleConfig.mqtt.encryption_enabled;
            service->reloadConfig(SEGMENT_MODULECONFIG);
            screen->showSimpleBanner(moduleConfig.mqtt.encryption_enabled ? "Encryption On" : "Encryption Off", 2000);
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::mqttCredentialsConfig()
{
    enum optionsNumbers { Back, Username, Password, Root };
    static const char *optionsArray[] = {"Back", "Username", "Password", "Root Topic"};

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Credentials";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 4;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Username) {
            menuQueue = mqtt_username_prompt;
            screen->runNow();
        } else if (selected == Password) {
            menuQueue = mqtt_password_prompt;
            screen->runNow();
        } else if (selected == Root) {
            menuQueue = mqtt_root_prompt;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::mqttToggleMenu()
{
    bool currentState = moduleConfig.mqtt.enabled;

    showConfirmationBanner(
        currentState ? "Disable MQTT?" : "Enable MQTT?",
        [currentState]() -> void {
            moduleConfig.mqtt.enabled = !currentState;
            service->reloadConfig(SEGMENT_MODULECONFIG);
            screen->showSimpleBanner(!currentState ? "MQTT Enabled" : "MQTT Disabled", 2000);
        }
    );
}

void menuHandler::saveUIConfig()
{
    nodeDB->saveProto("/prefs/uiconfig.proto", meshtastic_DeviceUIConfig_size, &meshtastic_DeviceUIConfig_msg, &uiconfig);
}

void menuHandler::silentModeToggle()
{
    showConfirmationBanner(
        g_chatSilentMode ? "Disable Silent Mode?" : "Enable Silent Mode?",
        []() -> void {
            g_chatSilentMode = !g_chatSilentMode;
            screen->showSimpleBanner(g_chatSilentMode ? "Silent Mode ON" : "Silent Mode OFF", 2000);
        }
    );
}

void menuHandler::sleepMenu()
{
    enum optionsNumbers { Back, SleepNow, TimerConfig };
    static const char *optionsArray[3] = {"Back", "Sleep Now", "Timer Config"};
    static int optionsEnumArray[3] = {Back, SleepNow, TimerConfig};

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Sleep Options";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == SleepNow) {
            screen->setOn(false);
        } else if (selected == TimerConfig) {
            menuHandler::menuQueue = menuHandler::sleep_timer_config;
            screen->runNow();
        } else {
            menuQueue = system_base_menu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::sleepTimerConfig()
{
    enum optionsNumbers { Back, Timer30s, Timer1m, Timer5m, Timer10m };
    static const char *optionsArray[5] = {"Back", "30 seconds", "1 minute", "5 minutes", "10 minutes"};
    static int optionsEnumArray[5] = {Back, Timer30s, Timer1m, Timer5m, Timer10m};

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Sleep Timer";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 5;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.bannerCallback = [](int selected) -> void {
        uint32_t timeoutMs = 0;
        const char* message = "";

        if (selected == Timer30s) {
            timeoutMs = 30 * 1000;
            message = "30s Screen Timer Set";
        } else if (selected == Timer1m) {
            timeoutMs = 60 * 1000;
            message = "1m Screen Timer Set";
        } else if (selected == Timer5m) {
            timeoutMs = 5 * 60 * 1000;
            message = "5m Screen Timer Set";
        } else if (selected == Timer10m) {
            timeoutMs = 10 * 60 * 1000;
            message = "10m Screen Timer Set";
        }

        if (timeoutMs > 0) {
            // Set custom timeout
            config.display.screen_on_secs = timeoutMs / 1000;
            service->reloadConfig(SEGMENT_CONFIG);
            screen->showSimpleBanner(message, 2000);
        } else {
            menuQueue = sleep_menu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::openChatActionsForNode(uint32_t nodeId)
{
    // Dynamic options (max 9 visible here)
    enum { kPreset = 1, kFree = 2, kRemove = 3, kRemoveFav = 4, kDeleteNode = 5, kMarkRead = 6, kInfo = 7, kScroll = 8, kScrollType = 9, kBack = 10 };

    static const char* opts[9];
    static int         enums[9];
    int count = 0;

    // Preset / Freetext according to CardKB
    if (kb_found) {
        opts[count]  = "New Freetext Msg";
        enums[count] = kFree;
        count++;
    } else {
        opts[count]  = "New Preset Msg";
        enums[count] = kPreset;
        count++;
    }

    // Scroll Btn only if there is NO CardKB and NO rotary encoder - MOVED TO SECOND POSITION
    static char scrollLabel[24];
    static char scrollTypeLabel[24];
    if (!kb_found && rotaryEncoderInterruptImpl1 == nullptr) {
        snprintf(scrollLabel, sizeof(scrollLabel), "Scroll Btn: %s", g_chatScrollByPress ? "ON" : "OFF");
        opts[count]  = scrollLabel;
        enums[count] = kScroll;
        count++;

        // Show scroll direction option only when scroll button is ON
        if (g_chatScrollByPress) {
            snprintf(scrollTypeLabel, sizeof(scrollTypeLabel), "Scroll Dir: %s", g_chatScrollUpDown ? "UP" : "DOWN");
            opts[count]  = scrollTypeLabel;
            enums[count] = kScrollType;
            count++;
        }
    }

    // Common
    opts[count]  = "Remove Chat";
    enums[count] = kRemove;
    count++;

    opts[count]  = "Remove Fav";
    enums[count] = kRemoveFav;
    count++;

    opts[count]  = "Delete Node";
    enums[count] = kDeleteNode;
    count++;

    opts[count]  = "Mark All Read";
    enums[count] = kMarkRead;
    count++;

    opts[count]  = "Node Info";
    enums[count] = kInfo;
    count++;

    opts[count]  = "Back";
    enums[count] = kBack;
    count++;

    BannerOverlayOptions o;
    o.message         = "Menu Chat";
    o.durationMs      = 0;
    o.optionsArrayPtr = opts;
    o.optionsEnumPtr  = enums;
    o.optionsCount    = count;

    o.bannerCallback  = [nodeId](int sel) {
    // Close the banner before changing screens/states
        NotificationRenderer::pauseBanner      = true;
        NotificationRenderer::alertBannerUntil = 1;

        switch (sel) {
        case kPreset:
            if (cannedMessageModule) cannedMessageModule->LaunchWithDestination(nodeId);
            break;

        case kFree:
            if (cannedMessageModule) cannedMessageModule->LaunchFreetextWithDestination(nodeId);
            break;

        case kRemove:
            // Remove chat history only (RAM + persistent)
            chat::ChatHistoryStore::instance().clearDM(nodeId);
            // Also remove persistent file
            {
                std::string filename = "/chat_dm_" + std::to_string(nodeId) + ".txt";
                FSCom.remove(filename.c_str());
            }
            if (screen) screen->setFrames(Screen::FOCUS_PRESERVE);
            break;

        case kRemoveFav:
            if (nodeDB) nodeDB->set_favorite(false, nodeId);
            if (screen) screen->setFrames(Screen::FOCUS_PRESERVE);
            break;

        case kDeleteNode:
            // Completely remove the node from the database
            if (nodeDB) {
                nodeDB->removeNodeByNum(nodeId);
                // Also remove chat history
                chat::ChatHistoryStore::instance().clearDM(nodeId);
                // Also remove persistent chat file
                std::string filename = "/chat_dm_" + std::to_string(nodeId) + ".txt";
                FSCom.remove(filename.c_str());
                if (screen) screen->showSimpleBanner("Node deleted", 1200);
            }
            if (screen) screen->setFrames(Screen::FOCUS_PRESERVE);
            break;

        case kMarkRead:
            // Mark all DM messages as read
            chat::ChatHistoryStore::instance().markAsReadDM(nodeId);
            // Reset scroll to newest message
            if (g_nodeScroll.find(nodeId) != g_nodeScroll.end()) {
                g_nodeScroll[nodeId].scrollIndex = 0;
                g_nodeScroll[nodeId].sel = 0;
            }
            if (screen) screen->showSimpleBanner("All marked as read", 1200);
            if (screen) screen->setFrames(Screen::FOCUS_PRESERVE);
            break;

        case kInfo:
            if (screen) {
                graphics::UIRenderer::currentFavoriteNodeNum = nodeId;
                screen->openNodeInfoFor(nodeId);
            }
            break;

        case kScroll:
            g_chatScrollByPress = !g_chatScrollByPress;
            if (screen) screen->showSimpleBanner(g_chatScrollByPress ? "Scroll Btn: ON" : "Scroll Btn: OFF", 1200);
            break;

        case kScrollType:
            g_chatScrollUpDown = !g_chatScrollUpDown;
            if (screen) screen->showSimpleBanner(g_chatScrollUpDown ? "Scroll Dir: UP" : "Scroll Dir: DOWN", 1200);
            break;

        default:
            break;
        }

        if (screen) screen->forceDisplay(true);
    };

    screen->showOverlayBanner(o);
}

void menuHandler::openChatActionsForChannel(uint8_t ch)
{
    enum { kPreset = 1, kFree = 2, kRemove = 3, kMarkRead = 4, kScroll = 5, kScrollType = 6, kBack = 7 };

    static const char* opts[7];
    static int         enums[7];
    int count = 0;

    // Preset / Freetext according to CardKB
    if (kb_found) {
        opts[count]  = "New Freetext Msg";
        enums[count] = kFree;
        count++;
    } else {
        opts[count]  = "New Preset Msg";
        enums[count] = kPreset;
        count++;
    }

    // Common
    opts[count]  = "Remove Chat";
    enums[count] = kRemove;
    count++;

    opts[count]  = "Mark All Read";
    enums[count] = kMarkRead;
    count++;

    // Scroll Btn only if there is NO CardKB and NO rotary encoder
    static char scrollLabel[24];
    static char scrollTypeLabel[24];
    if (!kb_found && rotaryEncoderInterruptImpl1 == nullptr) {
        snprintf(scrollLabel, sizeof(scrollLabel), "Scroll Btn: %s", g_chatScrollByPress ? "ON" : "OFF");
        opts[count]  = scrollLabel;
        enums[count] = kScroll;
        count++;
        // Show scroll direction option only when scroll button is ON
        if (g_chatScrollByPress) {
            snprintf(scrollTypeLabel, sizeof(scrollTypeLabel), "Scroll Dir: %s", g_chatScrollUpDown ? "UP" : "DOWN");
            opts[count]  = scrollTypeLabel;
            enums[count] = kScrollType;
            count++;
        }
    }

    opts[count]  = "Back";
    enums[count] = kBack;
    count++;

    // Title with channel name (if exists)
    const meshtastic_Channel c = channels.getByIndex(ch);
    const char *cname = (c.settings.name[0]) ? c.settings.name : nullptr;
    char title[64];
    if (cname) snprintf(title, sizeof(title), "Channel: %s", cname);
    else       snprintf(title, sizeof(title), "Channel %u", (unsigned)ch);

    BannerOverlayOptions o;
    o.message         = title;
    o.durationMs      = 0;
    o.optionsArrayPtr = opts;
    o.optionsEnumPtr  = enums;
    o.optionsCount    = count;

    o.bannerCallback  = [ch](int sel) {
    // Close banner before acting (avoids weird states)
        NotificationRenderer::pauseBanner      = true;
        NotificationRenderer::alertBannerUntil = 1;

    // Prepare keyboard header (if input is opened later)
        const meshtastic_Channel cc = channels.getByIndex(ch);
        const char *cname2 = (cc.settings.name[0]) ? cc.settings.name : nullptr;
        char hdr[64];
        if (cname2) snprintf(hdr, sizeof(hdr), "To: %s", cname2);
        else        snprintf(hdr, sizeof(hdr), "To: Channel %u", (unsigned)ch);
        g_pendingKeyboardHeader = hdr;

    // Ensure channel is active and marked as favorite-tab
        channels.setActiveByIndex(ch);
        g_favChannelTabs.insert(ch);

        switch (sel) {
        case kPreset:
            if (cannedMessageModule) cannedMessageModule->LaunchWithDestination(NODENUM_BROADCAST, ch);
            break;
        case kFree:
            if (cannedMessageModule) cannedMessageModule->LaunchFreetextWithDestination(NODENUM_BROADCAST, ch);
            break;
        case kRemove:
            // Remove chat history but maintain channel and frame (RAM + persistent)
            chat::ChatHistoryStore::instance().clearCHAN(ch);
            // Also remove persistent file
            {
                std::string filename = "/chat_ch_" + std::to_string(ch) + ".txt";
                FSCom.remove(filename.c_str());
            }
            if (screen) screen->setFrames(Screen::FOCUS_PRESERVE);
            break;
        case kMarkRead:
            // Mark all channel messages as read
            chat::ChatHistoryStore::instance().markAsReadCHAN(ch);
            // Reset scroll to newest message
            if (g_chanScroll.find(ch) != g_chanScroll.end()) {
                g_chanScroll[ch].scrollIndex = 0;
                g_chanScroll[ch].sel = 0;
            }
            if (screen) screen->showSimpleBanner("All marked as read", 1200);
            if (screen) screen->setFrames(Screen::FOCUS_PRESERVE);
            break;
        case kScroll:
            g_chatScrollByPress = !g_chatScrollByPress;
            if (screen) screen->showSimpleBanner(g_chatScrollByPress ? "Scroll Btn: ON" : "Scroll Btn: OFF", 1200);
            break;
        case kScrollType:
            g_chatScrollUpDown = !g_chatScrollUpDown;
            if (screen) screen->showSimpleBanner(g_chatScrollUpDown ? "Scroll Dir: UP" : "Scroll Dir: DOWN", 1200);
            break;
        default:
            break;
        }

        if (screen) screen->forceDisplay(true);
    };

    screen->showOverlayBanner(o);
}

} // namespace graphics

#endif