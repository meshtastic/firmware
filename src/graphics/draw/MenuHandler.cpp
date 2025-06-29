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
    screen->showOverlayBanner(
        "Set the LoRa region", duration, optionsArray, 23,
        [](int selected) -> void {
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
        },
        0);
}

void menuHandler::TwelveHourPicker()
{
    static const char *optionsArray[] = {"Back", "12-hour", "24-hour"};
    screen->showOverlayBanner("Time Format", 30000, optionsArray, 3, [](int selected) -> void {
        if (selected == 0) {
            menuHandler::menuQueue = menuHandler::clock_menu;
        } else if (selected == 1) {
            config.display.use_12h_clock = true;
        } else {
            config.display.use_12h_clock = false;
        }
        service->reloadConfig(SEGMENT_CONFIG);
    });
}

void menuHandler::ClockFacePicker()
{
    static const char *optionsArray[] = {"Back", "Digital", "Analog"};
    screen->showOverlayBanner("Which Face?", 30000, optionsArray, 3, [](int selected) -> void {
        if (selected == 0) {
            menuHandler::menuQueue = menuHandler::clock_menu;
        } else if (selected == 1) {
            graphics::ClockRenderer::digitalWatchFace = true;
            screen->setFrames(Screen::FOCUS_CLOCK);
        } else {
            graphics::ClockRenderer::digitalWatchFace = false;
            screen->setFrames(Screen::FOCUS_CLOCK);
        }
    });
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
    screen->showOverlayBanner("Pick Timezone", 30000, optionsArray, 17, [](int selected) -> void {
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
    });
}

void menuHandler::clockMenu()
{
    static const char *optionsArray[] = {"Back", "Clock Face", "Time Format", "Timezone"};
    screen->showOverlayBanner("Clock Action", 30000, optionsArray, 4, [](int selected) -> void {
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
    });
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
    screen->showOverlayBanner("Message Action", 30000, optionsArrayPtr, options, [](int selected) -> void {
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
    });
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
    screen->showOverlayBanner("Home Action", 30000, optionsArrayPtr, options, [](int selected) -> void {
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
            InputEvent event = {.inputEvent = (input_broker_event)175, .kbchar = 175, .touchX = 0, .touchY = 0};
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
    });
}

void menuHandler::systemBaseMenu()
{
    int options;
    static const char **optionsArrayPtr;
#if HAS_TFT
    static const char *optionsArray[] = {"Back", "Beeps Action", "Switch to MUI"};
    options = 3;
#endif
#ifdef HELTEC_MESH_NODE_T114
    static const char *optionsArray[] = {"Back", "Beeps Action", "Screen Color"};
    options = 3;
#endif
    optionsArrayPtr = optionsArray;
    screen->showOverlayBanner("System Action", 30000, optionsArrayPtr, options, [](int selected) -> void {
        if (selected == 1) {
            menuHandler::menuQueue = menuHandler::buzzermodemenupicker;
            screen->setInterval(0);
            runASAP = true;
        } else if (selected == 2) {
#if HAS_TFT
            menuHandler::menuQueue = menuHandler::mui_picker;
#endif
#ifdef HELTEC_MESH_NODE_T114
            menuHandler::menuQueue = menuHandler::tftcolormenupicker;
#endif
            screen->setInterval(0);
            runASAP = true;
        }
    });
}

void menuHandler::favoriteBaseMenu()
{
    int options;
    static const char **optionsArrayPtr;

    if (kb_found) {
        static const char *optionsArray[] = {"Back", "New Preset Msg", "New Freetext Msg"};
        optionsArrayPtr = optionsArray;
        options = 3;
    } else {
        static const char *optionsArray[] = {"Back", "New Preset Msg"};
        optionsArrayPtr = optionsArray;
        options = 2;
    }
    screen->showOverlayBanner("Favorites Action", 30000, optionsArrayPtr, options, [](int selected) -> void {
        if (selected == 1) {
            cannedMessageModule->LaunchWithDestination(graphics::UIRenderer::currentFavoriteNodeNum);
        } else if (selected == 2) {
            cannedMessageModule->LaunchFreetextWithDestination(graphics::UIRenderer::currentFavoriteNodeNum);
        }
    });
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
    screen->showOverlayBanner("Position Action", 30000, optionsArrayPtr, options, [](int selected) -> void {
        if (selected == 1) {
            menuQueue = gps_toggle_menu;
        } else if (selected == 2) {
            menuQueue = compass_point_north_menu;
        } else if (selected == 3) {
            accelerometerThread->calibrate(30);
        }
    });
}

