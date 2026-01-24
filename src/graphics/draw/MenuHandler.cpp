#include "configuration.h"
#if HAS_SCREEN
#include "ClockRenderer.h"
#include "Default.h"
#include "GPS.h"
#include "MenuHandler.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "buzz.h"
#include "graphics/Screen.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/draw/MessageRenderer.h"
#include "graphics/draw/UIRenderer.h"
#include "input/RotaryEncoderInterruptImpl1.h"
#include "input/UpDownInterruptImpl1.h"
#include "main.h"
#include "mesh/Default.h"
#include "mesh/MeshTypes.h"
#include "modules/AdminModule.h"
#include "modules/CannedMessageModule.h"
#include "modules/ExternalNotificationModule.h"
#include "modules/KeyVerificationModule.h"
#include "modules/TraceRouteModule.h"
#include <algorithm>
#include <array>
#include <functional>
#include <utility>

extern uint16_t TFT_MESH;

namespace graphics
{

namespace
{

// Caller must ensure the provided options array outlives the banner callback.
template <typename T, size_t N, typename Callback>
BannerOverlayOptions createStaticBannerOptions(const char *message, const MenuOption<T> (&options)[N],
                                               std::array<const char *, N> &labels, Callback &&onSelection)
{
    for (size_t i = 0; i < N; ++i) {
        labels[i] = options[i].label;
    }

    const MenuOption<T> *optionsPtr = options;
    auto callback = std::function<void(const MenuOption<T> &, int)>(std::forward<Callback>(onSelection));

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = message;
    bannerOptions.optionsArrayPtr = labels.data();
    bannerOptions.optionsCount = static_cast<uint8_t>(N);
    bannerOptions.bannerCallback = [optionsPtr, callback](int selected) -> void { callback(optionsPtr[selected], selected); };
    return bannerOptions;
}

} // namespace

menuHandler::screenMenus menuHandler::menuQueue = menu_none;
uint32_t menuHandler::pickedNodeNum = 0;
bool test_enabled = false;
uint8_t test_count = 0;

void menuHandler::loraMenu()
{
    static const char *optionsArray[] = {"Back", "Device Role", "Radio Preset", "Frequency Slot", "LoRa Region"};
    enum optionsNumbers { Back = 0, device_role_picker = 1, radio_preset_picker = 2, frequency_slot = 3, lora_picker = 4 };
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "LoRa Actions";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 5;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Back) {
            // No action
        } else if (selected == device_role_picker) {
            menuHandler::menuQueue = menuHandler::device_role_picker;
        } else if (selected == radio_preset_picker) {
            menuHandler::menuQueue = menuHandler::radio_preset_picker;
        } else if (selected == frequency_slot) {
            menuHandler::menuQueue = menuHandler::frequency_slot;
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
    static const LoraRegionOption regionOptions[] = {
        {"Back", OptionsAction::Back},
        {"US", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_US},
        {"EU_433", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_EU_433},
        {"EU_868", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_EU_868},
        {"CN", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_CN},
        {"JP", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_JP},
        {"ANZ", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_ANZ},
        {"KR", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_KR},
        {"TW", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_TW},
        {"RU", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_RU},
        {"IN", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_IN},
        {"NZ_865", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_NZ_865},
        {"TH", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_TH},
        {"LORA_24", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_LORA_24},
        {"UA_433", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_UA_433},
        {"UA_868", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_UA_868},
        {"MY_433", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_MY_433},
        {"MY_919", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_MY_919},
        {"SG_923", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_SG_923},
        {"PH_433", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_PH_433},
        {"PH_868", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_PH_868},
        {"PH_915", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_PH_915},
        {"ANZ_433", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_ANZ_433},
        {"KZ_433", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_KZ_433},
        {"KZ_863", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_KZ_863},
        {"NP_865", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_NP_865},
        {"BR_902", OptionsAction::Select, meshtastic_Config_LoRaConfig_RegionCode_BR_902},
    };

    constexpr size_t regionCount = sizeof(regionOptions) / sizeof(regionOptions[0]);
    static std::array<const char *, regionCount> regionLabels{};

    const char *bannerMessage = "Set the LoRa region";
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerMessage = "LoRa Region";
    }

    auto bannerOptions =
        createStaticBannerOptions(bannerMessage, regionOptions, regionLabels, [](const LoraRegionOption &option, int) -> void {
            if (!option.hasValue) {
                return;
            }

            auto selectedRegion = option.value;
            if (config.lora.region == selectedRegion) {
                return;
            }

            config.lora.region = selectedRegion;
            auto changes = SEGMENT_CONFIG;

        // FIXME: This should be a method consolidated with the same logic in the admin message as well
        // This is needed as we wait til picking the LoRa region to generate keys for the first time.
#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN || MESHTASTIC_EXCLUDE_PKI)
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
#endif
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
        });

    bannerOptions.durationMs = duration;

    int initialSelection = 0;
    for (size_t i = 0; i < regionCount; ++i) {
        if (regionOptions[i].hasValue && regionOptions[i].value == config.lora.region) {
            initialSelection = static_cast<int>(i);
            break;
        }
    }
    bannerOptions.InitialSelected = initialSelection;

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

void menuHandler::FrequencySlotPicker()
{

    enum ReplyOptions : int { Back = -1 };
    constexpr int MAX_CHANNEL_OPTIONS = 202;
    static const char *optionsArray[MAX_CHANNEL_OPTIONS];
    static int optionsEnumArray[MAX_CHANNEL_OPTIONS];
    static char channelText[MAX_CHANNEL_OPTIONS - 1][12];
    int options = 0;
    optionsArray[options] = "Back";
    optionsEnumArray[options++] = Back;
    optionsArray[options] = "Slot 0 (Auto)";
    optionsEnumArray[options++] = 0;

    // Calculate number of channels (copied from RadioInterface::applyModemConfig())
    meshtastic_Config_LoRaConfig &loraConfig = config.lora;
    double bw = loraConfig.bandwidth;
    if (loraConfig.use_preset) {
        switch (loraConfig.modem_preset) {
        case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
            bw = (myRegion->wideLora) ? 1625.0 : 500;
            break;
        case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
            bw = (myRegion->wideLora) ? 812.5 : 250;
            break;
        case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
            bw = (myRegion->wideLora) ? 812.5 : 250;
            break;
        case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
            bw = (myRegion->wideLora) ? 812.5 : 250;
            break;
        case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
            bw = (myRegion->wideLora) ? 812.5 : 250;
            break;
        case meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO:
            bw = (myRegion->wideLora) ? 1625.0 : 500;
            break;
        default:
            bw = (myRegion->wideLora) ? 812.5 : 250;
            break;
        case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
            bw = (myRegion->wideLora) ? 406.25 : 125;
            break;
        case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
            bw = (myRegion->wideLora) ? 406.25 : 125;
            break;
        }
    } else {
        bw = loraConfig.bandwidth;
        if (bw == 31) // This parameter is not an integer
            bw = 31.25;
        if (bw == 62) // Fix for 62.5Khz bandwidth
            bw = 62.5;
        if (bw == 200)
            bw = 203.125;
        if (bw == 400)
            bw = 406.25;
        if (bw == 800)
            bw = 812.5;
        if (bw == 1600)
            bw = 1625.0;
    }

    uint32_t numChannels = 0;
    if (myRegion) {
        numChannels = (uint32_t)floor((myRegion->freqEnd - myRegion->freqStart) / (myRegion->spacing + (bw / 1000.0)));
    } else {
        LOG_WARN("Region not set, cannot calculate number of channels");
        return;
    }

    if (numChannels > (uint32_t)(MAX_CHANNEL_OPTIONS - 2))
        numChannels = (uint32_t)(MAX_CHANNEL_OPTIONS - 2);

    for (uint32_t ch = 1; ch <= numChannels; ch++) {
        snprintf(channelText[ch - 1], sizeof(channelText[ch - 1]), "Slot %lu", (unsigned long)ch);
        optionsArray[options] = channelText[ch - 1];
        optionsEnumArray[options++] = (int)ch;
    }

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Frequency Slot";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.optionsCount = options;

    // Start highlight on current channel if possible, otherwise on "1"
    int initial = (int)config.lora.channel_num + 1;
    if (initial < 2 || initial > (int)numChannels + 1)
        initial = 1;
    bannerOptions.InitialSelected = initial;

    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Back) {
            menuHandler::menuQueue = menuHandler::lora_Menu;
            screen->runNow();
            return;
        }

