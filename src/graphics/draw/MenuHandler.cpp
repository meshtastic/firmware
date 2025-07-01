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
#include "main.h"
#include "modules/AdminModule.h"
#include "modules/CannedMessageModule.h"

extern uint16_t TFT_MESH;

namespace graphics
{
menuHandler::screenMenus menuHandler::menuQueue = menu_none;

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
                                         "ANZ_433"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Set the LoRa region";
    bannerOptions.durationMs = duration;
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 23;
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

void menuHandler::TwelveHourPicker()
{
    static const char *optionsArray[] = {"Back", "12-hour", "24-hour"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Time Format";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 0) {
            menuHandler::menuQueue = menuHandler::clock_menu;
        } else if (selected == 1) {
            config.display.use_12h_clock = true;
        } else {
            config.display.use_12h_clock = false;
        }
        service->reloadConfig(SEGMENT_CONFIG);
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::ClockFacePicker()
{
    static const char *optionsArray[] = {"Back", "Digital", "Analog"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Which Face?";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 0) {
            menuHandler::menuQueue = menuHandler::clock_menu;
        } else if (selected == 1) {
            graphics::ClockRenderer::digitalWatchFace = true;
            screen->setFrames(Screen::FOCUS_CLOCK);
        } else {
            graphics::ClockRenderer::digitalWatchFace = false;
            screen->setFrames(Screen::FOCUS_CLOCK);
        }
    };
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
    bannerOptions.optionsCount = 17;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 0) {
            menuHandler::menuQueue = menuHandler::clock_menu;
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
        } else if (selected == 8) { // UTC
            strncpy(config.device.tzdef, "UTC", sizeof(config.device.tzdef));
        } else if (selected == 9) { // EU/Western
            strncpy(config.device.tzdef, "GMT0BST,M3.5.0/1,M10.5.0", sizeof(config.device.tzdef));
        } else if (selected == 10) { // EU/Central
            strncpy(config.device.tzdef, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(config.device.tzdef));
        } else if (selected == 11) { // EU/Eastern
            strncpy(config.device.tzdef, "EET-2EEST,M3.5.0/3,M10.5.0/4", sizeof(config.device.tzdef));
        } else if (selected == 12) { // Asia/Kolkata
            strncpy(config.device.tzdef, "IST-5:30", sizeof(config.device.tzdef));
        } else if (selected == 13) { // China
            strncpy(config.device.tzdef, "HKT-8", sizeof(config.device.tzdef));
        } else if (selected == 14) { // AU/AWST
            strncpy(config.device.tzdef, "AWST-8", sizeof(config.device.tzdef));
        } else if (selected == 15) { // AU/ACST
            strncpy(config.device.tzdef, "ACST-9:30ACDT,M10.1.0,M4.1.0/3", sizeof(config.device.tzdef));
        } else if (selected == 16) { // AU/AEST
            strncpy(config.device.tzdef, "AEST-10AEDT,M10.1.0,M4.1.0/3", sizeof(config.device.tzdef));
        } else if (selected == 17) { // NZ
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
    static const char *optionsArray[] = {"Back", "Clock Face", "Time Format", "Timezone"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Clock Action";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 4;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            menuHandler::menuQueue = menuHandler::clock_face_picker;
            screen->setInterval(0);
            runASAP = true;
        } else if (selected == 2) {
            menuHandler::menuQueue = menuHandler::twelve_hour_picker;
            screen->setInterval(0);
            runASAP = true;
        } else if (selected == 3) {
            menuHandler::menuQueue = menuHandler::TZ_picker;
            screen->setInterval(0);
            runASAP = true;
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::messageResponseMenu()
{

    static const char **optionsArrayPtr;
    int options;
    if (kb_found) {
        static const char *optionsArray[] = {"Back", "Dismiss", "Reply via Preset", "Reply via Freetext"};
        optionsArrayPtr = optionsArray;
        options = 4;
    } else {
        static const char *optionsArray[] = {"Back", "Dismiss", "Reply via Preset"};
        optionsArrayPtr = optionsArray;
        options = 3;
    }
#ifdef HAS_I2S
    static const char *optionsArray[] = {"Back", "Dismiss", "Reply via Preset", "Reply via Freetext", "Read Aloud"};
    optionsArrayPtr = optionsArray;
    options = 5;
#endif
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Message Action";
    bannerOptions.optionsArrayPtr = optionsArrayPtr;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            screen->dismissCurrentFrame();
        } else if (selected == 2) {
            if (devicestate.rx_text_message.to == NODENUM_BROADCAST) {
                cannedMessageModule->LaunchWithDestination(NODENUM_BROADCAST, devicestate.rx_text_message.channel);
            } else {
                cannedMessageModule->LaunchWithDestination(devicestate.rx_text_message.from);
            }
        } else if (selected == 3) {
            if (devicestate.rx_text_message.to == NODENUM_BROADCAST) {
                cannedMessageModule->LaunchFreetextWithDestination(NODENUM_BROADCAST, devicestate.rx_text_message.channel);
            } else {
                cannedMessageModule->LaunchFreetextWithDestination(devicestate.rx_text_message.from);
            }
        }
#ifdef HAS_I2S
        else if (selected == 4) {
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
    int options;
    static const char **optionsArrayPtr;

    if (kb_found) {
#ifdef PIN_EINK_EN
        static const char *optionsArray[] = {"Back",           "Toggle Backlight", "Send Position",
                                             "New Preset Msg", "New Freetext Msg", "Bluetooth Toggle"};
#else
        static const char *optionsArray[] = {"Back",           "Sleep Screen",     "Send Position",
                                             "New Preset Msg", "New Freetext Msg", "Bluetooth Toggle"};
#endif
        optionsArrayPtr = optionsArray;
        options = 6;
    } else {
#ifdef PIN_EINK_EN
        static const char *optionsArray[] = {"Back", "Toggle Backlight", "Send Position", "New Preset Msg", "Bluetooth Toggle"};
#else
        static const char *optionsArray[] = {"Back", "Sleep Screen", "Send Position", "New Preset Msg", "Bluetooth Toggle"};
#endif
        optionsArrayPtr = optionsArray;
        options = 5;
    }
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Home Action";
    bannerOptions.optionsArrayPtr = optionsArrayPtr;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
#ifdef PIN_EINK_EN
            if (digitalRead(PIN_EINK_EN) == HIGH) {
                digitalWrite(PIN_EINK_EN, LOW);
            } else {
                digitalWrite(PIN_EINK_EN, HIGH);
            }
#else
            screen->setOn(false);
#endif
        } else if (selected == 2) {
            InputEvent event = {.inputEvent = (input_broker_event)INPUT_BROKER_SEND_PING, .kbchar = 0, .touchX = 0, .touchY = 0};
            inputBroker->injectInputEvent(&event);
        } else if (selected == 3) {
            cannedMessageModule->LaunchWithDestination(NODENUM_BROADCAST);
        } else if (selected == 4) {
            if (kb_found) {
                cannedMessageModule->LaunchFreetextWithDestination(NODENUM_BROADCAST);
            } else {
                InputEvent event = {.inputEvent = (input_broker_event)170, .kbchar = 170, .touchX = 0, .touchY = 0};
                inputBroker->injectInputEvent(&event);
            }
        } else if (selected == 5) {
            InputEvent event = {.inputEvent = (input_broker_event)170, .kbchar = 170, .touchX = 0, .touchY = 0};
            inputBroker->injectInputEvent(&event);
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::systemBaseMenu()
{
    int options;
    static const char **optionsArrayPtr;
#if HAS_TFT
    static const char *optionsArray[] = {"Back", "Beeps Action", "Reboot", "Switch to MUI"};
    options = 4;
#endif
#if defined(HELTEC_MESH_NODE_T114) || defined(HELTEC_VISION_MASTER_T190)
    static const char *optionsArray[] = {"Back", "Beeps Action", "Reboot", "Screen Color"};
    options = 4;
#endif
#if !defined(HELTEC_MESH_NODE_T114) && !defined(HELTEC_VISION_MASTER_T190) && !HAS_TFT
    static const char *optionsArray[] = {"Back", "Beeps Action", "Reboot"};
    options = 3;
#endif
    optionsArrayPtr = optionsArray;
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "System Action";
    bannerOptions.optionsArrayPtr = optionsArrayPtr;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            menuHandler::menuQueue = menuHandler::buzzermodemenupicker;
            screen->setInterval(0);
            runASAP = true;
        } else if (selected == 2) {
            menuHandler::menuQueue = menuHandler::reboot_menu;
            screen->setInterval(0);
            runASAP = true;
        } else if (selected == 3) {
#if HAS_TFT
            menuHandler::menuQueue = menuHandler::mui_picker;
#endif
#if defined(HELTEC_MESH_NODE_T114) || defined(HELTEC_VISION_MASTER_T190)
            menuHandler::menuQueue = menuHandler::tftcolormenupicker;
#endif
            screen->setInterval(0);
            runASAP = true;
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::favoriteBaseMenu()
{
    int options;
    static const char **optionsArrayPtr;

    if (kb_found) {
        static const char *optionsArray[] = {"Back", "New Preset Msg", "New Freetext Msg", "Remove Favorite"};
        optionsArrayPtr = optionsArray;
        options = 4;
    } else {
        static const char *optionsArray[] = {"Back", "New Preset Msg", "Remove Favorite"};
        optionsArrayPtr = optionsArray;
        options = 3;
    }
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Favorites Action";
    bannerOptions.optionsArrayPtr = optionsArrayPtr;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            cannedMessageModule->LaunchWithDestination(graphics::UIRenderer::currentFavoriteNodeNum);
        } else if (selected == 2 && kb_found) {
            cannedMessageModule->LaunchFreetextWithDestination(graphics::UIRenderer::currentFavoriteNodeNum);
        } else if ((!kb_found && selected == 2) || (selected == 3 && kb_found)) {
            menuHandler::menuQueue = menuHandler::remove_favorite;
            screen->setInterval(0);
            runASAP = true;
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::positionBaseMenu()
{
    int options;
    static const char **optionsArrayPtr;
    static const char *optionsArray[] = {"Back", "GPS Toggle", "Compass"};
    static const char *optionsArrayCalibrate[] = {"Back", "GPS Toggle", "Compass", "Compass Calibrate"};

    if (accelerometerThread) {
        optionsArrayPtr = optionsArrayCalibrate;
        options = 4;
    } else {
        optionsArrayPtr = optionsArray;
        options = 3;
    }
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Position Action";
    bannerOptions.optionsArrayPtr = optionsArrayPtr;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            menuQueue = gps_toggle_menu;
        } else if (selected == 2) {
            menuQueue = compass_point_north_menu;
        } else if (selected == 3) {
            accelerometerThread->calibrate(30);
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::nodeListMenu()
{
    static const char *optionsArray[] = {"Back", "Add Favorite", "Reset NodeDB"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Node Action";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 3;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            menuQueue = add_favorite;
        } else if (selected == 2) {
            menuQueue = reset_node_db_menu;
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
    static const char *optionsArray[] = {"Back", "Dynamic", "Fixed Ring", "Freeze Heading"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "North Directions?";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 4;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            if (config.display.compass_north_top != false) {
                config.display.compass_north_top = false;
                service->reloadConfig(SEGMENT_CONFIG);
            }
            screen->ignoreCompass = false;
            screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
        } else if (selected == 2) {
            if (config.display.compass_north_top != true) {
                config.display.compass_north_top = true;
                service->reloadConfig(SEGMENT_CONFIG);
            }
            screen->ignoreCompass = false;
            screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
        } else if (selected == 3) {
            if (config.display.compass_north_top != true) {
                config.display.compass_north_top = true;
                service->reloadConfig(SEGMENT_CONFIG);
            }
            screen->ignoreCompass = true;
            screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
        } else if (selected == 0) {
            menuQueue = position_base_menu;
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

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
        }
    };
    bannerOptions.InitialSelected = config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED ? 1 : 2;
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::BuzzerModeMenu()
{
    static const char *optionsArray[] = {"All Enabled", "Disabled", "Notifications", "System Only"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Beep Action";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 4;
    bannerOptions.bannerCallback = [](int selected) -> void {
        config.device.buzzer_mode = (meshtastic_Config_DeviceConfig_BuzzerMode)selected;
        service->reloadConfig(SEGMENT_CONFIG);
    };
    bannerOptions.InitialSelected = config.device.buzzer_mode;
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::switchToMUIMenu()
{
    static const char *optionsArray[] = {"Yes", "No"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Switch to MUI?";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 0) {
            config.display.displaymode = meshtastic_Config_DisplayConfig_DisplayMode_COLOR;
            config.bluetooth.enabled = false;
            service->reloadConfig(SEGMENT_CONFIG);
            rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::TFTColorPickerMenu()
{
    static const char *optionsArray[] = {"Back", "Default", "Meshtastic Green", "Yellow", "Red", "Orange", "Purple", "Teal",
                                         "Pink", "White"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Select Screen Color";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 10;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            LOG_INFO("Setting color to system default or defined variant");
            // Insert unset protobuf code here
        } else if (selected == 2) {
            LOG_INFO("Setting color to Meshtastic Green");
            TFT_MESH = COLOR565(0x67, 0xEA, 0x94);
        } else if (selected == 3) {
            LOG_INFO("Setting color to Yellow");
            TFT_MESH = COLOR565(255, 255, 102);
        } else if (selected == 4) {
            LOG_INFO("Setting color to Red");
            TFT_MESH = COLOR565(255, 64, 64);
        } else if (selected == 5) {
            LOG_INFO("Setting color to Orange");
            TFT_MESH = COLOR565(255, 160, 20);
        } else if (selected == 6) {
            LOG_INFO("Setting color to Purple");
            TFT_MESH = COLOR565(204, 153, 255);
        } else if (selected == 7) {
            LOG_INFO("Setting color to Teal");
            TFT_MESH = COLOR565(64, 224, 208);
        } else if (selected == 8) {
            LOG_INFO("Setting color to Pink");
            TFT_MESH = COLOR565(255, 105, 180);
        } else if (selected == 9) {
            LOG_INFO("Setting color to White");
            TFT_MESH = COLOR565(255, 255, 255);
        }

#if defined(HELTEC_MESH_NODE_T114) || defined(HELTEC_VISION_MASTER_T190)
        if (selected != 0) {

            static_cast<ST7789Spi *>(screen->getDisplayDevice())->setRGB(TFT_MESH);
            screen->setFrames(graphics::Screen::FOCUS_SYSTEM);
            // I think we need a saveToDisk to commit a protobuf change?
            // There isn't a protobuf for this setting yet, so no save
            // nodeDB->saveToDisk(SEGMENT_CONFIG);
        }
#endif
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::rebootMenu()
{
    static const char *optionsArray[] = {"Back", "Confirm"};
    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Reboot Device?";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsCount = 2;
    bannerOptions.bannerCallback = [](int selected) -> void {
        if (selected == 1) {
            IF_SCREEN(screen->showSimpleBanner("Rebooting...", 0));
            nodeDB->saveToDisk();
            rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::addFavoriteMenu()
{
    screen->showNodePicker("Node To Favorite", 30000, [](int nodenum) -> void {
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
            nodeDB->set_favorite(false, graphics::UIRenderer::currentFavoriteNodeNum);
            screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void menuHandler::handleMenuSwitch()
{
    switch (menuQueue) {
    case menu_none:
        break;
    case lora_picker:
        LoraRegionPicker();
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
    case position_base_menu:
        positionBaseMenu();
        break;
    case gps_toggle_menu:
        GPSToggleMenu();
        break;
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
        TFTColorPickerMenu();
        break;
    case reboot_menu:
        rebootMenu();
        break;
    case add_favorite:
        addFavoriteMenu();
        break;
    case remove_favorite:
        removeFavoriteMenu();
        break;
    }
    menuQueue = menu_none;
}

} // namespace graphics

#endif