void menuHandler::nodeListMenu()
{
    static const char *optionsArray[] = {"Back", "Reset NodeDB"};
    screen->showOverlayBanner("Node Action", 30000, optionsArray, 2, [](int selected) -> void {
        if (selected == 1) {
            menuQueue = reset_node_db_menu;
        }
    });
}

void menuHandler::resetNodeDBMenu()
{
    static const char *optionsArray[] = {"Back", "Confirm"};
    screen->showOverlayBanner("Confirm Reset NodeDB", 30000, optionsArray, 2, [](int selected) -> void {
        if (selected == 1) {
            disableBluetooth();
            LOG_INFO("Initiate node-db reset");
            nodeDB->resetNodes();
            rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        }
    });
}

void menuHandler::compassNorthMenu()
{
    static const char *optionsArray[] = {"Back", "Dynamic", "Fixed Ring", "Freeze Heading"};
    screen->showOverlayBanner("North Directions?", 30000, optionsArray, 4, [](int selected) -> void {
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
    });
}

void menuHandler::GPSToggleMenu()
{
    static const char *optionsArray[] = {"Back", "Enabled", "Disabled"};
    screen->showOverlayBanner(
        "Toggle GPS", 30000, optionsArray, 3,
        [](int selected) -> void {
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
        },
        config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED ? 1 : 2); // set inital selection
}

void menuHandler::BuzzerModeMenu()
{
    static const char *optionsArray[] = {"All Enabled", "Disabled", "Notifications", "System Only"};
    screen->showOverlayBanner(
        "Beep Action", 30000, optionsArray, 4,
        [](int selected) -> void {
            config.device.buzzer_mode = (meshtastic_Config_DeviceConfig_BuzzerMode)selected;
            service->reloadConfig(SEGMENT_CONFIG);
        },
        config.device.buzzer_mode);
}

void menuHandler::switchToMUIMenu()
{
    static const char *optionsArray[] = {"Yes", "No"};
    screen->showOverlayBanner("Switch to MUI?", 30000, optionsArray, 2, [](int selected) -> void {
        if (selected == 0) {
            config.display.displaymode = meshtastic_Config_DisplayConfig_DisplayMode_COLOR;
            config.bluetooth.enabled = false;
            service->reloadConfig(SEGMENT_CONFIG);
            rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        }
    });
}

void menuHandler::TFTColorPickerMenu()
{
    static const char *optionsArray[] = {"Back", "Default", "Meshtastic Green", "Red", "Orange", "Purple", "Teal"};
    screen->showOverlayBanner("Current Screen Color?", 30000, optionsArray, 7, [](int selected) -> void {
        // auto *tft = static_cast<ST7789Spi *>(dispdev);
        if (selected == 1) {
            LOG_INFO("Setting color to soft yellow");
            TFT_MESH = COLOR565(255, 255, 128);
        } else if (selected == 2) {
            LOG_INFO("Setting color to Meshtastic Green");
            TFT_MESH = COLOR565(0x67, 0xEA, 0x94);
        } else if (selected == 3) {
            LOG_INFO("Setting color to Red");
            TFT_MESH = COLOR565(255, 64, 64);
        } else if (selected == 4) {
            LOG_INFO("Setting color to orange");
            TFT_MESH = COLOR565(255, 165, 0);
        } else if (selected == 5) {
            LOG_INFO("Setting color to purple");
            TFT_MESH = COLOR565(192, 128, 192);
        } else if (selected == 6) {
            LOG_INFO("Setting color to teal");
            TFT_MESH = COLOR565(64, 224, 208);
        }

        if (selected != 0) {
            static_cast<ST7789Spi *>(screen->getDisplayDevice())->setRGB(TFT_MESH);
            screen->setFrames(graphics::Screen::FOCUS_SYSTEM);
        }
    });
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
    }
    menuQueue = menu_none;
}

} // namespace graphics

#endif