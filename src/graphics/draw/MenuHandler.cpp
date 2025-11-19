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

#include "modules/TraceRouteModule.h"
#include <functional>

extern uint16_t TFT_MESH;

namespace graphics
{
menuHandler::screenMenus menuHandler::menuQueue = menu_none;
bool test_enabled = false;
uint8_t test_count = 0;

void menuHandler::loraMenu()
{
    static const char *optionsArray[] = {"Back", "Device Role", "Radio Preset", "LoRa Region"};
    enum optionsNumbers { Back = 0, device_role_picker = 1, radio_preset_picker = 2, lora_picker = 3 };
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "LoRa Actions";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 4;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Back) {
            // No action
        } else if (selected == device_role_picker) {
            menuHandler::menuQueue = menuHandler::device_role_picker;
        } else if (selected == radio_preset_picker) {
            menuHandler::menuQueue = menuHandler::radio_preset_picker;
        } else if (selected == lora_picker) {
            menuHandler::menuQueue = menuHandler::lora_picker;
        }
    };
    screen->showOverlayBanner(bannerOptions);
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
            auto changes = SEGMENT_CONFIG;

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

            if (strncmp(moduleConfig.mqtt.root, default_mqtt_root, strlen(default_mqtt_root)) == 0) {
                //  Default broker is in use, so subscribe to the appropriate MQTT root topic for this region
                sprintf(moduleConfig.mqtt.root, "%s/%s", default_mqtt_root, myRegion->name);
                changes |= SEGMENT_MODULECONFIG;
            }

            service->reloadConfig(changes);
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

