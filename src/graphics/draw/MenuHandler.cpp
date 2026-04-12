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
#include "graphics/UiStrings.h"
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

menuHandler::screenMenus menuHandler::menuQueue = MenuNone;
uint32_t menuHandler::pickedNodeNum = 0;
bool test_enabled = false;
uint8_t test_count = 0;

void menuHandler::loraMenu()
{
    static const char *optionsArray[] = {
        UI_STR("Back", "返回"),
        UI_STR("Device Role", "设备角色"),
        UI_STR("Radio Preset", "电台预设"),
        UI_STR("Frequency Slot", "频点槽位"),
        UI_STR("LoRa Region", "LoRa区域"),
    };
    enum optionsNumbers { Back = 0, DeviceRolePicker = 1, RadioPresetPicker = 2, FrequencySlot = 3, LoraPicker = 4 };
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("LoRa Actions", "LoRa操作");
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 5;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Back) {
            // No action
        } else if (selected == DeviceRolePicker) {
            menuHandler::menuQueue = menuHandler::DeviceRolePicker;
        } else if (selected == RadioPresetPicker) {
            menuHandler::menuQueue = menuHandler::RadioPresetPicker;
        } else if (selected == FrequencySlot) {
            menuHandler::menuQueue = menuHandler::FrequencySlot;
        } else if (selected == LoraPicker) {
            menuHandler::menuQueue = menuHandler::LoraPicker;
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::OnboardMessage()
{
    static const char *optionsArray[] = {UI_STR("OK", "确定"), UI_STR("Got it!", "知道了")};
    enum optionsNumbers { OK, got };
    BannerOverlayOptions bannerOptions;
#if HAS_TFT
    bannerOptions.message = UI_STR("Welcome to Meshtastic!\nSwipe to navigate and\nlong press to select\nor open a menu.",
                                   "欢迎使用Meshtastic!\n滑动切换\n长按选择\n或打开菜单。");
#elif defined(BUTTON_PIN)
    bannerOptions.message = UI_STR("Welcome to Meshtastic!\nClick to navigate and\nlong press to select\nor open a menu.",
                                   "欢迎使用Meshtastic!\n点击切换\n长按选择\n或打开菜单。");
#else
    bannerOptions.message = UI_STR("Welcome to Meshtastic!\nUse the Select button\nto open menus\nand make selections.",
                                   "欢迎使用Meshtastic!\n按选择键打开菜单\n并进行选择。");
#endif
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        menuHandler::menuQueue = menuHandler::NoTimeoutLoraPicker;
        screen->runNow();
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::LoraRegionPicker(uint32_t duration)
{
    static const LoraRegionOption regionOptions[] = {
        {UI_STR("Back", "返回"), OptionsAction::Back},
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

    const char *bannerMessage = UI_STR("Set the LoRa region", "设置LoRa区域");
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerMessage = UI_STR("LoRa Region", "LoRa区域");
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

void menuHandler::deviceRolePicker()
{
    static const char *optionsArray[] = {UI_STR("Back", "返回"),
                                         UI_STR("Client", "客户端"),
                                         UI_STR("Client Mute", "客户端静音"),
                                         UI_STR("Lost and Found", "失物招领"),
                                         UI_STR("Tracker", "追踪器")};
    enum optionsNumbers {
        Back = 0,
        devicerole_client = 1,
        devicerole_clientmute = 2,
        devicerole_lostandfound = 3,
        devicerole_tracker = 4
    };
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Device Role", "设备角色");
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 5;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Back) {
            menuHandler::menuQueue = menuHandler::LoraMenu;
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
    double bw = loraConfig.use_preset ? modemPresetToBwKHz(loraConfig.modem_preset, myRegion->wideLora)
                                      : bwCodeToKHz(loraConfig.bandwidth);

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
            menuHandler::menuQueue = menuHandler::LoraMenu;
            screen->runNow();
            return;
        }

        config.lora.channel_num = selected;
        service->reloadConfig(SEGMENT_CONFIG);
        rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
    };

    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::radioPresetPicker()
{
    static const RadioPresetOption presetOptions[] = {
        {UI_STR("Back", "返回"), OptionsAction::Back},
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
        createStaticBannerOptions(UI_STR("Radio Preset", "电台预设"), presetOptions, presetLabels,
                                  [](const RadioPresetOption &option, int) -> void {
            if (option.action == OptionsAction::Back) {
                menuHandler::menuQueue = menuHandler::LoraMenu;
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

void menuHandler::twelveHourPicker()
{
    static const char *optionsArray[] = {UI_STR("Back", "返回"), UI_STR("12-hour", "12小时制"), UI_STR("24-hour", "24小时制")};
    enum optionsNumbers { Back = 0, twelve = 1, twentyfour = 2 };
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Time Format", "时间格式");
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Back) {
            menuHandler::menuQueue = menuHandler::ClockMenu;
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
    static const char *confirmOptions[] = {UI_STR("No", "否"), UI_STR(UI_STR("Yes", "是"), "是")};
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

void menuHandler::clockFacePicker()
{
    static const ClockFaceOption clockFaceOptions[] = {
        {UI_STR("Back", "返回"), OptionsAction::Back},
        {UI_STR("Digital", "数字"), OptionsAction::Select, false},
        {UI_STR("Analog", "指针"), OptionsAction::Select, true},
    };

    constexpr size_t clockFaceCount = sizeof(clockFaceOptions) / sizeof(clockFaceOptions[0]);
    static std::array<const char *, clockFaceCount> clockFaceLabels{};

    auto bannerOptions = createStaticBannerOptions(UI_STR("Which Face?", "选择表盘"), clockFaceOptions, clockFaceLabels,
                                                   [](const ClockFaceOption &option, int) -> void {
                                                       if (option.action == OptionsAction::Back) {
                                                           menuHandler::menuQueue = menuHandler::ClockMenu;
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
        {UI_STR("Back", "返回"), OptionsAction::Back},
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
        UI_STR("Pick Timezone", "选择时区"), timezoneOptions, timezoneLabels, [](const TimezoneOption &option, int) -> void {
            if (option.action == OptionsAction::Back) {
                menuHandler::menuQueue = menuHandler::ClockMenu;
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
    static const char *optionsArray[] = {UI_STR("Back", "返回"), UI_STR("Time Format", "时间格式"), UI_STR("Timezone", "时区")};
#else
    static const char *optionsArray[] = {UI_STR("Back", "返回"), UI_STR("Clock Face", "表盘"),
                                         UI_STR("Time Format", "时间格式"), UI_STR("Timezone", "时区")};
#endif
    enum optionsNumbers { Back = 0, Clock = 1, Time = 2, Timezone = 3 };
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Clock Action", "时钟操作");
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 4;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Clock) {
            menuHandler::menuQueue = menuHandler::ClockFacePicker;
            screen->runNow();
        } else if (selected == Time) {
            menuHandler::menuQueue = menuHandler::TwelveHourPicker;
            screen->runNow();
        } else if (selected == Timezone) {
            menuHandler::menuQueue = menuHandler::TzPicker;
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

    optionsArray[options] = UI_STR("Back", "返回");
    optionsEnumArray[options++] = Back;

    // New Reply submenu (replaces Preset and Freetext directly in this menu)
    optionsArray[options] = UI_STR("Reply", "回复");
    optionsEnumArray[options++] = ReplyMenu;

    optionsArray[options] = UI_STR("View Chats", "对话");
    optionsEnumArray[options++] = ViewMode;

    // If viewing ALL chats, hide “Mute Chat”
    if (mode != graphics::MessageRenderer::ThreadMode::ALL && mode != graphics::MessageRenderer::ThreadMode::DIRECT) {
        const uint8_t chIndex = (threadChannel != 0) ? (uint8_t)threadChannel : channels.getPrimaryIndex();
        const auto &chan = channels.getByIndex(chIndex);

        optionsArray[options] = chan.settings.module_settings.is_muted ? UI_STR("Unmute Channel", "取消静音")
                                                                       : UI_STR("Mute Channel", "静音频道");
        optionsEnumArray[options++] = MuteChannel;
    }

    // Delete submenu
    optionsArray[options] = UI_STR("Delete", "删除");
    optionsEnumArray[options++] = DeleteMenu;

#ifdef HAS_I2S
    optionsArray[options] = UI_STR("Read Aloud", "朗读");
    optionsEnumArray[options++] = Aloud;
#endif

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Message Action", "消息操作");
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = UI_STR("Message", "消息");
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
            menuHandler::menuQueue = menuHandler::MessageViewModeMenu;
            screen->runNow();

            // Reply submenu
        } else if (selected == ReplyMenu) {
            menuHandler::menuQueue = menuHandler::ReplyMenu;
            screen->runNow();

        } else if (selected == MuteChannel) {
            const uint8_t chIndex = (ch != 0) ? (uint8_t)ch : channels.getPrimaryIndex();
            auto &chan = channels.getByIndex(chIndex);
            if (chan.settings.has_module_settings) {
                chan.settings.module_settings.is_muted = !chan.settings.module_settings.is_muted;
                nodeDB->saveToDisk();
            }

        } else if (selected == DeleteMenu) {
            menuHandler::menuQueue = menuHandler::DeleteMessagesMenu;
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
    optionsArray[options] = UI_STR("Back", "返回");
    optionsEnumArray[options++] = Back;

    // Preset reply
    optionsArray[options] = UI_STR("With Preset", "预设回复");
    optionsEnumArray[options++] = ReplyPreset;

    // Freetext reply (only when keyboard exists)
    if (kb_found) {
        optionsArray[options] = UI_STR("With Freetext", "自由输入");
        optionsEnumArray[options++] = ReplyFreetext;
    }

    BannerOverlayOptions bannerOptions;

    // Dynamic title based on thread mode
    auto mode = graphics::MessageRenderer::getThreadMode();
    if (mode == graphics::MessageRenderer::ThreadMode::CHANNEL) {
        bannerOptions.message = UI_STR("Reply to Channel", "回复频道");
    } else if (mode == graphics::MessageRenderer::ThreadMode::DIRECT) {
        bannerOptions.message = UI_STR("Reply to DM", "回复私信");
    } else {
        // View All
        bannerOptions.message = UI_STR("Reply to Last Msg", "回复上条");
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
            menuHandler::menuQueue = menuHandler::MessageResponseMenu;
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

    optionsArray[options] = UI_STR("Back", "返回");
    optionsEnumArray[options++] = Back;

    optionsArray[options] = UI_STR("Delete Oldest", "删最旧");
    optionsEnumArray[options++] = DeleteOldest;

    // If viewing ALL chats → hide “Delete This Chat”
    if (mode != graphics::MessageRenderer::ThreadMode::ALL) {
        optionsArray[options] = UI_STR("Delete This Chat", "删当前");
        optionsEnumArray[options++] = DeleteThis;
    }
    if (currentResolution == ScreenResolution::UltraLow) {
        optionsArray[options] = UI_STR("Delete All", "全删");
    } else {
        optionsArray[options] = UI_STR("Delete All Chats", "删除全部");
    }
    optionsEnumArray[options++] = DeleteAll;

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Delete Messages", "删除消息");

    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [mode](int selected) -> void {
        int ch = graphics::MessageRenderer::getThreadChannel();
        uint32_t peer = graphics::MessageRenderer::getThreadPeer();

        if (selected == Back) {
            menuHandler::menuQueue = menuHandler::MessageResponseMenu;
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

    labels.push_back(UI_STR("Back", "返回"));
    ids.push_back(-1);
    labels.push_back(UI_STR("View All Chats", "查看所有聊天"));
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
    for (const auto &m : dms) {
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
    bannerOptions.message = UI_STR("Select Conversation", "选择对话");
    bannerOptions.optionsArrayPtr = options.data();
    bannerOptions.optionsEnumPtr = optionIds.data();
    bannerOptions.optionsCount = options.size();
    bannerOptions.InitialSelected = initialIndex;

    bannerOptions.bannerCallback = [=](int selected) -> void {
        LOG_DEBUG("messageViewModeMenu: selected=%d", selected);
        if (selected == -1) {
            menuHandler::menuQueue = menuHandler::MessageResponseMenu;
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
    enum optionsNumbers { Back, Mute, Backlight, Position, Preset, Freetext, Sleep, Flashlight, enumEnd };

    static const char *optionsArray[enumEnd] = {UI_STR("Back", "返回")};
    static int optionsEnumArray[enumEnd] = {Back};
    int options = 1;

    if (moduleConfig.external_notification.enabled && externalNotificationModule &&
        config.device.buzzer_mode != meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED) {
        if (!externalNotificationModule->getMute()) {
            optionsArray[options] = UI_STR("Temporarily Mute", "临时静音");
        } else {
            optionsArray[options] = UI_STR("Unmute", "取消静音");
        }
        optionsEnumArray[options++] = Mute;
    }
#if defined(PIN_EINK_EN) || defined(PCA_PIN_EINK_EN)
    optionsArray[options] = UI_STR("Toggle Backlight", "背光开关");
    optionsEnumArray[options++] = Backlight;
#else
    optionsArray[options] = UI_STR("Sleep Screen", "息屏");
    optionsEnumArray[options++] = Sleep;
#endif
    if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        optionsArray[options] = UI_STR("Send Position", "发送位置");
    } else {
        optionsArray[options] = UI_STR("Send Node Info", "发送节点");
    }
    optionsEnumArray[options++] = Position;
#if defined(HAS_NEOPIXEL)
    const bool flashlightActive = (ambientLightingThread != nullptr) && ambientLightingThread->isFlashlightModeActive();
    optionsArray[options] = flashlightActive ? UI_STR("Flashlight Off", "关闭手电") : UI_STR("Flashlight", "手电筒");
    optionsEnumArray[options++] = Flashlight;
#endif

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Home Action", "主页操作");
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = UI_STR("Home", "主页");
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
                IF_SCREEN(screen->showSimpleBanner(UI_STR("Position\nSent", "位置\n已发"), 3000));
            } else {
                IF_SCREEN(screen->showSimpleBanner(UI_STR("Node Info\nSent", "节点信息\n已发"), 3000));
            }
        } else if (selected == Flashlight) {
#if defined(HAS_NEOPIXEL)
            if (ambientLightingThread != nullptr) {
                if (ambientLightingThread->isFlashlightModeActive()) {
                    ambientLightingThread->setFlashlightMode(false);
                } else {
                    if (externalNotificationModule != nullptr) {
                        externalNotificationModule->stopNow();
                    }
                    ambientLightingThread->setFlashlightMode(true);
                }
            }
#endif
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

    static const char *optionsArray[enumEnd] = {UI_STR("Back", "返回")};
    static int optionsEnumArray[enumEnd] = {Back};
    int options = 1;
    optionsArray[options] = UI_STR("New Preset Msg", "新预设消息");
    optionsEnumArray[options++] = Preset;
    if (kb_found) {
        optionsArray[options] = UI_STR("New Freetext Msg", "新自由文本");
        optionsEnumArray[options++] = Freetext;
    }

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Message Action", "消息操作");
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
    static const char *optionsArray[enumEnd] = {UI_STR("Back", "返回")};
    static int optionsEnumArray[enumEnd] = {Back};
    int options = 1;

    optionsArray[options] = UI_STR("Notifications", "通知");
    optionsEnumArray[options++] = Notifications;

    optionsArray[options] = UI_STR("Display Options", "显示设置");
    optionsEnumArray[options++] = ScreenOptions;

    if (currentResolution == ScreenResolution::UltraLow) {
        optionsArray[options] = UI_STR("Bluetooth", "蓝牙");
    } else {
        optionsArray[options] = UI_STR("Bluetooth Toggle", "蓝牙开关");
    }
    optionsEnumArray[options++] = Bluetooth;
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
    optionsArray[options] = UI_STR("WiFi Toggle", "WiFi开关");
    optionsEnumArray[options++] = WiFiToggle;
#endif

    if (currentResolution == ScreenResolution::UltraLow) {
        optionsArray[options] = UI_STR("Power", "电源");
    } else {
        optionsArray[options] = UI_STR("Reboot/Shutdown", "重启/关机");
    }
    optionsEnumArray[options++] = PowerMenu;

    if (test_enabled) {
        optionsArray[options] = UI_STR("Test Menu", "测试菜单");
        optionsEnumArray[options++] = Test;
    }

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("System Action", "系统操作");
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = UI_STR("System", "系统");
    }
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = options;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Notifications) {
            menuHandler::menuQueue = menuHandler::BuzzerModeMenuPicker;
            screen->runNow();
        } else if (selected == ScreenOptions) {
            menuHandler::menuQueue = menuHandler::ScreenOptionsMenu;
            screen->runNow();
        } else if (selected == PowerMenu) {
            menuHandler::menuQueue = menuHandler::PowerMenu;
            screen->runNow();
        } else if (selected == Test) {
            menuHandler::menuQueue = menuHandler::TestMenu;
            screen->runNow();
        } else if (selected == Bluetooth) {
            menuQueue = BluetoothToggleMenu;
            screen->runNow();
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
        } else if (selected == WiFiToggle) {
            menuQueue = WifiToggleMenu;
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

    static const char *optionsArray[enumEnd] = {UI_STR("Back", "返回")};
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
        optionsArray[options] = UI_STR("Go To Chat", "进入聊天");
        optionsEnumArray[options++] = GoToChat;
    }
    if (currentResolution == ScreenResolution::UltraLow) {
        optionsArray[options] = UI_STR("New Preset", "新预设");
    } else {
        optionsArray[options] = UI_STR("New Preset Msg", "新预设消息");
    }
    optionsEnumArray[options++] = Preset;

    if (kb_found) {
        optionsArray[options] = UI_STR("New Freetext Msg", "新自由文本");
        optionsEnumArray[options++] = Freetext;
    }

    if (currentResolution != ScreenResolution::UltraLow) {
        optionsArray[options] = UI_STR("Trace Route", "路由追踪");
        optionsEnumArray[options++] = TraceRoute;
    }
    optionsArray[options] = UI_STR("Remove Favorite", "取消收藏");
    optionsEnumArray[options++] = Remove;

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Favorites Action", "收藏操作");
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = UI_STR("Favorites", "收藏");
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
            menuHandler::menuQueue = menuHandler::RemoveFavorite;
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
        {UI_STR("Back", "返回"), OptionsAction::Back},
        {UI_STR("On/Off Toggle", "开关"), OptionsAction::Select, static_cast<int>(PositionAction::GpsToggle)},
        {UI_STR("Format", "格式"), OptionsAction::Select, static_cast<int>(PositionAction::GpsFormat)},
        {UI_STR("Smart Position", "智能位置"), OptionsAction::Select, static_cast<int>(PositionAction::GPSSmartPosition)},
        {UI_STR("Update Interval", "更新间隔"), OptionsAction::Select, static_cast<int>(PositionAction::GPSUpdateInterval)},
        {UI_STR("Broadcast Interval", "广播间隔"), OptionsAction::Select, static_cast<int>(PositionAction::GPSPositionBroadcast)},
        {UI_STR("Compass", "罗盘"), OptionsAction::Select, static_cast<int>(PositionAction::CompassMenu)},
    };

    static const PositionMenuOption calibrateOptions[] = {
        {UI_STR("Back", "返回"), OptionsAction::Back},
        {UI_STR("On/Off Toggle", "开关"), OptionsAction::Select, static_cast<int>(PositionAction::GpsToggle)},
        {UI_STR("Format", "格式"), OptionsAction::Select, static_cast<int>(PositionAction::GpsFormat)},
        {UI_STR("Smart Position", "智能位置"), OptionsAction::Select, static_cast<int>(PositionAction::GPSSmartPosition)},
        {UI_STR("Update Interval", "更新间隔"), OptionsAction::Select, static_cast<int>(PositionAction::GPSUpdateInterval)},
        {UI_STR("Broadcast Interval", "广播间隔"), OptionsAction::Select, static_cast<int>(PositionAction::GPSPositionBroadcast)},
        {UI_STR("Compass", "罗盘"), OptionsAction::Select, static_cast<int>(PositionAction::CompassMenu)},
        {UI_STR("Compass Calibrate", "罗盘校准"), OptionsAction::Select, static_cast<int>(PositionAction::CompassCalibrate)},
    };

    constexpr size_t baseCount = sizeof(baseOptions) / sizeof(baseOptions[0]);
    static std::array<const char *, baseCount> baseLabels{};
#if !MESHTASTIC_EXCLUDE_ACCELEROMETER
    constexpr size_t calibrateCount = sizeof(calibrateOptions) / sizeof(calibrateOptions[0]);
    static std::array<const char *, calibrateCount> calibrateLabels{};
#endif

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
            menuQueue = GpsToggleMenu;
            screen->runNow();
            break;
        case PositionAction::GpsFormat:
            menuQueue = GpsFormatMenu;
            screen->runNow();
            break;
        case PositionAction::CompassMenu:
            menuQueue = CompassPointNorthMenu;
            screen->runNow();
            break;
        case PositionAction::CompassCalibrate:
#if !MESHTASTIC_EXCLUDE_ACCELEROMETER
            if (accelerometerThread) {
                accelerometerThread->calibrate(30);
            }
#endif
            break;
        case PositionAction::GPSSmartPosition:
            menuQueue = GpsSmartPositionMenu;
            screen->runNow();
            break;
        case PositionAction::GPSUpdateInterval:
            menuQueue = GpsUpdateIntervalMenu;
            screen->runNow();
            break;
        case PositionAction::GPSPositionBroadcast:
            menuQueue = GpsPositionBroadcastMenu;
            screen->runNow();
            break;
        }
    };

    BannerOverlayOptions bannerOptions;
#if !MESHTASTIC_EXCLUDE_ACCELEROMETER
    if (accelerometerThread) {
        bannerOptions = createStaticBannerOptions(UI_STR("GPS Action", "GPS操作"), calibrateOptions, calibrateLabels, onSelection);
    } else {
        bannerOptions = createStaticBannerOptions(UI_STR("GPS Action", "GPS操作"), baseOptions, baseLabels, onSelection);
    }
#else
    bannerOptions = createStaticBannerOptions("GPS Action", baseOptions, baseLabels, onSelection);
#endif

    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::nodeListMenu()
{
    enum optionsNumbers { Back, NodePicker, TraceRoute, Verify, Reset, NodeNameLength, enumEnd };
    static const char *optionsArray[enumEnd] = {UI_STR("Back", "返回")};
    static int optionsEnumArray[enumEnd] = {Back};
    int options = 1;

    optionsArray[options] = UI_STR("Node Actions / Settings", "节点操作 / 设置");
    optionsEnumArray[options++] = NodePicker;

    if (currentResolution != ScreenResolution::UltraLow) {
        optionsArray[options] = UI_STR("Show Long/Short Name", "显示长/短名称");
        optionsEnumArray[options++] = NodeNameLength;
    }
    optionsArray[options] = UI_STR("Reset NodeDB", "重置节点库");
    optionsEnumArray[options++] = Reset;

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Node Action", "节点操作");
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = options;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == NodePicker) {
            menuQueue = NodePickerMenu;
            screen->runNow();
        } else if (selected == Reset) {
            menuQueue = ResetNodeDbMenu;
            screen->runNow();
        } else if (selected == NodeNameLength) {
            menuHandler::menuQueue = menuHandler::NodeNameLengthMenu;
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
        menuQueue = ManageNodeMenu;
        screen->runNow();
    });
}

void menuHandler::manageNodeMenu()
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
            menuQueue = NodeBaseMenu;
            screen->runNow();
            return;
        }

        if (selected == Favorite) {
            const auto *n = nodeDB->getMeshNode(menuHandler::pickedNodeNum);
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
        {UI_STR("Back", "返回"), OptionsAction::Back},
        {UI_STR("Long", "长"), OptionsAction::Select, true},
        {UI_STR("Short", "短"), OptionsAction::Select, false},
    };

    constexpr size_t nodeNameCount = sizeof(nodeNameOptions) / sizeof(nodeNameOptions[0]);
    static std::array<const char *, nodeNameCount> nodeNameLabels{};

    auto bannerOptions = createStaticBannerOptions(UI_STR("Node Name Length", "名称长度"), nodeNameOptions, nodeNameLabels,
                                                   [](const NodeNameOption &option, int) -> void {
                                                       if (option.action == OptionsAction::Back) {
                                                           menuQueue = NodeBaseMenu;
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
                                                       service->reloadConfig(SEGMENT_CONFIG);
                                                       LOG_INFO("Setting names to %s", option.value ? "long" : "short");
                                                   });

    int initialSelection = config.display.use_long_node_name ? 1 : 2;
    bannerOptions.InitialSelected = initialSelection;

    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::resetNodeDBMenu()
{
    static const char *optionsArray[] = {UI_STR("Back", "返回"), UI_STR("Reset All", "重置全部"), UI_STR("Preserve Favorites", "保留收藏")};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Confirm Reset NodeDB", "确认重置节点库");
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
            menuQueue = NodeBaseMenu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::compassNorthMenu()
{
    static const CompassOption compassOptions[] = {
        {UI_STR("Back", "返回"), OptionsAction::Back},
        {"Dynamic", OptionsAction::Select, meshtastic_CompassMode_DYNAMIC},
        {"Fixed Ring", OptionsAction::Select, meshtastic_CompassMode_FIXED_RING},
        {"Freeze Heading", OptionsAction::Select, meshtastic_CompassMode_FREEZE_HEADING},
    };

    constexpr size_t compassCount = sizeof(compassOptions) / sizeof(compassOptions[0]);
    static std::array<const char *, compassCount> compassLabels{};

    auto bannerOptions = createStaticBannerOptions(UI_STR("North Directions?", "方向设置?"), compassOptions, compassLabels,
                                                   [](const CompassOption &option, int) -> void {
                                                       if (option.action == OptionsAction::Back) {
                                                           menuQueue = PositionBaseMenu;
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
        {UI_STR("Back", "返回"), OptionsAction::Back},
        {UI_STR("Enabled", "开启"), OptionsAction::Select, meshtastic_Config_PositionConfig_GpsMode_ENABLED},
        {UI_STR("Disabled", "关闭"), OptionsAction::Select, meshtastic_Config_PositionConfig_GpsMode_DISABLED},
    };

    constexpr size_t toggleCount = sizeof(gpsToggleOptions) / sizeof(gpsToggleOptions[0]);
    static std::array<const char *, toggleCount> toggleLabels{};

    auto bannerOptions =
        createStaticBannerOptions(UI_STR("Toggle GPS", "GPS开关"), gpsToggleOptions, toggleLabels,
                                  [](const GPSToggleOption &option, int) -> void {
            if (option.action == OptionsAction::Back) {
                menuQueue = PositionBaseMenu;
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
        {UI_STR("Back", "返回"), OptionsAction::Back},
        {UI_STR("Decimal Degrees", "十进制度"), OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_DEC},
        {UI_STR("Degrees Minutes Seconds", "度分秒"), OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_DMS},
        {UI_STR("Universal Transverse Mercator", "UTM坐标"), OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_UTM},
        {UI_STR("Military Grid Reference System", "MGRS网格"), OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_MGRS},
        {UI_STR("Open Location Code", "开放位置码"), OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_OLC},
        {UI_STR("Ordnance Survey Grid Ref", "OSGR坐标"), OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_OSGR},
        {UI_STR("Maidenhead Locator", "梅登网格"), OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_MLS},
    };

    static const GPSFormatOption formatOptionsLow[] = {
        {UI_STR("Back", "返回"), OptionsAction::Back},
        {UI_STR("DEC", "十进"), OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_DEC},
        {UI_STR("DMS", "度分"), OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_DMS},
        {UI_STR("UTM", "UTM"), OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_UTM},
        {UI_STR("MGRS", "MGRS"), OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_MGRS},
        {UI_STR("OLC", "OLC"), OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_OLC},
        {UI_STR("OSGR", "OSGR"), OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_OSGR},
        {UI_STR("MLS", "MLS"), OptionsAction::Select, meshtastic_DeviceUIConfig_GpsCoordinateFormat_MLS},
    };

    constexpr size_t formatCount = sizeof(formatOptionsHigh) / sizeof(formatOptionsHigh[0]);
    static std::array<const char *, formatCount> formatLabelsHigh{};
    static std::array<const char *, formatCount> formatLabelsLow{};

    auto onSelection = [](const GPSFormatOption &option, int) -> void {
        if (option.action == OptionsAction::Back) {
            menuQueue = PositionBaseMenu;
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
    static const char *optionsArray[] = {UI_STR("Back", "返回"), UI_STR("Enabled", "开启"), UI_STR("Disabled", "关闭")};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Toggle Smart Position", "智能位置开关");
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = UI_STR("Smrt Postn", "智能位置");
    }
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 0) {
            menuQueue = PositionBaseMenu;
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
    static const char *optionsArray[] = {UI_STR("Back", "返回"),
                                         UI_STR("8 seconds", "8秒"),
                                         UI_STR("20 seconds", "20秒"),
                                         UI_STR("40 seconds", "40秒"),
                                         UI_STR("1 minute", "1分钟"),
                                         UI_STR("80 seconds", "80秒"),
                                         UI_STR("2 minutes", "2分钟"),
                                         UI_STR("5 minutes", "5分钟"),
                                         UI_STR("10 minutes", "10分钟"),
                                         UI_STR("15 minutes", "15分钟"),
                                         UI_STR("30 minutes", "30分钟"),
                                         UI_STR("1 hour", "1小时"),
                                         UI_STR("6 hours", "6小时"),
                                         UI_STR("12 hours", "12小时"),
                                         UI_STR("24 hours", "24小时"),
                                         UI_STR("At Boot Only", "仅开机时")};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Update Interval", "更新间隔");
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 16;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 0) {
            menuQueue = PositionBaseMenu;
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
    static const char *optionsArray[] = {UI_STR("Back", "返回"),
                                         UI_STR("1 minute", "1分钟"),
                                         UI_STR("90 seconds", "90秒"),
                                         UI_STR("5 minutes", "5分钟"),
                                         UI_STR("15 minutes", "15分钟"),
                                         UI_STR("1 hour", "1小时"),
                                         UI_STR("2 hours", "2小时"),
                                         UI_STR("3 hours", "3小时"),
                                         UI_STR("4 hours", "4小时"),
                                         UI_STR("5 hours", "5小时"),
                                         UI_STR("6 hours", "6小时"),
                                         UI_STR("12 hours", "12小时"),
                                         UI_STR("18 hours", "18小时"),
                                         UI_STR("24 hours", "24小时"),
                                         UI_STR("36 hours", "36小时"),
                                         UI_STR("48 hours", "48小时"),
                                         UI_STR("72 hours", "72小时")};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Broadcast Interval", "广播间隔");
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 17;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 0) {
            menuQueue = PositionBaseMenu;
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

void menuHandler::bluetoothToggleMenu()
{
    static const char *optionsArray[] = {UI_STR("Back", "返回"), UI_STR("Enabled", "开启"), UI_STR("Disabled", "关闭")};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Toggle Bluetooth", "蓝牙开关");
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = UI_STR("Bluetooth", "蓝牙");
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
    static const char *optionsArray[] = {UI_STR("All Enabled", "全开"),
                                         UI_STR("All Disabled", "全关"),
                                         UI_STR("Notifications", "通知"),
                                         UI_STR("System Only", "仅系统"),
                                         UI_STR("DMs Only", "仅私信")};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Notification Sounds", "通知声音");
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
    static const char *optionsArray[] = {UI_STR("Back", "返回"), UI_STR("Low", "低"), UI_STR("Medium", "中"),
                                         UI_STR("High", "高")};

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
    bannerOptions.message = UI_STR("Brightness", "亮度");
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
    static const char *optionsArray[] = {"No", UI_STR("Yes", "是")};
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
        {UI_STR("Back", "返回"), OptionsAction::Back},
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
        UI_STR("Select Screen Color", "选择屏幕颜色"), colorOptions, colorLabels, [display](const ScreenColorOption &option, int) -> void {
            if (option.action == OptionsAction::Back) {
                menuQueue = SystemBaseMenu;
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
    static const char *optionsArray[] = {UI_STR("Back", "返回"), "Confirm"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Reboot Device?", "重启设备?");
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = UI_STR("Reboot", "重启");
    }
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            IF_SCREEN(screen->showSimpleBanner(UI_STR("Rebooting...", "正在重启..."), 0));
            nodeDB->saveToDisk();
            messageStore.saveToFlash();
            rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
        } else {
            menuQueue = PowerMenu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::shutdownMenu()
{
    static const char *optionsArray[] = {UI_STR("Back", "返回"), "Confirm"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Shutdown Device?", "关机?");
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = UI_STR("Shutdown", "关机");
    }
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            InputEvent event = {.inputEvent = (input_broker_event)INPUT_BROKER_SHUTDOWN, .kbchar = 0, .touchX = 0, .touchY = 0};
            inputBroker->injectInputEvent(&event);
        } else {
            menuQueue = PowerMenu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::addFavoriteMenu()
{
    const char *NODE_PICKER_TITLE;
    if (currentResolution == ScreenResolution::UltraLow) {
        NODE_PICKER_TITLE = UI_STR("Node Favorite", "节点收藏");
    } else {
        NODE_PICKER_TITLE = UI_STR("Node To Favorite", "选择收藏节点");
    }
    screen->showNodePicker(NODE_PICKER_TITLE, 30000, [](uint32_t nodenum) -> void {
        LOG_WARN("Nodenum: %u", nodenum);
        nodeDB->set_favorite(true, nodenum);
        screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
    });
}

void menuHandler::removeFavoriteMenu()
{

    static const char *optionsArray[] = {UI_STR("Back", "返回"), UI_STR("Yes", "是")};
    BannerOverlayOptions bannerOptions;
    std::string message = UI_STR("Unfavorite This Node?\n", "取消收藏该节点?\n");
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
    screen->showNodePicker(UI_STR("Node to Trace", "选择追踪节点"), 30000, [](uint32_t nodenum) -> void {
        LOG_INFO("Menu: Node picker selected node 0x%08x, traceRouteModule=%p", nodenum, traceRouteModule);
        if (traceRouteModule) {
            traceRouteModule->startTraceRoute(nodenum);
        }
    });
}

void menuHandler::testMenu()
{

    enum optionsNumbers { Back, NumberPicker, ShowChirpy,TestAnnounce };
    static const char *optionsArray[4] = {UI_STR("Back", "返回")};
    static int optionsEnumArray[4] = {Back};
    int options = 1;

    optionsArray[options] = "Number Picker";
    optionsEnumArray[options++] = NumberPicker;

    optionsArray[options] = screen->isFrameHidden("chirpy") ? "Show Chirpy" : "Hide Chirpy";
    optionsEnumArray[options++] = ShowChirpy;
#ifdef HAS_I2S
    optionsArray[options] = "Test Announce";
    optionsEnumArray[options++] = TestAnnounce;
#endif

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Hidden Test Menu";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = options;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == NumberPicker) {
            menuQueue = NumberTest;
            screen->runNow();
        } else if (selected == ShowChirpy) {
            screen->toggleFrameVisibility("chirpy");
            screen->setFrames(Screen::FOCUS_SYSTEM);

        } else if (selected == TestAnnounce) {
#ifdef HAS_I2S
            audioThread->readAloud("This is a test of the emergency broadcast system. This is only a test.");
#endif
        } else {
            menuQueue = SystemBaseMenu;
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

    static const char *optionsArray[] = {UI_STR("Back", "返回"), UI_STR("WiFi Toggle", "WiFi开关")};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("WiFi Menu", "WiFi菜单");
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Wifi_toggle) {
            menuQueue = WifiToggleMenu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::wifiToggleMenu()
{
    enum optionsNumbers { Back, Wifi_disable, Wifi_enable };

    static const char *optionsArray[] = {UI_STR("Back", "返回"), UI_STR("WiFi Disabled", "WiFi关闭"),
                                         UI_STR("WiFi Enabled", "WiFi开启")};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("WiFi Actions", "WiFi操作");
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
#if defined(T_DECK)
    // TDeck Doesn't seem to support brightness at all, at least not reliably
    bool hasSupportBrightness = false;
#elif defined(ST7789_CS) || defined(USE_OLED) || defined(USE_SSD1306) || defined(USE_SH1106) || defined(USE_SH1107)
    bool hasSupportBrightness = true;
#else
    bool hasSupportBrightness = false;
#endif

    enum optionsNumbers { Back, Brightness, ScreenColor, FrameToggles, DisplayUnits, MessageBubbles };
    static const char *optionsArray[6] = {UI_STR("Back", "返回")};
    static int optionsEnumArray[6] = {Back};
    int options = 1;

    // Only show brightness for B&W displays
    if (hasSupportBrightness) {
        optionsArray[options] = UI_STR("Brightness", "亮度");
        optionsEnumArray[options++] = Brightness;
    }

    // Only show screen color for TFT displays
#if defined(HELTEC_MESH_NODE_T114) || defined(HELTEC_VISION_MASTER_T190) || defined(T_DECK) || defined(T_LORA_PAGER) ||          \
    HAS_TFT || defined(HACKADAY_COMMUNICATOR)
    optionsArray[options] = UI_STR("Screen Color", "屏幕颜色");
    optionsEnumArray[options++] = ScreenColor;
#endif

    optionsArray[options] = UI_STR("Frame Visibility", "框架可见性");
    optionsEnumArray[options++] = FrameToggles;

    optionsArray[options] = UI_STR("Display Units", "显示单元");
    optionsEnumArray[options++] = DisplayUnits;

    optionsArray[options] = UI_STR("Message Bubbles", "消息气泡");
    optionsEnumArray[options++] = MessageBubbles;

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Display Options", "显示设置");
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = options;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Brightness) {
            menuHandler::menuQueue = menuHandler::BrightnessPicker;
            screen->runNow();
        } else if (selected == ScreenColor) {
            menuHandler::menuQueue = menuHandler::TftColorMenuPicker;
            screen->runNow();
        } else if (selected == FrameToggles) {
            menuHandler::menuQueue = menuHandler::FrameToggles;
            screen->runNow();
        } else if (selected == DisplayUnits) {
            menuHandler::menuQueue = menuHandler::DisplayUnits;
            screen->runNow();
        } else if (selected == MessageBubbles) {
            menuHandler::menuQueue = menuHandler::MessageBubblesMenu;
            screen->runNow();
        } else {
            menuQueue = SystemBaseMenu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::powerMenu()
{

    enum optionsNumbers { Back, Reboot, Shutdown, MUI };
    static const char *optionsArray[4] = {UI_STR("Back", "返回")};
    static int optionsEnumArray[4] = {Back};
    int options = 1;

    optionsArray[options] = UI_STR("Reboot", "重启");
    optionsEnumArray[options++] = Reboot;

    optionsArray[options] = UI_STR("Shutdown", "关机");
    optionsEnumArray[options++] = Shutdown;

#if HAS_TFT
    optionsArray[options] = "Switch to MUI";
    optionsEnumArray[options++] = MUI;
#endif

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Reboot / Shutdown", "重启/关机");
    if (currentResolution == ScreenResolution::UltraLow) {
        bannerOptions.message = UI_STR("Power", "电源");
    }
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = options;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == Reboot) {
            menuHandler::menuQueue = menuHandler::RebootMenu;
            screen->runNow();
        } else if (selected == Shutdown) {
            menuHandler::menuQueue = menuHandler::ShutdownMenu;
            screen->runNow();
        } else if (selected == MUI) {
            menuHandler::menuQueue = menuHandler::MuiPicker;
            screen->runNow();
        } else {
            menuQueue = SystemBaseMenu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::keyVerificationInitMenu()
{
    screen->showNodePicker(UI_STR("Node to Verify", "选择验证节点"), 30000,
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

void menuHandler::frameTogglesMenu()
{
    enum optionsNumbers {
        Finish,
        nodelist_nodes,
        nodelist_location,
        nodelist_lastheard,
        nodelist_hopsignal,
        nodelist_distance,
        nodelist_bearings,
        gps_position,
        lora,
        clock,
        show_favorites,
        show_env_telemetry,
        show_aq_telemetry,
        show_power,
        enumEnd
    };
    static const char *optionsArray[enumEnd] = {UI_STR("Finish", "完成")};
    static int optionsEnumArray[enumEnd] = {Finish};
    int options = 1;

    // Track last selected index (not enum value!)
    static int lastSelectedIndex = 0;

#ifndef USE_EINK
    optionsArray[options] = screen->isFrameHidden("nodelist_nodes") ? UI_STR("Show Node Lists", "显示节点列表") : UI_STR("Hide Node Lists", "隐藏节点列表");
    optionsEnumArray[options++] = nodelist_nodes;
#else
    optionsArray[options] = screen->isFrameHidden("nodelist_lastheard") ? UI_STR("Show NL - Last Heard", "显示列表-最近收到") : UI_STR("Hide NL - Last Heard", "隐藏列表-最近收到");
    optionsEnumArray[options++] = nodelist_lastheard;
    optionsArray[options] = screen->isFrameHidden("nodelist_hopsignal") ? UI_STR("Show NL - Hops/Signal", "显示列表-跳/信") : UI_STR("Hide NL - Hops/Signal", "隐藏列表-跳/信");
    optionsEnumArray[options++] = nodelist_hopsignal;
#endif

#if HAS_GPS
#ifndef USE_EINK
    optionsArray[options] = screen->isFrameHidden("nodelist_location") ? UI_STR("Show Position Lists", "显示位置列表")
                                                                       : UI_STR("Hide Position Lists", "隐藏位置列表");
    optionsEnumArray[options++] = nodelist_location;
#else
    optionsArray[options] = screen->isFrameHidden("nodelist_distance") ? UI_STR("Show NL - Distance", "显示列表-距离") : UI_STR("Hide NL - Distance", "隐藏列表-距离");
    optionsEnumArray[options++] = nodelist_distance;
    optionsArray[options] = screen->isFrameHidden("nodelist_bearings") ? UI_STR("Show NL - Bearings", "显示列表-方位") : UI_STR("Hide NL - Bearings", "隐藏列表-方位");
    optionsEnumArray[options++] = nodelist_bearings;
#endif

    optionsArray[options] = screen->isFrameHidden("gps") ? UI_STR("Show Position", "显示位置")
                                                         : UI_STR("Hide Position", "隐藏位置");
    optionsEnumArray[options++] = gps_position;
#endif

    optionsArray[options] = screen->isFrameHidden("lora") ? UI_STR("Show LoRa", "显示LoRa")
                                                          : UI_STR("Hide LoRa", "隐藏LoRa");
    optionsEnumArray[options++] = lora;

    optionsArray[options] = screen->isFrameHidden("clock") ? UI_STR("Show Clock", "显示时钟") : UI_STR("Hide Clock", "隐藏时钟");
    optionsEnumArray[options++] = clock;

    optionsArray[options] = screen->isFrameHidden("show_favorites") ? UI_STR("Show Favorites", "显示收藏") : UI_STR("Hide Favorites", "隐藏收藏");
    optionsEnumArray[options++] = show_favorites;

    optionsArray[options] = moduleConfig.telemetry.environment_screen_enabled
                                ? UI_STR("Hide Env. Telemetry", "隐藏环境遥测")
                                : UI_STR("Show Env. Telemetry", "显示环境遥测");
    optionsEnumArray[options++] = show_env_telemetry;

    optionsArray[options] = moduleConfig.telemetry.air_quality_screen_enabled
                                ? UI_STR("Hide AQ Telemetry", "隐藏空气质量遥测")
                                : UI_STR("Show AQ Telemetry", "显示空气质量遥测");
    optionsEnumArray[options++] = show_aq_telemetry;

    optionsArray[options] = moduleConfig.telemetry.power_screen_enabled ? UI_STR("Hide Power", "隐藏电源")
                                                                        : UI_STR("Show Power", "显示电源");
    optionsEnumArray[options++] = show_power;

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Show/Hide Frames", "显示/隐藏页面");
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
        } else if (selected == gps_position) {
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

void menuHandler::displayUnitsMenu()
{
    enum optionsNumbers { Back, MetricUnits, ImperialUnits };

    static const char *optionsArray[] = {UI_STR("Back", "返回"), UI_STR("Metric", "公制"), UI_STR("Imperial", "英制")};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Select display units", "选择显示单位");
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
            menuHandler::menuQueue = menuHandler::ScreenOptionsMenu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::messageBubblesMenu()
{
    enum optionsNumbers { Back, ShowBubbles, HideBubbles };

    static const char *optionsArray[] = {UI_STR("Back", "返回"), UI_STR("Show Bubbles", "显示气泡"),
                                         UI_STR("Hide Bubbles", "隐藏气泡")};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = UI_STR("Message Bubbles", "消息气泡");
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    bannerOptions.InitialSelected = config.display.enable_message_bubbles ? 1 : 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == ShowBubbles) {
            config.display.enable_message_bubbles = true;
            service->reloadConfig(SEGMENT_CONFIG);
            LOG_INFO("Message bubbles enabled");
        } else if (selected == HideBubbles) {
            config.display.enable_message_bubbles = false;
            service->reloadConfig(SEGMENT_CONFIG);
            LOG_INFO("Message bubbles disabled");
        } else {
            menuHandler::menuQueue = menuHandler::ScreenOptionsMenu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::handleMenuSwitch(OLEDDisplay *display)
{
    if (menuQueue != MenuNone)
        test_count = 0;
    switch (menuQueue) {
    case MenuNone:
        break;
    case LoraMenu:
        loraMenu();
        break;
    case LoraPicker:
        LoraRegionPicker();
        break;
    case DeviceRolePicker:
        deviceRolePicker();
        break;
    case RadioPresetPicker:
        radioPresetPicker();
        break;
    case FrequencySlot:
        FrequencySlotPicker();
        break;
    case NoTimeoutLoraPicker:
        LoraRegionPicker(0);
        break;
    case TzPicker:
        TZPicker();
        break;
    case TwelveHourPicker:
        twelveHourPicker();
        break;
    case ClockFacePicker:
        clockFacePicker();
        break;
    case ClockMenu:
        clockMenu();
        break;
    case SystemBaseMenu:
        systemBaseMenu();
        break;
    case PositionBaseMenu:
        positionBaseMenu();
        break;
    case NodeBaseMenu:
        nodeListMenu();
        break;
#if !MESHTASTIC_EXCLUDE_GPS
    case GpsToggleMenu:
        GPSToggleMenu();
        break;
    case GpsFormatMenu:
        GPSFormatMenu();
        break;
    case GpsSmartPositionMenu:
        GPSSmartPositionMenu();
        break;
    case GpsUpdateIntervalMenu:
        GPSUpdateIntervalMenu();
        break;
    case GpsPositionBroadcastMenu:
        GPSPositionBroadcastMenu();
        break;
#endif
    case CompassPointNorthMenu:
        compassNorthMenu();
        break;
    case ResetNodeDbMenu:
        resetNodeDBMenu();
        break;
    case BuzzerModeMenuPicker:
        BuzzerModeMenu();
        break;
    case MuiPicker:
        switchToMUIMenu();
        break;
    case TftColorMenuPicker:
        TFTColorPickerMenu(display);
        break;
    case BrightnessPicker:
        BrightnessPickerMenu();
        break;
    case NodeNameLengthMenu:
        nodeNameLengthMenu();
        break;
    case RebootMenu:
        rebootMenu();
        break;
    case ShutdownMenu:
        shutdownMenu();
        break;
    case NodePickerMenu:
        NodePicker();
        break;
    case ManageNodeMenu:
        manageNodeMenu();
        break;
    case RemoveFavorite:
        removeFavoriteMenu();
        break;
    case TraceRouteMenu:
        traceRouteMenu();
        break;
    case TestMenu:
        testMenu();
        break;
    case NumberTest:
        numberTest();
        break;
    case WifiToggleMenu:
        wifiToggleMenu();
        break;
    case KeyVerificationInit:
        keyVerificationInitMenu();
        break;
    case KeyVerificationFinalPrompt:
        keyVerificationFinalPrompt();
        break;
    case BluetoothToggleMenu:
        bluetoothToggleMenu();
        break;
    case ScreenOptionsMenu:
        screenOptionsMenu();
        break;
    case PowerMenu:
        powerMenu();
        break;
    case FrameToggles:
        frameTogglesMenu();
        break;
    case DisplayUnits:
        displayUnitsMenu();
        break;
    case ThrottleMessage:
        screen->showSimpleBanner(UI_STR("Too Many Attempts\nTry again in 60 seconds.", "尝试次数过多\n60秒后再试。"), 5000);
        break;
    case MessageResponseMenu:
        messageResponseMenu();
        break;
    case ReplyMenu:
        replyMenu();
        break;
    case DeleteMessagesMenu:
        deleteMessagesMenu();
        break;
    case MessageViewModeMenu:
        messageViewModeMenu();
        break;
    case MessageBubblesMenu:
        messageBubblesMenu();
        break;
    }
    menuQueue = MenuNone;
}

void menuHandler::saveUIConfig()
{
    nodeDB->saveProto("/prefs/uiconfig.proto", meshtastic_DeviceUIConfig_size, &meshtastic_DeviceUIConfig_msg, &uiconfig);
}

} // namespace graphics

#endif