        config.lora.channel_num = selected;
        service->reloadConfig(SEGMENT_CONFIG);
        rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
    };

    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::RadioPresetPicker()
{
    static const RadioPresetOption presetOptions[] = {
        {"Back", OptionsAction::Back},
        {"LongTurbo", OptionsAction::Select, meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO},
        {"LongModerate", OptionsAction::Select, meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE},
        {"LongFast", OptionsAction::Select, meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST},
        {"MediumSlow", OptionsAction::Select, meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW},
        {"MediumFast", OptionsAction::Select, meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST},
        {"ShortSlow", OptionsAction::Select, meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW},
        {"ShortFast", OptionsAction::Select, meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST},
        {"ShortTurbo", OptionsAction::Select, meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO},
    };

    constexpr size_t presetCount = sizeof(presetOptions) / sizeof(presetOptions[0]);
    static std::array<const char *, presetCount> presetLabels{};

    auto bannerOptions =
        createStaticBannerOptions("Radio Preset", presetOptions, presetLabels, [](const RadioPresetOption &option, int) -> void {
            if (option.action == OptionsAction::Back) {
                menuHandler::menuQueue = menuHandler::lora_Menu;
                screen->runNow();
                return;
            }

            if (!option.hasValue) {
                return;
            }

            config.lora.modem_preset = option.value;
            config.lora.channel_num = 0;        // Reset to default channel for the preset
            config.lora.override_frequency = 0; // Clear any custom frequency
            service->reloadConfig(SEGMENT_CONFIG);
            rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        });

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
    static const ClockFaceOption clockFaceOptions[] = {
        {"Back", OptionsAction::Back},
        {"Digital", OptionsAction::Select, false},
        {"Analog", OptionsAction::Select, true},
    };

    constexpr size_t clockFaceCount = sizeof(clockFaceOptions) / sizeof(clockFaceOptions[0]);
    static std::array<const char *, clockFaceCount> clockFaceLabels{};

    auto bannerOptions = createStaticBannerOptions("Which Face?", clockFaceOptions, clockFaceLabels,
                                                   [](const ClockFaceOption &option, int) -> void {
                                                       if (option.action == OptionsAction::Back) {
                                                           menuHandler::menuQueue = menuHandler::clock_menu;
                                                           screen->runNow();
                                                           return;
                                                       }

                                                       if (!option.hasValue) {
                                                           return;
                                                       }

                                                       if (uiconfig.is_clockface_analog == option.value) {
                                                           return;
                                                       }

                                                       uiconfig.is_clockface_analog = option.value;
                                                       saveUIConfig();
                                                       screen->setFrames(Screen::FOCUS_CLOCK);
                                                   });

    bannerOptions.InitialSelected = uiconfig.is_clockface_analog ? 2 : 1;
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::TZPicker()
{
    static const TimezoneOption timezoneOptions[] = {
        {"Back", OptionsAction::Back},
        {"US/Hawaii", OptionsAction::Select, "HST10"},
        {"US/Alaska", OptionsAction::Select, "AKST9AKDT,M3.2.0,M11.1.0"},
        {"US/Pacific", OptionsAction::Select, "PST8PDT,M3.2.0,M11.1.0"},
        {"US/Arizona", OptionsAction::Select, "MST7"},
        {"US/Mountain", OptionsAction::Select, "MST7MDT,M3.2.0,M11.1.0"},
        {"US/Central", OptionsAction::Select, "CST6CDT,M3.2.0,M11.1.0"},
        {"US/Eastern", OptionsAction::Select, "EST5EDT,M3.2.0,M11.1.0"},
        {"BR/Brasilia", OptionsAction::Select, "BRT3"},
        {"UTC", OptionsAction::Select, "UTC0"},
        {"EU/Western", OptionsAction::Select, "GMT0BST,M3.5.0/1,M10.5.0"},
        {"EU/Central", OptionsAction::Select, "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"EU/Eastern", OptionsAction::Select, "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        {"Asia/Kolkata", OptionsAction::Select, "IST-5:30"},
        {"Asia/Hong_Kong", OptionsAction::Select, "HKT-8"},
        {"AU/AWST", OptionsAction::Select, "AWST-8"},
        {"AU/ACST", OptionsAction::Select, "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
        {"AU/AEST", OptionsAction::Select, "AEST-10AEDT,M10.1.0,M4.1.0/3"},
        {"Pacific/NZ", OptionsAction::Select, "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    };

    constexpr size_t timezoneCount = sizeof(timezoneOptions) / sizeof(timezoneOptions[0]);
    static std::array<const char *, timezoneCount> timezoneLabels{};

    auto bannerOptions = createStaticBannerOptions(
        "Pick Timezone", timezoneOptions, timezoneLabels, [](const TimezoneOption &option, int) -> void {
            if (option.action == OptionsAction::Back) {
                menuHandler::menuQueue = menuHandler::clock_menu;
                screen->runNow();
                return;
            }

            if (!option.hasValue) {
                return;
            }

            if (strncmp(config.device.tzdef, option.value, sizeof(config.device.tzdef)) == 0) {
                return;
            }

            strncpy(config.device.tzdef, option.value, sizeof(config.device.tzdef));
            config.device.tzdef[sizeof(config.device.tzdef) - 1] = '\0';

            setenv("TZ", config.device.tzdef, 1);
            service->reloadConfig(SEGMENT_CONFIG);
        });

    int initialSelection = 0;
    for (size_t i = 0; i < timezoneCount; ++i) {
        if (timezoneOptions[i].hasValue &&
            strncmp(config.device.tzdef, timezoneOptions[i].value, sizeof(config.device.tzdef)) == 0) {
            initialSelection = static_cast<int>(i);
            break;
        }
    }
    bannerOptions.InitialSelected = initialSelection;

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
    enum optionsNumbers { Back = 0, ViewMode, DeleteMenu, ReplyMenu, MuteChannel, Aloud, enumEnd };

    static const char *optionsArray[enumEnd];
    static int optionsEnumArray[enumEnd];
    int options = 0;

    auto mode = graphics::MessageRenderer::getThreadMode();
    int threadChannel = graphics::MessageRenderer::getThreadChannel();

    optionsArray[options] = "Back";
    optionsEnumArray[options++] = Back;

    // New Reply submenu (replaces Preset and Freetext directly in this menu)
    optionsArray[options] = "Reply";
    optionsEnumArray[options++] = ReplyMenu;

    optionsArray[options] = "View Chats";
    optionsEnumArray[options++] = ViewMode;

    // If viewing ALL chats, hide “Mute Chat”
    if (mode != graphics::MessageRenderer::ThreadMode::ALL && mode != graphics::MessageRenderer::ThreadMode::DIRECT) {
        const uint8_t chIndex = (threadChannel != 0) ? (uint8_t)threadChannel : channels.getPrimaryIndex();
        auto &chan = channels.getByIndex(chIndex);

        optionsArray[options] = chan.settings.module_settings.is_muted ? "Unmute Channel" : "Mute Channel";
        optionsEnumArray[options++] = MuteChannel;
    }

    // Delete submenu
    optionsArray[options] = "Delete";
    optionsEnumArray[options++] = DeleteMenu;

#ifdef HAS_I2S
    optionsArray[options] = "Read Aloud";
    optionsEnumArray[options++] = Aloud;
#endif

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Message Action";
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = "Message";
    }
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [](int selected) -> void {
        LOG_DEBUG("messageResponseMenu: selected %d", selected);

        auto mode = graphics::MessageRenderer::getThreadMode();
        int ch = graphics::MessageRenderer::getThreadChannel();
        uint32_t peer = graphics::MessageRenderer::getThreadPeer();

        LOG_DEBUG("[ReplyCtx] mode=%d ch=%d peer=0x%08x", (int)mode, ch, (unsigned int)peer);

        if (selected == ViewMode) {
            menuHandler::menuQueue = menuHandler::message_viewmode_menu;
            screen->runNow();

            // Reply submenu
        } else if (selected == ReplyMenu) {
            menuHandler::menuQueue = menuHandler::reply_menu;
            screen->runNow();

        } else if (selected == MuteChannel) {
            const uint8_t chIndex = (ch != 0) ? (uint8_t)ch : channels.getPrimaryIndex();
            auto &chan = channels.getByIndex(chIndex);
            if (chan.settings.has_module_settings) {
                chan.settings.module_settings.is_muted = !chan.settings.module_settings.is_muted;
                nodeDB->saveToDisk();
            }

        } else if (selected == DeleteMenu) {
            menuHandler::menuQueue = menuHandler::delete_messages_menu;
            screen->runNow();

#ifdef HAS_I2S
        } else if (selected == Aloud) {
            const meshtastic_MeshPacket &mp = devicestate.rx_text_message;
            const char *msg = reinterpret_cast<const char *>(mp.decoded.payload.bytes);
            audioThread->readAloud(msg);
#endif
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::replyMenu()
{
    enum replyOptions { Back = 0, ReplyPreset, ReplyFreetext, enumEnd };

    static const char *optionsArray[enumEnd];
    static int optionsEnumArray[enumEnd];
    int options = 0;

    // Back
    optionsArray[options] = "Back";
    optionsEnumArray[options++] = Back;

    // Preset reply
    optionsArray[options] = "With Preset";
    optionsEnumArray[options++] = ReplyPreset;

    // Freetext reply (only when keyboard exists)
    if (kb_found) {
        optionsArray[options] = "With Freetext";
        optionsEnumArray[options++] = ReplyFreetext;
    }

    BannerOverlayOptions bannerOptions;

    // Dynamic title based on thread mode
    auto mode = graphics::MessageRenderer::getThreadMode();
    if (mode == graphics::MessageRenderer::ThreadMode::CHANNEL) {
        bannerOptions.message = "Reply to Channel";
    } else if (mode == graphics::MessageRenderer::ThreadMode::DIRECT) {
        bannerOptions.message = "Reply to DM";
    } else {
        // View All
        bannerOptions.message = "Reply to Last Msg";
    }

    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.optionsCount = options;
    bannerOptions.InitialSelected = 1;

    bannerOptions.bannerCallback = [](int selected) -> void {
        auto mode = graphics::MessageRenderer::getThreadMode();
        int ch = graphics::MessageRenderer::getThreadChannel();
        uint32_t peer = graphics::MessageRenderer::getThreadPeer();

        if (selected == Back) {
            menuHandler::menuQueue = menuHandler::message_response_menu;
            screen->runNow();
            return;
        }

        // Preset reply
        if (selected == ReplyPreset) {

            if (mode == graphics::MessageRenderer::ThreadMode::CHANNEL) {
                cannedMessageModule->LaunchWithDestination(NODENUM_BROADCAST, ch);

            } else if (mode == graphics::MessageRenderer::ThreadMode::DIRECT) {
                cannedMessageModule->LaunchWithDestination(peer);

            } else {
                // Fallback for last received message
                if (devicestate.rx_text_message.to == NODENUM_BROADCAST) {
                    cannedMessageModule->LaunchWithDestination(NODENUM_BROADCAST, devicestate.rx_text_message.channel);
                } else {
                    cannedMessageModule->LaunchWithDestination(devicestate.rx_text_message.from);
                }
            }

            return;
        }

        // Freetext reply
        if (selected == ReplyFreetext) {

            if (mode == graphics::MessageRenderer::ThreadMode::CHANNEL) {
                cannedMessageModule->LaunchFreetextWithDestination(NODENUM_BROADCAST, ch);

            } else if (mode == graphics::MessageRenderer::ThreadMode::DIRECT) {
                cannedMessageModule->LaunchFreetextWithDestination(peer);

            } else {
                // Fallback for last received message
                if (devicestate.rx_text_message.to == NODENUM_BROADCAST) {
                    cannedMessageModule->LaunchFreetextWithDestination(NODENUM_BROADCAST, devicestate.rx_text_message.channel);
                } else {
                    cannedMessageModule->LaunchFreetextWithDestination(devicestate.rx_text_message.from);
                }
            }

            return;
        }
    };
    screen->showOverlayBanner(bannerOptions);
}
void menuHandler::deleteMessagesMenu()
{
    enum optionsNumbers { Back = 0, DeleteOldest, DeleteThis, DeleteAll, enumEnd };

    static const char *optionsArray[enumEnd];
    static int optionsEnumArray[enumEnd];
    int options = 0;

    auto mode = graphics::MessageRenderer::getThreadMode();

    optionsArray[options] = "Back";
    optionsEnumArray[options++] = Back;

    optionsArray[options] = "Delete Oldest";
    optionsEnumArray[options++] = DeleteOldest;

    // If viewing ALL chats → hide “Delete This Chat”
    if (mode != graphics::MessageRenderer::ThreadMode::ALL) {
        optionsArray[options] = "Delete This Chat";
        optionsEnumArray[options++] = DeleteThis;
    }
    if (currentResolution == ScreenResolution::UltraLow) {
        optionsArray[options] = "Delete All";
    } else {
        optionsArray[options] = "Delete All Chats";
    }
    optionsEnumArray[options++] = DeleteAll;

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Delete Messages";

    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [mode](int selected) -> void {
        int ch = graphics::MessageRenderer::getThreadChannel();
        uint32_t peer = graphics::MessageRenderer::getThreadPeer();

        if (selected == Back) {
            menuHandler::menuQueue = menuHandler::message_response_menu;
            screen->runNow();
            return;
        }

        if (selected == DeleteAll) {
            LOG_INFO("Deleting all messages");
            messageStore.clearAllMessages();
            graphics::MessageRenderer::clearThreadRegistries();
            graphics::MessageRenderer::clearMessageCache();
            return;
        }

        if (selected == DeleteOldest) {
            LOG_INFO("Deleting oldest message");

            if (mode == graphics::MessageRenderer::ThreadMode::ALL) {
                messageStore.deleteOldestMessage();
            } else if (mode == graphics::MessageRenderer::ThreadMode::CHANNEL) {
                messageStore.deleteOldestMessageInChannel(ch);
            } else if (mode == graphics::MessageRenderer::ThreadMode::DIRECT) {
                messageStore.deleteOldestMessageWithPeer(peer);
            }
            return;
        }

        // This only appears in non-ALL modes
        if (selected == DeleteThis) {
            LOG_INFO("Deleting all messages in this thread");

            if (mode == graphics::MessageRenderer::ThreadMode::CHANNEL) {
                messageStore.deleteAllMessagesInChannel(ch);
            } else if (mode == graphics::MessageRenderer::ThreadMode::DIRECT) {
                messageStore.deleteAllMessagesWithPeer(peer);
            }
            return;
        }
    };

    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::messageViewModeMenu()
{
    auto encodeChannelId = [](int ch) -> int { return 100 + ch; };
    auto isChannelSel = [](int id) -> bool { return id >= 100 && id < 200; };

    static std::vector<std::string> labels;
    static std::vector<int> ids;
    static std::vector<uint32_t> idToPeer; // DM lookup

    labels.clear();
    ids.clear();
    idToPeer.clear();

    labels.push_back("Back");
    ids.push_back(-1);
    labels.push_back("View All Chats");
    ids.push_back(-2);

    // Channels with messages
    for (int ch = 0; ch < 8; ++ch) {
        auto msgs = messageStore.getChannelMessages((uint8_t)ch);
        if (!msgs.empty()) {
            char buf[40];
            const char *cname = channels.getName(ch);
            snprintf(buf, sizeof(buf), cname && cname[0] ? "#%s" : "#Ch%d", cname ? cname : "", ch);
            labels.push_back(buf);
            ids.push_back(encodeChannelId(ch));
            LOG_DEBUG("messageViewModeMenu: Added live channel %s (id=%d)", buf, encodeChannelId(ch));
        }
    }

    // Registry channels
    for (int ch : graphics::MessageRenderer::getSeenChannels()) {
        if (ch < 0 || ch >= 8)
            continue;
        auto msgs = messageStore.getChannelMessages((uint8_t)ch);
        if (msgs.empty())
            continue;
        int enc = encodeChannelId(ch);
        if (std::find(ids.begin(), ids.end(), enc) == ids.end()) {
            char buf[40];
            const char *cname = channels.getName(ch);
            snprintf(buf, sizeof(buf), cname && cname[0] ? "#%s" : "#Ch%d", cname ? cname : "", ch);
            labels.push_back(buf);
            ids.push_back(enc);
            LOG_DEBUG("messageViewModeMenu: Added registry channel %s (id=%d)", buf, enc);
        }
    }

    // Gather unique peers
    auto dms = messageStore.getDirectMessages();
    std::vector<uint32_t> uniquePeers;
    for (auto &m : dms) {
        uint32_t peer = (m.sender == nodeDB->getNodeNum()) ? m.dest : m.sender;
        if (peer != nodeDB->getNodeNum() && std::find(uniquePeers.begin(), uniquePeers.end(), peer) == uniquePeers.end())
            uniquePeers.push_back(peer);
    }
    for (uint32_t peer : graphics::MessageRenderer::getSeenPeers()) {
        if (peer != nodeDB->getNodeNum() && std::find(uniquePeers.begin(), uniquePeers.end(), peer) == uniquePeers.end())
            uniquePeers.push_back(peer);
    }
    std::sort(uniquePeers.begin(), uniquePeers.end());

    // Encode peers
    for (size_t i = 0; i < uniquePeers.size(); ++i) {
        uint32_t peer = uniquePeers[i];
        auto node = nodeDB->getMeshNode(peer);
        std::string name;
        if (node && node->has_user)
            name = sanitizeString(node->user.long_name).substr(0, 15);
        else {
            char buf[20];
            snprintf(buf, sizeof(buf), "Node %08X", peer);
            name = buf;
        }
        labels.push_back("@" + name);
        int encPeer = 1000 + (int)idToPeer.size();
        ids.push_back(encPeer);
        idToPeer.push_back(peer);
        LOG_DEBUG("messageViewModeMenu: Added DM %s peer=0x%08x id=%d", name.c_str(), (unsigned int)peer, encPeer);
    }

    // Active ID
    int activeId = -2;
    auto mode = graphics::MessageRenderer::getThreadMode();
    if (mode == graphics::MessageRenderer::ThreadMode::CHANNEL)
        activeId = encodeChannelId(graphics::MessageRenderer::getThreadChannel());
    else if (mode == graphics::MessageRenderer::ThreadMode::DIRECT) {
        uint32_t cur = graphics::MessageRenderer::getThreadPeer();
        for (size_t i = 0; i < idToPeer.size(); ++i)
            if (idToPeer[i] == cur) {
                activeId = 1000 + (int)i;
                break;
            }
    }

    LOG_DEBUG("messageViewModeMenu: Active thread id=%d", activeId);

    // Build banner
    static std::vector<const char *> options;
    static std::vector<int> optionIds;
    options.clear();
    optionIds.clear();

    int initialIndex = 0;
    for (size_t i = 0; i < labels.size(); i++) {
        options.push_back(labels[i].c_str());
        optionIds.push_back(ids[i]);
        if (ids[i] == activeId)
            initialIndex = (int)i;
    }

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Select Conversation";
    bannerOptions.optionsArrayPtr = options.data();
    bannerOptions.optionsEnumPtr = optionIds.data();
    bannerOptions.optionsCount = options.size();
    bannerOptions.InitialSelected = initialIndex;

    bannerOptions.bannerCallback = [=](int selected) -> void {
        LOG_DEBUG("messageViewModeMenu: selected=%d", selected);
        if (selected == -1) {
            menuHandler::menuQueue = menuHandler::message_response_menu;
            screen->runNow();
        } else if (selected == -2) {
            graphics::MessageRenderer::setThreadMode(graphics::MessageRenderer::ThreadMode::ALL);
        } else if (isChannelSel(selected)) {
            int ch = selected - 100;
            graphics::MessageRenderer::setThreadMode(graphics::MessageRenderer::ThreadMode::CHANNEL, ch);
        } else if (selected >= 1000) {
            int idx = selected - 1000;
            if (idx >= 0 && (size_t)idx < idToPeer.size()) {
                uint32_t peer = idToPeer[idx];
                graphics::MessageRenderer::setThreadMode(graphics::MessageRenderer::ThreadMode::DIRECT, -1, peer);
            }
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::homeBaseMenu()
{
    enum optionsNumbers { Back, Mute, Backlight, Position, Preset, Freetext, Sleep, enumEnd };

    static const char *optionsArray[enumEnd] = {"Back"};
    static int optionsEnumArray[enumEnd] = {Back};
    int options = 1;

    if (moduleConfig.external_notification.enabled && externalNotificationModule &&
        config.device.buzzer_mode != meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED) {
        if (!externalNotificationModule->getMute()) {
            optionsArray[options] = "Temporarily Mute";
        } else {
            optionsArray[options] = "Unmute";
        }
        optionsEnumArray[options++] = Mute;
    }
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

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Home Action";
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = "Home";
    }
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Mute) {
            if (moduleConfig.external_notification.enabled && externalNotificationModule) {
                externalNotificationModule->setMute(!externalNotificationModule->getMute());
                IF_SCREEN(if (!externalNotificationModule->getMute()) externalNotificationModule->stopNow();)
            }
        } else if (selected == Backlight) {
            screen->setOn(false);
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
            service->refreshLocalMeshNode();
            if (service->trySendPosition(NODENUM_BROADCAST, true)) {
                IF_SCREEN(screen->showSimpleBanner("Position\nSent", 3000));
            } else {
                IF_SCREEN(screen->showSimpleBanner("Node Info\nSent", 3000));
            }
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
    enum optionsNumbers { Back, Notifications, ScreenOptions, Bluetooth, WiFiToggle, PowerMenu, Test, enumEnd };
    static const char *optionsArray[enumEnd] = {"Back"};
    static int optionsEnumArray[enumEnd] = {Back};
    int options = 1;

    optionsArray[options] = "Notifications";
    optionsEnumArray[options++] = Notifications;

    optionsArray[options] = "Display Options";
    optionsEnumArray[options++] = ScreenOptions;

    if (currentResolution == ScreenResolution::UltraLow) {
        optionsArray[options] = "Bluetooth";
    } else {
        optionsArray[options] = "Bluetooth Toggle";
    }
    optionsEnumArray[options++] = Bluetooth;
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
    optionsArray[options] = "WiFi Toggle";
    optionsEnumArray[options++] = WiFiToggle;
#endif

    if (currentResolution == ScreenResolution::UltraLow) {
        optionsArray[options] = "Power";
    } else {
        optionsArray[options] = "Reboot/Shutdown";
    }
    optionsEnumArray[options++] = PowerMenu;

    if (test_enabled) {
        optionsArray[options] = "Test Menu";
        optionsEnumArray[options++] = Test;
    }

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "System Action";
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = "System";
    }
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = options;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Notifications) {
            menuHandler::menuQueue = menuHandler::buzzermodemenupicker;
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
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
        } else if (selected == WiFiToggle) {
            menuQueue = wifi_toggle_menu;
            screen->runNow();
#endif
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
    enum optionsNumbers { Back, Preset, Freetext, GoToChat, Remove, TraceRoute, enumEnd };

    static const char *optionsArray[enumEnd] = {"Back"};
    static int optionsEnumArray[enumEnd] = {Back};
    int options = 1;

    // Only show "View Conversation" if a message exists with this node
    uint32_t peer = graphics::UIRenderer::currentFavoriteNodeNum;
    bool hasConversation = false;
    for (const auto &m : messageStore.getMessages()) {
        if ((m.sender == peer || m.dest == peer)) {
            hasConversation = true;
            break;
        }
    }
    if (hasConversation) {
        optionsArray[options] = "Go To Chat";
        optionsEnumArray[options++] = GoToChat;
    }
    if (currentResolution == ScreenResolution::UltraLow) {
        optionsArray[options] = "New Preset";
    } else {
        optionsArray[options] = "New Preset Msg";
    }
    optionsEnumArray[options++] = Preset;

    if (kb_found) {
        optionsArray[options] = "New Freetext Msg";
        optionsEnumArray[options++] = Freetext;
    }

    if (currentResolution != ScreenResolution::UltraLow) {
        optionsArray[options] = "Trace Route";
        optionsEnumArray[options++] = TraceRoute;
    }
    optionsArray[options] = "Remove Favorite";
    optionsEnumArray[options++] = Remove;

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Favorites Action";
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = "Favorites";
    }
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Preset) {
            cannedMessageModule->LaunchWithDestination(graphics::UIRenderer::currentFavoriteNodeNum);
        } else if (selected == Freetext) {
            cannedMessageModule->LaunchFreetextWithDestination(graphics::UIRenderer::currentFavoriteNodeNum);
        }
        // Handle new Go To Thread action
        else if (selected == GoToChat) {
            // Switch thread to direct conversation with this node
            graphics::MessageRenderer::setThreadMode(graphics::MessageRenderer::ThreadMode::DIRECT, -1,
                                                     graphics::UIRenderer::currentFavoriteNodeNum);

            // Manually create and send a UIFrameEvent to trigger the jump
            UIFrameEvent evt;
            evt.action = UIFrameEvent::Action::SWITCH_TO_TEXTMESSAGE;
            screen->handleUIFrameEvent(&evt);
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
    enum class PositionAction {
        GpsToggle,
        GpsFormat,
        CompassMenu,
        CompassCalibrate,
        GPSSmartPosition,
        GPSUpdateInterval,
        GPSPositionBroadcast
    };

    static const PositionMenuOption baseOptions[] = {
        {"Back", OptionsAction::Back},
        {"On/Off Toggle", OptionsAction::Select, static_cast<int>(PositionAction::GpsToggle)},
        {"Format", OptionsAction::Select, static_cast<int>(PositionAction::GpsFormat)},
        {"Smart Position", OptionsAction::Select, static_cast<int>(PositionAction::GPSSmartPosition)},
        {"Update Interval", OptionsAction::Select, static_cast<int>(PositionAction::GPSUpdateInterval)},
        {"Broadcast Interval", OptionsAction::Select, static_cast<int>(PositionAction::GPSPositionBroadcast)},
        {"Compass", OptionsAction::Select, static_cast<int>(PositionAction::CompassMenu)},
    };

    static const PositionMenuOption calibrateOptions[] = {
        {"Back", OptionsAction::Back},
        {"On/Off Toggle", OptionsAction::Select, static_cast<int>(PositionAction::GpsToggle)},
        {"Format", OptionsAction::Select, static_cast<int>(PositionAction::GpsFormat)},
        {"Smart Position", OptionsAction::Select, static_cast<int>(PositionAction::GPSSmartPosition)},
        {"Update Interval", OptionsAction::Select, static_cast<int>(PositionAction::GPSUpdateInterval)},
        {"Broadcast Interval", OptionsAction::Select, static_cast<int>(PositionAction::GPSPositionBroadcast)},
        {"Compass", OptionsAction::Select, static_cast<int>(PositionAction::CompassMenu)},
        {"Compass Calibrate", OptionsAction::Select, static_cast<int>(PositionAction::CompassCalibrate)},
    };

    constexpr size_t baseCount = sizeof(baseOptions) / sizeof(baseOptions[0]);
    constexpr size_t calibrateCount = sizeof(calibrateOptions) / sizeof(calibrateOptions[0]);
    static std::array<const char *, baseCount> baseLabels{};
    static std::array<const char *, calibrateCount> calibrateLabels{};

    auto onSelection = [](const PositionMenuOption &option, int) -> void {
        if (option.action == OptionsAction::Back) {
            return;
        }

        if (!option.hasValue) {
            return;
        }

        auto action = static_cast<PositionAction>(option.value);
        switch (action) {
        case PositionAction::GpsToggle:
            menuQueue = gps_toggle_menu;
            screen->runNow();
            break;
        case PositionAction::GpsFormat:
            menuQueue = gps_format_menu;
            screen->runNow();
            break;
        case PositionAction::CompassMenu:
            menuQueue = compass_point_north_menu;
            screen->runNow();
            break;
        case PositionAction::CompassCalibrate:
            if (accelerometerThread) {
                accelerometerThread->calibrate(30);
            }
            break;
        case PositionAction::GPSSmartPosition:
            menuQueue = gps_smart_position_menu;
            screen->runNow();
            break;
        case PositionAction::GPSUpdateInterval:
            menuQueue = gps_update_interval_menu;
            screen->runNow();
            break;
        case PositionAction::GPSPositionBroadcast:
            menuQueue = gps_position_broadcast_menu;
            screen->runNow();
            break;
        }
    };

    BannerOverlayOptions bannerOptions;
    if (accelerometerThread) {
        bannerOptions = createStaticBannerOptions("GPS Action", calibrateOptions, calibrateLabels, onSelection);
    } else {
        bannerOptions = createStaticBannerOptions("GPS Action", baseOptions, baseLabels, onSelection);
    }

    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::nodeListMenu()
{
    enum optionsNumbers { Back, NodePicker, TraceRoute, Verify, Reset, NodeNameLength, enumEnd };
    static const char *optionsArray[enumEnd] = {"Back"};
    static int optionsEnumArray[enumEnd] = {Back};
    int options = 1;

    optionsArray[options] = "Node Actions / Settings";
    optionsEnumArray[options++] = NodePicker;

    if (currentResolution != ScreenResolution::UltraLow) {
        optionsArray[options] = "Show Long/Short Name";
        optionsEnumArray[options++] = NodeNameLength;
    }
    optionsArray[options] = "Reset NodeDB";
    optionsEnumArray[options++] = Reset;

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Node Action";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = options;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == NodePicker) {
            menuQueue = NodePicker_menu;
            screen->runNow();
        } else if (selected == Reset) {
            menuQueue = reset_node_db_menu;
            screen->runNow();
        } else if (selected == NodeNameLength) {
            menuHandler::menuQueue = menuHandler::node_name_length_menu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::NodePicker()
{
    const char *NODE_PICKER_TITLE;
    if (currentResolution == ScreenResolution::UltraLow) {
        NODE_PICKER_TITLE = "Pick Node";
    } else {
        NODE_PICKER_TITLE = "Pick A Node";
    }
    screen->showNodePicker(NODE_PICKER_TITLE, 30000, [](uint32_t nodenum) -> void {
        LOG_INFO("Nodenum: %u", nodenum);
        // Store the selection so the Manage Node menu knows which node to operate on
        menuHandler::pickedNodeNum = nodenum;
        // Keep UI favorite context in sync (used elsewhere for some node-based actions)
        graphics::UIRenderer::currentFavoriteNodeNum = nodenum;
        menuQueue = Manage_Node_menu;
        screen->runNow();
    });
}

void menuHandler::ManageNodeMenu()
{
    // If we don't have a node selected yet, go fast exit
    auto node = nodeDB->getMeshNode(menuHandler::pickedNodeNum);
    if (!node) {
        return;
    }
    enum optionsNumbers { Back, Favorite, Mute, TraceRoute, KeyVerification, Ignore, enumEnd };
    static const char *optionsArray[enumEnd] = {"Back"};
    static int optionsEnumArray[enumEnd] = {Back};
    int options = 1;

    if (node->is_favorite) {
        optionsArray[options] = "Unfavorite";
    } else {
        optionsArray[options] = "Favorite";
    }
    optionsEnumArray[options++] = Favorite;

    bool isMuted = (node->bitfield & NODEINFO_BITFIELD_IS_MUTED_MASK) != 0;
    if (isMuted) {
        optionsArray[options] = "Unmute Notifications";
    } else {
        optionsArray[options] = "Mute Notifications";
    }
    optionsEnumArray[options++] = Mute;

    optionsArray[options] = "Trace Route";
    optionsEnumArray[options++] = TraceRoute;

    optionsArray[options] = "Key Verification";
    optionsEnumArray[options++] = KeyVerification;

    if (node->is_ignored) {
        optionsArray[options] = "Unignore Node";
    } else {
        optionsArray[options] = "Ignore Node";
    }
    optionsEnumArray[options++] = Ignore;

    BannerOverlayOptions bannerOptions;

    std::string title = "";
    if (node->has_user && node->user.long_name && node->user.long_name[0]) {
        title += sanitizeString(node->user.long_name).substr(0, 15);
    } else {
        char buf[20];
        snprintf(buf, sizeof(buf), "%08X", (unsigned int)node->num);
        title += buf;
    }
    bannerOptions.message = title.c_str();
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = options;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Back) {
            menuQueue = node_base_menu;
            screen->runNow();
            return;
        }

        if (selected == Favorite) {
            auto n = nodeDB->getMeshNode(menuHandler::pickedNodeNum);
            if (!n) {
                return;
            }
            if (n->is_favorite) {
                LOG_INFO("Removing node %08X from favorites", menuHandler::pickedNodeNum);
                nodeDB->set_favorite(false, menuHandler::pickedNodeNum);
            } else {
                LOG_INFO("Adding node %08X to favorites", menuHandler::pickedNodeNum);
                nodeDB->set_favorite(true, menuHandler::pickedNodeNum);
            }
            screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
            return;
        }

        if (selected == Mute) {
            auto n = nodeDB->getMeshNode(menuHandler::pickedNodeNum);
            if (!n) {
                return;
            }

            if (n->bitfield & NODEINFO_BITFIELD_IS_MUTED_MASK) {
                n->bitfield &= ~NODEINFO_BITFIELD_IS_MUTED_MASK;
                LOG_INFO("Unmuted node %08X", menuHandler::pickedNodeNum);
            } else {
                n->bitfield |= NODEINFO_BITFIELD_IS_MUTED_MASK;
                LOG_INFO("Muted node %08X", menuHandler::pickedNodeNum);
            }
            nodeDB->notifyObservers(true);
            nodeDB->saveToDisk();
            screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
            return;
        }

        if (selected == TraceRoute) {
            LOG_INFO("Starting traceroute to %08X", menuHandler::pickedNodeNum);
            if (traceRouteModule) {
                traceRouteModule->startTraceRoute(menuHandler::pickedNodeNum);
            }
            return;
        }

        if (selected == KeyVerification) {
            LOG_INFO("Initiating key verification with %08X", menuHandler::pickedNodeNum);
            if (keyVerificationModule) {
                keyVerificationModule->sendInitialRequest(menuHandler::pickedNodeNum);
            }
            return;
        }

        if (selected == Ignore) {
            auto n = nodeDB->getMeshNode(menuHandler::pickedNodeNum);
            if (!n) {
                return;
            }

            if (n->is_ignored) {
                n->is_ignored = false;
                LOG_INFO("Unignoring node %08X", menuHandler::pickedNodeNum);
            } else {
                n->is_ignored = true;
                LOG_INFO("Ignoring node %08X", menuHandler::pickedNodeNum);
            }
            nodeDB->notifyObservers(true);
            nodeDB->saveToDisk();
            screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
            return;
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::nodeNameLengthMenu()
{
    static const NodeNameOption nodeNameOptions[] = {
        {"Back", OptionsAction::Back},
        {"Long", OptionsAction::Select, true},
        {"Short", OptionsAction::Select, false},
    };

    constexpr size_t nodeNameCount = sizeof(nodeNameOptions) / sizeof(nodeNameOptions[0]);
    static std::array<const char *, nodeNameCount> nodeNameLabels{};

    auto bannerOptions = createStaticBannerOptions("Node Name Length", nodeNameOptions, nodeNameLabels,
                                                   [](const NodeNameOption &option, int) -> void {
                                                       if (option.action == OptionsAction::Back) {
                                                           menuQueue = node_base_menu;
                                                           screen->runNow();
                                                           return;
                                                       }

                                                       if (!option.hasValue) {
                                                           return;
                                                       }

                                                       if (config.display.use_long_node_name == option.value) {
                                                           return;
                                                       }

                                                       config.display.use_long_node_name = option.value;
                                                       saveUIConfig();
                                                       LOG_INFO("Setting names to %s", option.value ? "long" : "short");
                                                   });

    int initialSelection = config.display.use_long_node_name ? 1 : 2;
    bannerOptions.InitialSelected = initialSelection;

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
        } else if (selected == 0) {
            menuQueue = node_base_menu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::compassNorthMenu()
{
    static const CompassOption compassOptions[] = {
        {"Back", OptionsAction::Back},
        {"Dynamic", OptionsAction::Select, meshtastic_CompassMode_DYNAMIC},
        {"Fixed Ring", OptionsAction::Select, meshtastic_CompassMode_FIXED_RING},
        {"Freeze Heading", OptionsAction::Select, meshtastic_CompassMode_FREEZE_HEADING},
    };

    constexpr size_t compassCount = sizeof(compassOptions) / sizeof(compassOptions[0]);
    static std::array<const char *, compassCount> compassLabels{};

    auto bannerOptions = createStaticBannerOptions("North Directions?", compassOptions, compassLabels,
                                                   [](const CompassOption &option, int) -> void {
                                                       if (option.action == OptionsAction::Back) {
                                                           menuQueue = position_base_menu;
                                                           screen->runNow();
                                                           return;
                                                       }

                                                       if (!option.hasValue) {
                                                           return;
                                                       }

                                                       if (uiconfig.compass_mode == option.value) {
                                                           return;
                                                       }

                                                       uiconfig.compass_mode = option.value;
                                                       saveUIConfig();
                                                       screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
                                                   });

    int initialSelection = 0;
    for (size_t i = 0; i < compassCount; ++i) {
        if (compassOptions[i].hasValue && uiconfig.compass_mode == compassOptions[i].value) {
            initialSelection = static_cast<int>(i);
            break;
        }
    }
    bannerOptions.InitialSelected = initialSelection;

    screen->showOverlayBanner(bannerOptions);
}

#if !MESHTASTIC_EXCLUDE_GPS
void menuHandler::GPSToggleMenu()
{
    static const GPSToggleOption gpsToggleOptions[] = {
        {"Back", OptionsAction::Back},
        {"Enabled", OptionsAction::Select, meshtastic_Config_PositionConfig_GpsMode_ENABLED},
        {"Disabled", OptionsAction::Select, meshtastic_Config_PositionConfig_GpsMode_DISABLED},
    };

    constexpr size_t toggleCount = sizeof(gpsToggleOptions) / sizeof(gpsToggleOptions[0]);
    static std::array<const char *, toggleCount> toggleLabels{};

    auto bannerOptions =
        createStaticBannerOptions("Toggle GPS", gpsToggleOptions, toggleLabels, [](const GPSToggleOption &option, int) -> void {
            if (option.action == OptionsAction::Back) {
                menuQueue = position_base_menu;
                screen->runNow();
                return;
            }

            if (!option.hasValue) {
                return;
            }

            if (config.position.gps_mode == option.value) {
                return;
            }

            config.position.gps_mode = option.value;
            if (option.value == meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
                playGPSEnableBeep();
                gps->enable();
            } else {
                playGPSDisableBeep();
                gps->disable();
            }
            service->reloadConfig(SEGMENT_CONFIG);
        });

    int initialSelection = 0;
    for (size_t i = 0; i < toggleCount; ++i) {
        if (gpsToggleOptions[i].hasValue && config.position.gps_mode == gpsToggleOptions[i].value) {
            initialSelection = static_cast<int>(i);
            break;
        }
    }
    bannerOptions.InitialSelected = initialSelection;

    screen->showOverlayBanner(bannerOptions);
}
void menuHandler::GPSFormatMenu()
{
    static const GPSFormatOption formatOptionsHigh[] = {
        {"Back", OptionsAction::Back},
        {"Decimal Degrees", OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_DEC},
        {"Degrees Minutes Seconds", OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_DMS},
        {"Universal Transverse Mercator", OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_UTM},
        {"Military Grid Reference System", OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_MGRS},
        {"Open Location Code", OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_OLC},
        {"Ordnance Survey Grid Ref", OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_OSGR},
        {"Maidenhead Locator", OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_MLS},
    };

    static const GPSFormatOption formatOptionsLow[] = {
        {"Back", OptionsAction::Back},
        {"DEC", OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_DEC},
        {"DMS", OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_DMS},
        {"UTM", OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_UTM},
        {"MGRS", OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_MGRS},
        {"OLC", OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_OLC},
        {"OSGR", OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_OSGR},
        {"MLS", OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_MLS},
    };

    constexpr size_t formatCount = sizeof(formatOptionsHigh) / sizeof(formatOptionsHigh[0]);
    static std::array<const char *, formatCount> formatLabelsHigh{};
    static std::array<const char *, formatCount> formatLabelsLow{};

    auto onSelection = [](const GPSFormatOption &option, int) -> void {
        if (option.action == OptionsAction::Back) {
            menuQueue = position_base_menu;
            screen->runNow();
            return;
        }

        if (!option.hasValue) {
            return;
        }

        if (uiconfig.gps_format == option.value) {
            return;
        }

        uiconfig.gps_format = option.value;
        saveUIConfig();
        service->reloadConfig(SEGMENT_CONFIG);
    };

    BannerOverlayOptions bannerOptions;
    int initialSelection = 0;

    if (currentResolution == ScreenResolution::High) {
        bannerOptions = createStaticBannerOptions("GPS Format", formatOptionsHigh, formatLabelsHigh, onSelection);
        for (size_t i = 0; i < formatCount; ++i) {
            if (formatOptionsHigh[i].hasValue && uiconfig.gps_format == formatOptionsHigh[i].value) {
                initialSelection = static_cast<int>(i);
                break;
            }
        }
    } else {
        bannerOptions = createStaticBannerOptions("GPS Format", formatOptionsLow, formatLabelsLow, onSelection);
        for (size_t i = 0; i < formatCount; ++i) {
            if (formatOptionsLow[i].hasValue && uiconfig.gps_format == formatOptionsLow[i].value) {
                initialSelection = static_cast<int>(i);
                break;
            }
        }
    }

    bannerOptions.InitialSelected = initialSelection;
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::GPSSmartPositionMenu()
{
    static const char *optionsArray[] = {"Back", "Enabled", "Disabled"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Toggle Smart Position";
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = "Smrt Postn";
    }
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 0) {
            menuQueue = position_base_menu;
            screen->runNow();
        } else if (selected == 1) {
            config.position.position_broadcast_smart_enabled = true;
            saveUIConfig();
            service->reloadConfig(SEGMENT_CONFIG);
            rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        } else if (selected == 2) {
            config.position.position_broadcast_smart_enabled = false;
            saveUIConfig();
            service->reloadConfig(SEGMENT_CONFIG);
            rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        }
    };
    bannerOptions.InitialSelected = config.position.position_broadcast_smart_enabled ? 1 : 2;
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::GPSUpdateIntervalMenu()
{
    static const char *optionsArray[] = {"Back",      "8 seconds", "20 seconds", "40 seconds",  "1 minute",   "80 seconds",
                                         "2 minutes", "5 minutes", "10 minutes", "15 minutes",  "30 minutes", "1 hour",
                                         "6 hours",   "12 hours",  "24 hours",   "At Boot Only"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Update Interval";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 16;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 0) {
            menuQueue = position_base_menu;
            screen->runNow();
        } else if (selected == 1) {
            config.position.gps_update_interval = 8;
        } else if (selected == 2) {
            config.position.gps_update_interval = 20;
        } else if (selected == 3) {
            config.position.gps_update_interval = 40;
        } else if (selected == 4) {
            config.position.gps_update_interval = 60;
        } else if (selected == 5) {
            config.position.gps_update_interval = 80;
        } else if (selected == 6) {
            config.position.gps_update_interval = 120;
        } else if (selected == 7) {
            config.position.gps_update_interval = 300;
        } else if (selected == 8) {
            config.position.gps_update_interval = 600;
        } else if (selected == 9) {
            config.position.gps_update_interval = 900;
        } else if (selected == 10) {
            config.position.gps_update_interval = 1800;
        } else if (selected == 11) {
            config.position.gps_update_interval = 3600;
        } else if (selected == 12) {
            config.position.gps_update_interval = 21600;
        } else if (selected == 13) {
            config.position.gps_update_interval = 43200;
        } else if (selected == 14) {
            config.position.gps_update_interval = 86400;
        } else if (selected == 15) {
            config.position.gps_update_interval = 2147483647; // At Boot Only
        }

        if (selected != 0) {
            saveUIConfig();
            service->reloadConfig(SEGMENT_CONFIG);
            rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        }
    };

    if (config.position.gps_update_interval == 8) {
        bannerOptions.InitialSelected = 1;
    } else if (config.position.gps_update_interval == 20) {
        bannerOptions.InitialSelected = 2;
    } else if (config.position.gps_update_interval == 40) {
        bannerOptions.InitialSelected = 3;
    } else if (config.position.gps_update_interval == 60) {
        bannerOptions.InitialSelected = 4;
    } else if (config.position.gps_update_interval == 80) {
        bannerOptions.InitialSelected = 5;
    } else if (config.position.gps_update_interval == 120) {
        bannerOptions.InitialSelected = 6;
    } else if (config.position.gps_update_interval == 300) {
        bannerOptions.InitialSelected = 7;
    } else if (config.position.gps_update_interval == 600) {
        bannerOptions.InitialSelected = 8;
    } else if (config.position.gps_update_interval == 900) {
        bannerOptions.InitialSelected = 9;
    } else if (config.position.gps_update_interval == 1800) {
        bannerOptions.InitialSelected = 10;
    } else if (config.position.gps_update_interval == 3600) {
        bannerOptions.InitialSelected = 11;
    } else if (config.position.gps_update_interval == 21600) {
        bannerOptions.InitialSelected = 12;
    } else if (config.position.gps_update_interval == 43200) {
        bannerOptions.InitialSelected = 13;
    } else if (config.position.gps_update_interval == 86400) {
        bannerOptions.InitialSelected = 14;
    } else if (config.position.gps_update_interval == 2147483647) { // At Boot Only
        bannerOptions.InitialSelected = 15;
    } else {
        bannerOptions.InitialSelected = 0;
    }
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::GPSPositionBroadcastMenu()
{
    static const char *optionsArray[] = {"Back",     "1 minute", "90 seconds", "5 minutes", "15 minutes", "1 hour",
                                         "2 hours",  "3 hours",  "4 hours",    "5 hours",   "6 hours",    "12 hours",
                                         "18 hours", "24 hours", "36 hours",   "48 hours",  "72 hours"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Broadcast Interval";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 17;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 0) {
            menuQueue = position_base_menu;
            screen->runNow();
        } else if (selected == 1) {
            config.position.position_broadcast_secs = 60;
        } else if (selected == 2) {
            config.position.position_broadcast_secs = 90;
        } else if (selected == 3) {
            config.position.position_broadcast_secs = 300;
        } else if (selected == 4) {
            config.position.position_broadcast_secs = 900;
        } else if (selected == 5) {
            config.position.position_broadcast_secs = 3600;
        } else if (selected == 6) {
            config.position.position_broadcast_secs = 7200;
        } else if (selected == 7) {
            config.position.position_broadcast_secs = 10800;
        } else if (selected == 8) {
            config.position.position_broadcast_secs = 14400;
        } else if (selected == 9) {
            config.position.position_broadcast_secs = 18000;
        } else if (selected == 10) {
            config.position.position_broadcast_secs = 21600;
        } else if (selected == 11) {
            config.position.position_broadcast_secs = 43200;
        } else if (selected == 12) {
            config.position.position_broadcast_secs = 64800;
        } else if (selected == 13) {
            config.position.position_broadcast_secs = 86400;
        } else if (selected == 14) {
            config.position.position_broadcast_secs = 129600;
        } else if (selected == 15) {
            config.position.position_broadcast_secs = 172800;
        } else if (selected == 16) {
            config.position.position_broadcast_secs = 259200;
        }

        if (selected != 0) {
            saveUIConfig();
            service->reloadConfig(SEGMENT_CONFIG);
            rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        }
    };

    if (config.position.position_broadcast_secs == 60) {
        bannerOptions.InitialSelected = 1;
    } else if (config.position.position_broadcast_secs == 90) {
        bannerOptions.InitialSelected = 2;
    } else if (config.position.position_broadcast_secs == 300) {
        bannerOptions.InitialSelected = 3;
    } else if (config.position.position_broadcast_secs == 900) {
        bannerOptions.InitialSelected = 4;
    } else if (config.position.position_broadcast_secs == 3600) {
        bannerOptions.InitialSelected = 5;
    } else if (config.position.position_broadcast_secs == 7200) {
        bannerOptions.InitialSelected = 6;
    } else if (config.position.position_broadcast_secs == 10800) {
        bannerOptions.InitialSelected = 7;
    } else if (config.position.position_broadcast_secs == 14400) {
        bannerOptions.InitialSelected = 8;
    } else if (config.position.position_broadcast_secs == 18000) {
        bannerOptions.InitialSelected = 9;
    } else if (config.position.position_broadcast_secs == 21600) {
        bannerOptions.InitialSelected = 10;
    } else if (config.position.position_broadcast_secs == 43200) {
        bannerOptions.InitialSelected = 11;
    } else if (config.position.position_broadcast_secs == 64800) {
        bannerOptions.InitialSelected = 12;
    } else if (config.position.position_broadcast_secs == 86400) {
        bannerOptions.InitialSelected = 13;
    } else if (config.position.position_broadcast_secs == 129600) {
        bannerOptions.InitialSelected = 14;
    } else if (config.position.position_broadcast_secs == 172800) {
        bannerOptions.InitialSelected = 15;
    } else if (config.position.position_broadcast_secs == 259200) {
        bannerOptions.InitialSelected = 16;
    } else {
        bannerOptions.InitialSelected = 0;
    }
    screen->showOverlayBanner(bannerOptions);
}

#endif

void menuHandler::BluetoothToggleMenu()
{
    static const char *optionsArray[] = {"Back", "Enabled", "Disabled"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Toggle Bluetooth";
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = "Bluetooth";
    }
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
    static const char *optionsArray[] = {"All Enabled", "All Disabled", "Notifications", "System Only", "DMs Only"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Notification Sounds";
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
    static const ScreenColorOption colorOptions[] = {
        {"Back", OptionsAction::Back},
        {"Default", OptionsAction::Select, ScreenColor(0, 0, 0, true)},
        {"Meshtastic Green", OptionsAction::Select, ScreenColor(0x67, 0xEA, 0x94)},
        {"Yellow", OptionsAction::Select, ScreenColor(255, 255, 128)},
        {"Red", OptionsAction::Select, ScreenColor(255, 64, 64)},
        {"Orange", OptionsAction::Select, ScreenColor(255, 160, 20)},
        {"Purple", OptionsAction::Select, ScreenColor(204, 153, 255)},
        {"Blue", OptionsAction::Select, ScreenColor(0, 0, 255)},
        {"Teal", OptionsAction::Select, ScreenColor(16, 102, 102)},
        {"Cyan", OptionsAction::Select, ScreenColor(0, 255, 255)},
        {"Ice", OptionsAction::Select, ScreenColor(173, 216, 230)},
        {"Pink", OptionsAction::Select, ScreenColor(255, 105, 180)},
        {"White", OptionsAction::Select, ScreenColor(255, 255, 255)},
        {"Gray", OptionsAction::Select, ScreenColor(128, 128, 128)},
    };

    constexpr size_t colorCount = sizeof(colorOptions) / sizeof(colorOptions[0]);
    static std::array<const char *, colorCount> colorLabels{};

    auto bannerOptions = createStaticBannerOptions(
        "Select Screen Color", colorOptions, colorLabels, [display](const ScreenColorOption &option, int) -> void {
            if (option.action == OptionsAction::Back) {
                menuQueue = system_base_menu;
                screen->runNow();
                return;
            }

            if (!option.hasValue) {
                return;
            }

#if defined(HELTEC_MESH_NODE_T114) || defined(HELTEC_VISION_MASTER_T190) || defined(T_DECK) || defined(T_LORA_PAGER) ||          \
    HAS_TFT || defined(HACKADAY_COMMUNICATOR)
            const ScreenColor &color = option.value;
            if (color.useVariant) {
                LOG_INFO("Setting color to system default or defined variant");
            } else {
                LOG_INFO("Setting color to %s", option.label);
            }

            uint8_t r = color.r;
            uint8_t g = color.g;
            uint8_t b = color.b;

            display->setColor(BLACK);
            display->fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
            display->setColor(WHITE);

            if (color.useVariant || (r == 0 && g == 0 && b == 0)) {
#ifdef TFT_MESH_OVERRIDE
                TFT_MESH = TFT_MESH_OVERRIDE;
#else
                TFT_MESH = COLOR565(255, 255, 128);
#endif
            } else {
                TFT_MESH = COLOR565(r, g, b);
            }

#if defined(HELTEC_MESH_NODE_T114) || defined(HELTEC_VISION_MASTER_T190)
            static_cast<ST7789Spi *>(screen->getDisplayDevice())->setRGB(TFT_MESH);
#endif

            screen->setFrames(graphics::Screen::FOCUS_SYSTEM);
            if (color.useVariant || (r == 0 && g == 0 && b == 0)) {
                uiconfig.screen_rgb_color = 0;
            } else {
                uiconfig.screen_rgb_color =
                    (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
            }
            LOG_INFO("Storing Value of %d to uiconfig.screen_rgb_color", uiconfig.screen_rgb_color);
            saveUIConfig();
#endif
        });

    int initialSelection = 0;
    if (uiconfig.screen_rgb_color == 0) {
        initialSelection = 1;
    } else {
        uint32_t currentColor = uiconfig.screen_rgb_color;
        for (size_t i = 0; i < colorCount; ++i) {
            if (!colorOptions[i].hasValue) {
                continue;
            }
            const ScreenColor &color = colorOptions[i].value;
            if (color.useVariant) {
                continue;
            }
            uint32_t encoded =
                (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) | static_cast<uint32_t>(color.b);
            if (encoded == currentColor) {
                initialSelection = static_cast<int>(i);
                break;
            }
        }
    }
    bannerOptions.InitialSelected = initialSelection;

    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::rebootMenu()
{
    static const char *optionsArray[] = {"Back", "Confirm"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Reboot Device?";
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = "Reboot";
    }
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            IF_SCREEN(screen->showSimpleBanner("Rebooting...", 0));
            nodeDB->saveToDisk();
            messageStore.saveToFlash();
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
    bannerOptions.message = "Shutdown Device?";
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = "Shutdown";
    }
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
    enum optionsNumbers { Back, Wifi_disable, Wifi_enable };

    static const char *optionsArray[] = {"Back", "WiFi Disabled", "WiFi Enabled"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "WiFi Actions";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    if (config.network.wifi_enabled == true)
        bannerOptions.InitialSelected = 2;
    else
        bannerOptions.InitialSelected = 1;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Wifi_disable) {
            config.network.wifi_enabled = false;
            config.bluetooth.enabled = true;
            service->reloadConfig(SEGMENT_CONFIG);
            rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        } else if (selected == Wifi_enable) {
            config.network.wifi_enabled = true;
            config.bluetooth.enabled = false;
            service->reloadConfig(SEGMENT_CONFIG);
            rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
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

    enum optionsNumbers { Back, Brightness, ScreenColor, FrameToggles, DisplayUnits };
    static const char *optionsArray[5] = {"Back"};
    static int optionsEnumArray[5] = {Back};
    int options = 1;

    // Only show brightness for B&W displays
    if (hasSupportBrightness) {
        optionsArray[options] = "Brightness";
        optionsEnumArray[options++] = Brightness;
    }

    // Only show screen color for TFT displays
#if defined(HELTEC_MESH_NODE_T114) || defined(HELTEC_VISION_MASTER_T190) || defined(T_DECK) || defined(T_LORA_PAGER) ||          \
    HAS_TFT || defined(HACKADAY_COMMUNICATOR)
    optionsArray[options] = "Screen Color";
    optionsEnumArray[options++] = ScreenColor;
#endif

    optionsArray[options] = "Frame Visibility";
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
    bannerOptions.message = "Reboot / Shutdown";
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = "Power";
    }
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
        nodelist_nodes,
        nodelist_location,
        nodelist_lastheard,
        nodelist_hopsignal,
        nodelist_distance,
        nodelist_bearings,
        gps,
        lora,
        clock,
        show_favorites,
        show_env_telemetry,
        show_aq_telemetry,
        show_power,
        enumEnd
    };
    static const char *optionsArray[enumEnd] = {"Finish"};
    static int optionsEnumArray[enumEnd] = {Finish};
    int options = 1;

    // Track last selected index (not enum value!)
    static int lastSelectedIndex = 0;

#ifndef USE_EINK
    optionsArray[options] = screen->isFrameHidden("nodelist_nodes") ? "Show Node Lists" : "Hide Node Lists";
    optionsEnumArray[options++] = nodelist_nodes;
#else
    optionsArray[options] = screen->isFrameHidden("nodelist_lastheard") ? "Show NL - Last Heard" : "Hide NL - Last Heard";
    optionsEnumArray[options++] = nodelist_lastheard;
    optionsArray[options] = screen->isFrameHidden("nodelist_hopsignal") ? "Show NL - Hops/Signal" : "Hide NL - Hops/Signal";
    optionsEnumArray[options++] = nodelist_hopsignal;
#endif

#if HAS_GPS
#ifndef USE_EINK
    optionsArray[options] = screen->isFrameHidden("nodelist_location") ? "Show Position Lists" : "Hide Position Lists";
    optionsEnumArray[options++] = nodelist_location;
#else
    optionsArray[options] = screen->isFrameHidden("nodelist_distance") ? "Show NL - Distance" : "Hide NL - Distance";
    optionsEnumArray[options++] = nodelist_distance;
    optionsArray[options] = screen->isFrameHidden("nodelist_bearings") ? "Show NL - Bearings" : "Hide NL - Bearings";
    optionsEnumArray[options++] = nodelist_bearings;
#endif

    optionsArray[options] = screen->isFrameHidden("gps") ? "Show Position" : "Hide Position";
    optionsEnumArray[options++] = gps;
#endif

    optionsArray[options] = screen->isFrameHidden("lora") ? "Show LoRa" : "Hide LoRa";
    optionsEnumArray[options++] = lora;

    optionsArray[options] = screen->isFrameHidden("clock") ? "Show Clock" : "Hide Clock";
    optionsEnumArray[options++] = clock;

    optionsArray[options] = screen->isFrameHidden("show_favorites") ? "Show Favorites" : "Hide Favorites";
    optionsEnumArray[options++] = show_favorites;

    optionsArray[options] = moduleConfig.telemetry.environment_screen_enabled ? "Hide Env. Telemetry" : "Show Env. Telemetry";
    optionsEnumArray[options++] = show_env_telemetry;

    optionsArray[options] = moduleConfig.telemetry.air_quality_screen_enabled ? "Hide AQ Telemetry" : "Show AQ Telemetry";
    optionsEnumArray[options++] = show_aq_telemetry;

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
        } else if (selected == nodelist_nodes) {
            screen->toggleFrameVisibility("nodelist_nodes");
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        } else if (selected == nodelist_location) {
            screen->toggleFrameVisibility("nodelist_location");
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
        } else if (selected == show_env_telemetry) {
            moduleConfig.telemetry.environment_screen_enabled = !moduleConfig.telemetry.environment_screen_enabled;
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        } else if (selected == show_aq_telemetry) {
            moduleConfig.telemetry.air_quality_screen_enabled = !moduleConfig.telemetry.air_quality_screen_enabled;
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
    case frequency_slot:
        FrequencySlotPicker();
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
    case node_base_menu:
        nodeListMenu();
        break;
#if !MESHTASTIC_EXCLUDE_GPS
    case gps_toggle_menu:
        GPSToggleMenu();
        break;
    case gps_format_menu:
        GPSFormatMenu();
        break;
    case gps_smart_position_menu:
        GPSSmartPositionMenu();
        break;
    case gps_update_interval_menu:
        GPSUpdateIntervalMenu();
        break;
    case gps_position_broadcast_menu:
        GPSPositionBroadcastMenu();
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
    case NodePicker_menu:
        NodePicker();
        break;
    case Manage_Node_menu:
        ManageNodeMenu();
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
    case message_response_menu:
        messageResponseMenu();
        break;
    case reply_menu:
        replyMenu();
        break;
    case delete_messages_menu:
        deleteMessagesMenu();
        break;
    case message_viewmode_menu:
        messageViewModeMenu();
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