#include "configuration.h"
#if HAS_SCREEN
#include "MenuHandler.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "graphics/Screen.h"
#include "main.h"
#include "modules/CannedMessageModule.h"

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
    screen->showOverlayBanner("12/24 display?", 30000, optionsArray, 3, [](int selected) -> void {
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

void menuHandler::TZPicker()
{
    static const char *optionsArray[] = {"Back",
                                         "US/Hawaii",
                                         "US/Alaska",
                                         "US/Pacific",
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
        } else if (selected == 4) { // Mountain
            strncpy(config.device.tzdef, "MST7MDT,M3.2.0,M11.1.0", sizeof(config.device.tzdef));
        } else if (selected == 5) { // Central
            strncpy(config.device.tzdef, "CST6CDT,M3.2.0,M11.1.0", sizeof(config.device.tzdef));
        } else if (selected == 6) { // Eastern
            strncpy(config.device.tzdef, "EST5EDT,M3.2.0,M11.1.0", sizeof(config.device.tzdef));
        } else if (selected == 7) { // UTC
            strncpy(config.device.tzdef, "UTC", sizeof(config.device.tzdef));
        } else if (selected == 8) { // EU/Western
            strncpy(config.device.tzdef, "GMT0BST,M3.5.0/1,M10.5.0", sizeof(config.device.tzdef));
        } else if (selected == 9) { // EU/Central
            strncpy(config.device.tzdef, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(config.device.tzdef));
        } else if (selected == 10) { // EU/Eastern
            strncpy(config.device.tzdef, "EET-2EEST,M3.5.0/3,M10.5.0/4", sizeof(config.device.tzdef));
        } else if (selected == 11) { // Asia/Kolkata
            strncpy(config.device.tzdef, "IST-5:30", sizeof(config.device.tzdef));
        } else if (selected == 12) { // China
            strncpy(config.device.tzdef, "HKT-8", sizeof(config.device.tzdef));
        } else if (selected == 13) { // AU/AWST
            strncpy(config.device.tzdef, "AWST-8", sizeof(config.device.tzdef));
        } else if (selected == 14) { // AU/ACST
            strncpy(config.device.tzdef, "ACST-9:30ACDT,M10.1.0,M4.1.0/3", sizeof(config.device.tzdef));
        } else if (selected == 15) { // AU/AEST
            strncpy(config.device.tzdef, "AEST-10AEDT,M10.1.0,M4.1.0/3", sizeof(config.device.tzdef));
        } else if (selected == 16) { // NZ
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
    static const char *optionsArray[] = {"Back", "12-hour", "Timezone"};
    screen->showOverlayBanner("Clock Menu", 30000, optionsArray, 3, [](int selected) -> void {
        if (selected == 1) {
            menuHandler::menuQueue = menuHandler::twelve_hour_picker;
            screen->setInterval(0);
            runASAP = true;
        } else if (selected == 2) {
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
        const char *optionsArray[] = {"Back", "Dismiss", "Reply via Preset", "Reply via Freetext"};
        optionsArrayPtr = optionsArray;
        options = 4;
    } else {
        const char *optionsArray[] = {"Back", "Dismiss", "Reply via Preset"};
        optionsArrayPtr = optionsArray;
        options = 3;
    }
#ifdef HAS_I2S
    const char *optionsArray[] = {"Back", "Dismiss", "Reply via Preset", "Reply via Freetext", "Read Aloud"};
    optionsArrayPtr = optionsArray;
    options = 5;
#endif
    screen->showOverlayBanner("Message Action?", 30000, optionsArrayPtr, options, [](int selected) -> void {
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
    case clock_menu:
        clockMenu();
        break;
    }
    menuQueue = menu_none;
}

} // namespace graphics

#endif