void menuHandler::RadioPresetPicker()
{
    static const char *optionsArray[] = {"Back",       "LongSlow",  "LongModerate", "LongFast",  "MediumSlow",
                                         "MediumFast", "ShortSlow", "ShortFast",    "ShortTurbo"};
    enum optionsNumbers {
        Back = 0,
        radiopreset_LongSlow = 1,
        radiopreset_LongModerate = 2,
        radiopreset_LongFast = 3,
        radiopreset_MediumSlow = 4,
        radiopreset_MediumFast = 5,
        radiopreset_ShortSlow = 6,
        radiopreset_ShortFast = 7,
        radiopreset_ShortTurbo = 8
    };
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Radio Preset";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 9;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Back) {
            menuHandler::menuQueue = menuHandler::lora_Menu;
            screen->runNow();
            return;
        } else if (selected == radiopreset_LongSlow) {
            config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
        } else if (selected == radiopreset_LongModerate) {
            config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE;
        } else if (selected == radiopreset_LongFast) {
            config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
        } else if (selected == radiopreset_MediumSlow) {
            config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW;
        } else if (selected == radiopreset_MediumFast) {
            config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
        } else if (selected == radiopreset_ShortSlow) {
            config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW;
        } else if (selected == radiopreset_ShortFast) {
            config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST;
        } else if (selected == radiopreset_ShortTurbo) {
            config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;
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
            if (uiconfig.screen_brightness > 0) {
                uiconfig.screen_brightness = 0;
                io.digitalWrite(PCA_PIN_EINK_EN, LOW);
            } else {
                uiconfig.screen_brightness = 1;
                io.digitalWrite(PCA_PIN_EINK_EN, HIGH);
            }
            saveUIConfig();
#endif
        } else if (selected == Sleep) {
            screen->setOn(false);
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

void menuHandler::textMessageMenu()
{
    cannedMessageModule->LaunchWithDestination(NODENUM_BROADCAST);
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
    enum optionsNumbers { Back, Notifications, ScreenOptions, Bluetooth, PowerMenu, Test, enumEnd };
    static const char *optionsArray[enumEnd] = {"Back"};
    static int optionsEnumArray[enumEnd] = {Back};
    int options = 1;

    optionsArray[options] = "Notifications";
    optionsEnumArray[options++] = Notifications;
    optionsArray[options] = "Display Options";
    optionsEnumArray[options++] = ScreenOptions;

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
        if (selected == Notifications) {
            menuHandler::menuQueue = menuHandler::notifications_menu;
            screen->runNow();
        } else if (selected == ScreenOptions) {
            menuHandler::menuQueue = menuHandler::screen_options_menu;
            screen->runNow();
        } else if (selected == PowerMenu) {
            menuHandler::menuQueue = menuHandler::power_menu;
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

void menuHandler::nodeNameLengthMenu()
{
    enum OptionsNumbers { Back, Long, Short };
    static const char *optionsArray[] = {"Back", "Long", "Short"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Node Name Length";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Long) {
            // Set names to long
            LOG_INFO("Setting names to long");
            config.display.use_long_node_name = true;
        } else if (selected == Short) {
            // Set names to short
            LOG_INFO("Setting names to short");
            config.display.use_long_node_name = false;
        } else if (selected == Back) {
            menuQueue = screen_options_menu;
            screen->runNow();
        }
    };
    bannerOptions.InitialSelected = config.display.use_long_node_name == true ? 1 : 2;
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::resetNodeDBMenu()
{
    static const char *optionsArray[] = {"Back", "Reset All", "Preserve Favorites"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Confirm Reset NodeDB";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1 || selected == 2) {
            disableBluetooth();
            screen->setFrames(Screen::FOCUS_DEFAULT);
        }
        if (selected == 1) {
            LOG_INFO("Initiate node-db reset");
            nodeDB->resetNodes();
            rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        } else if (selected == 2) {
            LOG_INFO("Initiate node-db reset but keeping favorites");
            nodeDB->resetNodes(1);
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
            saveUIConfig();
            service->reloadConfig(SEGMENT_CONFIG);
        } else if (selected == 2) {
            uiconfig.gps_format = meshtastic_DeviceUIConfig_GpsCoordinateFormat_DMS;
            saveUIConfig();
            service->reloadConfig(SEGMENT_CONFIG);
        } else if (selected == 3) {
            uiconfig.gps_format = meshtastic_DeviceUIConfig_GpsCoordinateFormat_UTM;
            saveUIConfig();
            service->reloadConfig(SEGMENT_CONFIG);
        } else if (selected == 4) {
            uiconfig.gps_format = meshtastic_DeviceUIConfig_GpsCoordinateFormat_MGRS;
            saveUIConfig();
            service->reloadConfig(SEGMENT_CONFIG);
        } else if (selected == 5) {
            uiconfig.gps_format = meshtastic_DeviceUIConfig_GpsCoordinateFormat_OLC;
            saveUIConfig();
            service->reloadConfig(SEGMENT_CONFIG);
        } else if (selected == 6) {
            uiconfig.gps_format = meshtastic_DeviceUIConfig_GpsCoordinateFormat_OSGR;
            saveUIConfig();
            service->reloadConfig(SEGMENT_CONFIG);
        } else if (selected == 7) {
            uiconfig.gps_format = meshtastic_DeviceUIConfig_GpsCoordinateFormat_MLS;
            saveUIConfig();
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
        if (selected == 0)
            return;
        else if (selected != (config.bluetooth.enabled ? 1 : 2)) {
            InputEvent event = {.inputEvent = (input_broker_event)170, .kbchar = 170, .touchX = 0, .touchY = 0};
            inputBroker->injectInputEvent(&event);
        }
    };
    bannerOptions.InitialSelected = config.bluetooth.enabled ? 1 : 2;
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::BuzzerModeMenu()
{
    static const char *optionsArray[] = {"All Enabled", "Disabled", "Notifications", "System Only", "DMs Only"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Buzzer Mode";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 5;
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

    enum optionsNumbers { Back, NodeNameLength, Brightness, ScreenColor, FrameToggles, DisplayUnits };
    static const char *optionsArray[5] = {"Back"};
    static int optionsEnumArray[5] = {Back};
    int options = 1;

#if defined(T_DECK) || defined(T_LORA_PAGER)
    optionsArray[options] = "Show Long/Short Name";
    optionsEnumArray[options++] = NodeNameLength;
#endif

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

    optionsArray[options] = "Frame Visiblity Toggle";
    optionsEnumArray[options++] = FrameToggles;

    optionsArray[options] = "Display Units";
    optionsEnumArray[options++] = DisplayUnits;

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Display Options";
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
        } else if (selected == NodeNameLength) {
            menuHandler::menuQueue = menuHandler::node_name_length_menu;
            screen->runNow();
        } else if (selected == FrameToggles) {
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        } else if (selected == DisplayUnits) {
            menuHandler::menuQueue = menuHandler::DisplayUnits;
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
        show_telemetry,
        show_power,
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

    optionsArray[options] = moduleConfig.telemetry.environment_screen_enabled ? "Hide Telemetry" : "Show Telemetry";
    optionsEnumArray[options++] = show_telemetry;

    optionsArray[options] = moduleConfig.telemetry.power_screen_enabled ? "Hide Power" : "Show Power";
    optionsEnumArray[options++] = show_power;

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
        } else if (selected == show_telemetry) {
            moduleConfig.telemetry.environment_screen_enabled = !moduleConfig.telemetry.environment_screen_enabled;
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        } else if (selected == show_power) {
            moduleConfig.telemetry.power_screen_enabled = !moduleConfig.telemetry.power_screen_enabled;
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::DisplayUnits_menu()
{
    enum optionsNumbers { Back, MetricUnits, ImperialUnits };

    static const char *optionsArray[] = {"Back", "Metric", "Imperial"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = " Select display units";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL)
        bannerOptions.InitialSelected = 2;
    else
        bannerOptions.InitialSelected = 1;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == MetricUnits) {
            config.display.units = meshtastic_Config_DisplayConfig_DisplayUnits_METRIC;
            service->reloadConfig(SEGMENT_CONFIG);
        } else if (selected == ImperialUnits) {
            config.display.units = meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL;
            service->reloadConfig(SEGMENT_CONFIG);
        } else {
            menuHandler::menuQueue = menuHandler::screen_options_menu;
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
    case radio_preset_picker:
        RadioPresetPicker();
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
    case position_base_menu:
        positionBaseMenu();
        break;
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
    case node_name_length_menu:
        nodeNameLengthMenu();
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
    case DisplayUnits:
        DisplayUnits_menu();
        break;
    case throttle_message:
        screen->showSimpleBanner("Too Many Attempts\nTry again in 60 seconds.", 5000);
        break;
    }
    menuQueue = menu_none;
}

void menuHandler::saveUIConfig()
{
    nodeDB->saveProto("/prefs/uiconfig.proto", meshtastic_DeviceUIConfig_size, &meshtastic_DeviceUIConfig_msg, &uiconfig);
}

} // namespace graphics

#endif