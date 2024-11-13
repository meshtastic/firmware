#include "AdminModule.h"
#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "meshUtils.h"
#include <FSCommon.h>
#if defined(ARCH_ESP32) && !MESHTASTIC_EXCLUDE_BLUETOOTH
#include "BleOta.h"
#endif
#include "Router.h"
#include "configuration.h"
#include "main.h"
#ifdef ARCH_NRF52
#include "main.h"
#endif
#ifdef ARCH_PORTDUINO
#include "unistd.h"
#endif
#include "../userPrefs.h"
#include "Default.h"
#include "TypeConversions.h"

#if !MESHTASTIC_EXCLUDE_MQTT
#include "mqtt/MQTT.h"
#endif

#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif

#if MESHTASTIC_EXCLUDE_GPS
#include "modules/PositionModule.h"
#endif

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C
#include "motion/AccelerometerThread.h"
#endif

AdminModule *adminModule;
bool hasOpenEditTransaction;

/// A special reserved string to indicate strings we can not share with external nodes.  We will use this 'reserved' word instead.
/// Also, to make setting work correctly, if someone tries to set a string to this reserved value we assume they don't really want
/// a change.
static const char *secretReserved = "sekrit";

/// If buf is the reserved secret word, replace the buffer with currentVal
static void writeSecret(char *buf, size_t bufsz, const char *currentVal)
{
    if (strcmp(buf, secretReserved) == 0) {
        strncpy(buf, currentVal, bufsz);
    }
}

/**
 * @brief Handle received protobuf message
 *
 * @param mp Received MeshPacket
 * @param r Decoded AdminMessage
 * @return bool
 */
bool AdminModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *r)
{
    // if handled == false, then let others look at this message also if they want
    bool handled = false;
    assert(r);
    bool fromOthers = !isFromUs(&mp);
    if (mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        return handled;
    }
    meshtastic_Channel *ch = &channels.getByIndex(mp.channel);
    // Could tighten this up further by tracking the last public_key we went an AdminMessage request to
    // and only allowing responses from that remote.
    if (messageIsResponse(r)) {
        LOG_DEBUG("Allow admin response message");
    } else if (mp.from == 0) {
        if (config.security.is_managed) {
            LOG_INFO("Ignore local admin payload because is_managed");
            return handled;
        }
    } else if (strcasecmp(ch->settings.name, Channels::adminChannel) == 0) {
        if (!config.security.admin_channel_enabled) {
            LOG_INFO("Ignore admin channel, legacy admin is disabled");
            myReply = allocErrorResponse(meshtastic_Routing_Error_NOT_AUTHORIZED, &mp);
            return handled;
        }
    } else if (mp.pki_encrypted) {
        if ((config.security.admin_key[0].size == 32 &&
             memcmp(mp.public_key.bytes, config.security.admin_key[0].bytes, 32) == 0) ||
            (config.security.admin_key[1].size == 32 &&
             memcmp(mp.public_key.bytes, config.security.admin_key[1].bytes, 32) == 0) ||
            (config.security.admin_key[2].size == 32 &&
             memcmp(mp.public_key.bytes, config.security.admin_key[2].bytes, 32) == 0)) {
            LOG_INFO("PKC admin payload with authorized sender key");
        } else {
            myReply = allocErrorResponse(meshtastic_Routing_Error_ADMIN_PUBLIC_KEY_UNAUTHORIZED, &mp);
            LOG_INFO("Received PKC admin payload, but the sender public key does not match the admin authorized key!");
            return handled;
        }
    } else {
        LOG_INFO("Ignore unauthorized admin payload %i", r->which_payload_variant);
        myReply = allocErrorResponse(meshtastic_Routing_Error_NOT_AUTHORIZED, &mp);
        return handled;
    }

    LOG_INFO("Handle admin payload %i", r->which_payload_variant);

    // all of the get and set messages, including those for other modules, flow through here first.
    // any message that changes state, we want to check the passkey for
    if (mp.from != 0 && !messageIsRequest(r) && !messageIsResponse(r)) {
        if (!checkPassKey(r)) {
            LOG_WARN("Admin message without session_key!");
            myReply = allocErrorResponse(meshtastic_Routing_Error_ADMIN_BAD_SESSION_KEY, &mp);
            return handled;
        }
    }
    switch (r->which_payload_variant) {

    /**
     * Getters
     */
    case meshtastic_AdminMessage_get_owner_request_tag:
        LOG_INFO("Client got owner");
        handleGetOwner(mp);
        break;

    case meshtastic_AdminMessage_get_config_request_tag:
        LOG_INFO("Client got config");
        handleGetConfig(mp, r->get_config_request);
        break;

    case meshtastic_AdminMessage_get_module_config_request_tag:
        LOG_INFO("Client got module config");
        handleGetModuleConfig(mp, r->get_module_config_request);
        break;

    case meshtastic_AdminMessage_get_channel_request_tag: {
        uint32_t i = r->get_channel_request - 1;
        LOG_INFO("Client got channel %u", i);
        if (i >= MAX_NUM_CHANNELS)
            myReply = allocErrorResponse(meshtastic_Routing_Error_BAD_REQUEST, &mp);
        else
            handleGetChannel(mp, i);
        break;
    }

    /**
     * Setters
     */
    case meshtastic_AdminMessage_set_owner_tag:
        LOG_INFO("Client set owner");
        handleSetOwner(r->set_owner);
        break;

    case meshtastic_AdminMessage_set_config_tag:
        LOG_INFO("Client set config");
        handleSetConfig(r->set_config);
        break;

    case meshtastic_AdminMessage_set_module_config_tag:
        LOG_INFO("Client set module config");
        handleSetModuleConfig(r->set_module_config);
        break;

    case meshtastic_AdminMessage_set_channel_tag:
        LOG_INFO("Client set channel %d", r->set_channel.index);
        if (r->set_channel.index < 0 || r->set_channel.index >= (int)MAX_NUM_CHANNELS)
            myReply = allocErrorResponse(meshtastic_Routing_Error_BAD_REQUEST, &mp);
        else
            handleSetChannel(r->set_channel);
        break;
    case meshtastic_AdminMessage_set_ham_mode_tag:
        LOG_INFO("Client set ham mode");
        handleSetHamMode(r->set_ham_mode);
        break;

    /**
     * Other
     */
    case meshtastic_AdminMessage_reboot_seconds_tag: {
        reboot(r->reboot_seconds);
        break;
    }
    case meshtastic_AdminMessage_reboot_ota_seconds_tag: {
        int32_t s = r->reboot_ota_seconds;
#if defined(ARCH_ESP32) && !MESHTASTIC_EXCLUDE_BLUETOOTH
        if (BleOta::getOtaAppVersion().isEmpty()) {
            LOG_INFO("No OTA firmware available, scheduling regular reboot in %d seconds", s);
            screen->startAlert("Rebooting...");
        } else {
            screen->startFirmwareUpdateScreen();
            BleOta::switchToOtaApp();
            LOG_INFO("Reboot to OTA in %d seconds", s);
        }
#else
        LOG_INFO("Not on ESP32, scheduling regular reboot in %d seconds", s);
        screen->startAlert("Rebooting...");
#endif
        rebootAtMsec = (s < 0) ? 0 : (millis() + s * 1000);
        break;
    }
    case meshtastic_AdminMessage_shutdown_seconds_tag: {
        int32_t s = r->shutdown_seconds;
        LOG_INFO("Shutdown in %d seconds", s);
        shutdownAtMsec = (s < 0) ? 0 : (millis() + s * 1000);
        break;
    }
    case meshtastic_AdminMessage_get_device_metadata_request_tag: {
        LOG_INFO("Client got device metadata");
        handleGetDeviceMetadata(mp);
        break;
    }
    case meshtastic_AdminMessage_factory_reset_config_tag: {
        disableBluetooth();
        LOG_INFO("Initiate factory config reset");
        nodeDB->factoryReset();
        LOG_INFO("Factory config reset finished, rebooting soon");
        reboot(DEFAULT_REBOOT_SECONDS);
        break;
    }
    case meshtastic_AdminMessage_factory_reset_device_tag: {
        disableBluetooth();
        LOG_INFO("Initiate full factory reset");
        nodeDB->factoryReset(true);
        reboot(DEFAULT_REBOOT_SECONDS);
        break;
    }
    case meshtastic_AdminMessage_nodedb_reset_tag: {
        disableBluetooth();
        LOG_INFO("Initiate node-db reset");
        nodeDB->resetNodes();
        reboot(DEFAULT_REBOOT_SECONDS);
        break;
    }
    case meshtastic_AdminMessage_begin_edit_settings_tag: {
        LOG_INFO("Begin transaction for editing settings");
        hasOpenEditTransaction = true;
        break;
    }
    case meshtastic_AdminMessage_commit_edit_settings_tag: {
        disableBluetooth();
        LOG_INFO("Commit transaction for edited settings");
        hasOpenEditTransaction = false;
        saveChanges(SEGMENT_CONFIG | SEGMENT_MODULECONFIG | SEGMENT_DEVICESTATE | SEGMENT_CHANNELS);
        break;
    }
    case meshtastic_AdminMessage_get_device_connection_status_request_tag: {
        LOG_INFO("Client got device connection status");
        handleGetDeviceConnectionStatus(mp);
        break;
    }
    case meshtastic_AdminMessage_get_module_config_response_tag: {
        LOG_INFO("Client received a get_module_config response");
        if (fromOthers && r->get_module_config_response.which_payload_variant ==
                              meshtastic_AdminMessage_ModuleConfigType_REMOTEHARDWARE_CONFIG) {
            handleGetModuleConfigResponse(mp, r);
        }
        break;
    }
    case meshtastic_AdminMessage_remove_by_nodenum_tag: {
        LOG_INFO("Client received remove_nodenum command");
        nodeDB->removeNodeByNum(r->remove_by_nodenum);
        this->notifyObservers(r); // Observed by screen
        break;
    }
    case meshtastic_AdminMessage_set_favorite_node_tag: {
        LOG_INFO("Client received set_favorite_node command");
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(r->set_favorite_node);
        if (node != NULL) {
            node->is_favorite = true;
            saveChanges(SEGMENT_DEVICESTATE, false);
        }
        break;
    }
    case meshtastic_AdminMessage_remove_favorite_node_tag: {
        LOG_INFO("Client received remove_favorite_node command");
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(r->remove_favorite_node);
        if (node != NULL) {
            node->is_favorite = false;
            saveChanges(SEGMENT_DEVICESTATE, false);
        }
        break;
    }
    case meshtastic_AdminMessage_set_ignored_node_tag: {
        LOG_INFO("Client received set_ignored_node command");
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(r->set_ignored_node);
        if (node != NULL) {
            node->is_ignored = true;
            node->has_device_metrics = false;
            node->has_position = false;
            node->user.public_key.size = 0;
            node->user.public_key.bytes[0] = 0;
            saveChanges(SEGMENT_DEVICESTATE, false);
        }
        break;
    }
    case meshtastic_AdminMessage_remove_ignored_node_tag: {
        LOG_INFO("Client received remove_ignored_node command");
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(r->remove_ignored_node);
        if (node != NULL) {
            node->is_ignored = false;
            saveChanges(SEGMENT_DEVICESTATE, false);
        }
        break;
    }
    case meshtastic_AdminMessage_set_fixed_position_tag: {
        LOG_INFO("Client received set_fixed_position command");
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeDB->getNodeNum());
        node->has_position = true;
        node->position = TypeConversions::ConvertToPositionLite(r->set_fixed_position);
        nodeDB->setLocalPosition(r->set_fixed_position);
        config.position.fixed_position = true;
        saveChanges(SEGMENT_DEVICESTATE | SEGMENT_CONFIG, false);
#if !MESHTASTIC_EXCLUDE_GPS
        if (gps != nullptr)
            gps->enable();
        // Send our new fixed position to the mesh for good measure
        positionModule->sendOurPosition();
#endif
        break;
    }
    case meshtastic_AdminMessage_remove_fixed_position_tag: {
        LOG_INFO("Client received remove_fixed_position command");
        nodeDB->clearLocalPosition();
        config.position.fixed_position = false;
        saveChanges(SEGMENT_DEVICESTATE | SEGMENT_CONFIG, false);
        break;
    }
    case meshtastic_AdminMessage_set_time_only_tag: {
        LOG_INFO("Client received set_time_only command");
        struct timeval tv;
        tv.tv_sec = r->set_time_only;
        tv.tv_usec = 0;

        perhapsSetRTC(RTCQualityNTP, &tv, false);
        break;
    }
    case meshtastic_AdminMessage_enter_dfu_mode_request_tag: {
        LOG_INFO("Client requesting to enter DFU mode");
#if defined(ARCH_NRF52) || defined(ARCH_RP2040)
        enterDfuMode();
#endif
        break;
    }
    case meshtastic_AdminMessage_delete_file_request_tag: {
        LOG_DEBUG("Client requesting to delete file: %s", r->delete_file_request);
#ifdef FSCom
        if (FSCom.remove(r->delete_file_request)) {
            LOG_DEBUG("Successfully deleted file");
        } else {
            LOG_DEBUG("Failed to delete file");
        }
#endif
        break;
    }
#ifdef ARCH_PORTDUINO
    case meshtastic_AdminMessage_exit_simulator_tag:
        LOG_INFO("Exiting simulator");
        _exit(0);
        break;
#endif

    default:
        meshtastic_AdminMessage res = meshtastic_AdminMessage_init_default;
        AdminMessageHandleResult handleResult = MeshModule::handleAdminMessageForAllModules(mp, r, &res);

        if (handleResult == AdminMessageHandleResult::HANDLED_WITH_RESPONSE) {
            setPassKey(&res);
            myReply = allocDataProtobuf(res);
        } else if (mp.decoded.want_response) {
            LOG_DEBUG("Did not responded to a request that wanted a respond. req.variant=%d", r->which_payload_variant);
        } else if (handleResult != AdminMessageHandleResult::HANDLED) {
            // Probably a message sent by us or sent to our local node.  FIXME, we should avoid scanning these messages
            LOG_INFO("Ignore irrelevant admin %d", r->which_payload_variant);
        }
        break;
    }

    // If asked for a response and it is not yet set, generate an 'ACK' response
    if (mp.decoded.want_response && !myReply) {
        myReply = allocErrorResponse(meshtastic_Routing_Error_NONE, &mp);
    }

    return handled;
}

void AdminModule::handleGetModuleConfigResponse(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *r)
{
    // Skip if it's disabled or no pins are exposed
    if (!r->get_module_config_response.payload_variant.remote_hardware.enabled ||
        r->get_module_config_response.payload_variant.remote_hardware.available_pins_count == 0) {
        LOG_DEBUG("Remote hardware module disabled or no available_pins. Skip");
        return;
    }
    for (uint8_t i = 0; i < devicestate.node_remote_hardware_pins_count; i++) {
        if (devicestate.node_remote_hardware_pins[i].node_num == 0 || !devicestate.node_remote_hardware_pins[i].has_pin) {
            continue;
        }
        for (uint8_t j = 0; j < r->get_module_config_response.payload_variant.remote_hardware.available_pins_count; j++) {
            auto availablePin = r->get_module_config_response.payload_variant.remote_hardware.available_pins[j];
            if (i < devicestate.node_remote_hardware_pins_count) {
                devicestate.node_remote_hardware_pins[i].node_num = mp.from;
                devicestate.node_remote_hardware_pins[i].pin = availablePin;
            }
            i++;
        }
    }
}

/**
 * Setter methods
 */

void AdminModule::handleSetOwner(const meshtastic_User &o)
{
    int changed = 0;

    if (*o.long_name) {
        changed |= strcmp(owner.long_name, o.long_name);
        strncpy(owner.long_name, o.long_name, sizeof(owner.long_name));
    }
    if (*o.short_name) {
        changed |= strcmp(owner.short_name, o.short_name);
        strncpy(owner.short_name, o.short_name, sizeof(owner.short_name));
    }
    if (*o.id) {
        changed |= strcmp(owner.id, o.id);
        strncpy(owner.id, o.id, sizeof(owner.id));
    }
    if (owner.is_licensed != o.is_licensed) {
        changed = 1;
        owner.is_licensed = o.is_licensed;
    }

    if (changed) { // If nothing really changed, don't broadcast on the network or write to flash
        service->reloadOwner(!hasOpenEditTransaction);
        saveChanges(SEGMENT_DEVICESTATE);
    }
}

void AdminModule::handleSetConfig(const meshtastic_Config &c)
{
    auto changes = SEGMENT_CONFIG;
    auto existingRole = config.device.role;
    bool isRegionUnset = (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET);
    bool requiresReboot = true;

    switch (c.which_payload_variant) {
    case meshtastic_Config_device_tag:
        LOG_INFO("Set config: Device");
        config.has_device = true;
#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
        if (config.device.double_tap_as_button_press == false && c.payload_variant.device.double_tap_as_button_press == true &&
            accelerometerThread->enabled == false) {
            config.device.double_tap_as_button_press = c.payload_variant.device.double_tap_as_button_press;
            accelerometerThread->enabled = true;
            accelerometerThread->start();
        }
#endif
#ifdef LED_PIN
        // Turn LED off if heartbeat by config
        if (c.payload_variant.device.led_heartbeat_disabled) {
            digitalWrite(LED_PIN, HIGH ^ LED_STATE_ON);
        }
#endif
        if (config.device.button_gpio == c.payload_variant.device.button_gpio &&
            config.device.buzzer_gpio == c.payload_variant.device.buzzer_gpio &&
            config.device.role == c.payload_variant.device.role &&
            config.device.disable_triple_click == c.payload_variant.device.disable_triple_click &&
            config.device.rebroadcast_mode == c.payload_variant.device.rebroadcast_mode) {
            requiresReboot = false;
        }
        config.device = c.payload_variant.device;
        if (config.device.rebroadcast_mode == meshtastic_Config_DeviceConfig_RebroadcastMode_NONE &&
            IS_ONE_OF(config.device.role, meshtastic_Config_DeviceConfig_Role_ROUTER,
                      meshtastic_Config_DeviceConfig_Role_REPEATER)) {
            config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;
            const char *warning = "Rebroadcast mode can't be set to NONE for a router or repeater\n";
            LOG_WARN(warning);
            sendWarning(warning);
        }
        // If we're setting router role for the first time, install its intervals
        if (existingRole != c.payload_variant.device.role)
            nodeDB->installRoleDefaults(c.payload_variant.device.role);
        if (config.device.node_info_broadcast_secs < min_node_info_broadcast_secs) {
            LOG_DEBUG("Tried to set node_info_broadcast_secs too low, setting to %d", min_node_info_broadcast_secs);
            config.device.node_info_broadcast_secs = min_node_info_broadcast_secs;
        }
        // Router Client is deprecated; Set it to client
        if (c.payload_variant.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT) {
            config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
            if (moduleConfig.store_forward.enabled && !moduleConfig.store_forward.is_server) {
                moduleConfig.store_forward.is_server = true;
                changes |= SEGMENT_MODULECONFIG;
                requiresReboot = true;
            }
        }
#if USERPREFS_EVENT_MODE
        // If we're in event mode, nobody is a Router or Repeater
        if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER ||
            config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER) {
            config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
        }
#endif
        break;
    case meshtastic_Config_position_tag:
        LOG_INFO("Set config: Position");
        config.has_position = true;
        config.position = c.payload_variant.position;
        // Save nodedb as well in case we got a fixed position packet
        saveChanges(SEGMENT_DEVICESTATE, false);
        break;
    case meshtastic_Config_power_tag:
        LOG_INFO("Set config: Power");
        config.has_power = true;
        // Really just the adc override is the only thing that can change without a reboot
        if (config.power.device_battery_ina_address == c.payload_variant.power.device_battery_ina_address &&
            config.power.is_power_saving == c.payload_variant.power.is_power_saving &&
            config.power.ls_secs == c.payload_variant.power.ls_secs &&
            config.power.min_wake_secs == c.payload_variant.power.min_wake_secs &&
            config.power.on_battery_shutdown_after_secs == c.payload_variant.power.on_battery_shutdown_after_secs &&
            config.power.sds_secs == c.payload_variant.power.sds_secs &&
            config.power.wait_bluetooth_secs == c.payload_variant.power.wait_bluetooth_secs) {
            requiresReboot = false;
        }
        config.power = c.payload_variant.power;
        if (c.payload_variant.power.on_battery_shutdown_after_secs > 0 &&
            c.payload_variant.power.on_battery_shutdown_after_secs < 30) {
            LOG_WARN("Tried to set on_battery_shutdown_after_secs too low, set to min 30 seconds");
            config.power.on_battery_shutdown_after_secs = 30;
        }
        break;
    case meshtastic_Config_network_tag:
        LOG_INFO("Set config: WiFi");
        config.has_network = true;
        config.network = c.payload_variant.network;
        break;
    case meshtastic_Config_display_tag:
        LOG_INFO("Set config: Display");
        config.has_display = true;
        if (config.display.screen_on_secs == c.payload_variant.display.screen_on_secs &&
            config.display.flip_screen == c.payload_variant.display.flip_screen &&
            config.display.oled == c.payload_variant.display.oled) {
            requiresReboot = false;
        }
#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
        if (config.display.wake_on_tap_or_motion == false && c.payload_variant.display.wake_on_tap_or_motion == true &&
            accelerometerThread->enabled == false) {
            config.display.wake_on_tap_or_motion = c.payload_variant.display.wake_on_tap_or_motion;
            accelerometerThread->enabled = true;
            accelerometerThread->start();
        }
#endif
        config.display = c.payload_variant.display;
        break;
    case meshtastic_Config_lora_tag:
        LOG_INFO("Set config: LoRa");
        config.has_lora = true;
        // If no lora radio parameters change, don't need to reboot
        if (config.lora.use_preset == c.payload_variant.lora.use_preset && config.lora.region == c.payload_variant.lora.region &&
            config.lora.modem_preset == c.payload_variant.lora.modem_preset &&
            config.lora.bandwidth == c.payload_variant.lora.bandwidth &&
            config.lora.spread_factor == c.payload_variant.lora.spread_factor &&
            config.lora.coding_rate == c.payload_variant.lora.coding_rate &&
            config.lora.tx_power == c.payload_variant.lora.tx_power &&
            config.lora.frequency_offset == c.payload_variant.lora.frequency_offset &&
            config.lora.override_frequency == c.payload_variant.lora.override_frequency &&
            config.lora.channel_num == c.payload_variant.lora.channel_num &&
            config.lora.sx126x_rx_boosted_gain == c.payload_variant.lora.sx126x_rx_boosted_gain) {
            requiresReboot = false;
        }

#ifdef RF95_FAN_EN
        // Turn PA off if disabled by config
        if (c.payload_variant.lora.pa_fan_disabled) {
            digitalWrite(RF95_FAN_EN, LOW ^ 0);
        } else {
            digitalWrite(RF95_FAN_EN, HIGH ^ 0);
        }
#endif
        config.lora = c.payload_variant.lora;
        // If we're setting region for the first time, init the region
        if (isRegionUnset && config.lora.region > meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
            config.lora.tx_enabled = true;
            initRegion();
            if (myRegion->dutyCycle < 100) {
                config.lora.ignore_mqtt = true; // Ignore MQTT by default if region has a duty cycle limit
            }
            if (strcmp(moduleConfig.mqtt.root, default_mqtt_root) == 0) {
                sprintf(moduleConfig.mqtt.root, "%s/%s", default_mqtt_root, myRegion->name);
                changes = SEGMENT_CONFIG | SEGMENT_MODULECONFIG;
            }
        }
        break;
    case meshtastic_Config_bluetooth_tag:
        LOG_INFO("Set config: Bluetooth");
        config.has_bluetooth = true;
        config.bluetooth = c.payload_variant.bluetooth;
        break;
    case meshtastic_Config_security_tag:
        LOG_INFO("Set config: Security");
        config.security = c.payload_variant.security;
#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN) && !(MESHTASTIC_EXCLUDE_PKI)
        // We check for a potentially valid private key, and a blank public key, and regen the public key if needed.
        if (config.security.private_key.size == 32 && !memfll(config.security.private_key.bytes, 0, 32) &&
            (config.security.public_key.size == 0 || memfll(config.security.public_key.bytes, 0, 32))) {
            if (crypto->regeneratePublicKey(config.security.public_key.bytes, config.security.private_key.bytes)) {
                config.security.public_key.size = 32;
            }
        }
#endif
        owner.public_key.size = config.security.public_key.size;
        memcpy(owner.public_key.bytes, config.security.public_key.bytes, config.security.public_key.size);
#if !MESHTASTIC_EXCLUDE_PKI
        crypto->setDHPrivateKey(config.security.private_key.bytes);
#endif
        if (config.security.debug_log_api_enabled == c.payload_variant.security.debug_log_api_enabled &&
            config.security.serial_enabled == c.payload_variant.security.serial_enabled)
            requiresReboot = false;

        break;
    }
    if (requiresReboot && !hasOpenEditTransaction) {
        disableBluetooth();
    }

    saveChanges(changes, requiresReboot);
}

void AdminModule::handleSetModuleConfig(const meshtastic_ModuleConfig &c)
{
    if (!hasOpenEditTransaction)
        disableBluetooth();
    switch (c.which_payload_variant) {
    case meshtastic_ModuleConfig_mqtt_tag:
        LOG_INFO("Set module config: MQTT");
        moduleConfig.has_mqtt = true;
        moduleConfig.mqtt = c.payload_variant.mqtt;
        break;
    case meshtastic_ModuleConfig_serial_tag:
        LOG_INFO("Set module config: Serial");
        moduleConfig.has_serial = true;
        moduleConfig.serial = c.payload_variant.serial;
        break;
    case meshtastic_ModuleConfig_external_notification_tag:
        LOG_INFO("Set module config: External Notification");
        moduleConfig.has_external_notification = true;
        moduleConfig.external_notification = c.payload_variant.external_notification;
        break;
    case meshtastic_ModuleConfig_store_forward_tag:
        LOG_INFO("Set module config: Store & Forward");
        moduleConfig.has_store_forward = true;
        moduleConfig.store_forward = c.payload_variant.store_forward;
        break;
    case meshtastic_ModuleConfig_range_test_tag:
        LOG_INFO("Set module config: Range Test");
        moduleConfig.has_range_test = true;
        moduleConfig.range_test = c.payload_variant.range_test;
        break;
    case meshtastic_ModuleConfig_telemetry_tag:
        LOG_INFO("Set module config: Telemetry");
        moduleConfig.has_telemetry = true;
        moduleConfig.telemetry = c.payload_variant.telemetry;
        break;
    case meshtastic_ModuleConfig_canned_message_tag:
        LOG_INFO("Set module config: Canned Message");
        moduleConfig.has_canned_message = true;
        moduleConfig.canned_message = c.payload_variant.canned_message;
        break;
    case meshtastic_ModuleConfig_audio_tag:
        LOG_INFO("Set module config: Audio");
        moduleConfig.has_audio = true;
        moduleConfig.audio = c.payload_variant.audio;
        break;
    case meshtastic_ModuleConfig_remote_hardware_tag:
        LOG_INFO("Set module config: Remote Hardware");
        moduleConfig.has_remote_hardware = true;
        moduleConfig.remote_hardware = c.payload_variant.remote_hardware;
        break;
    case meshtastic_ModuleConfig_neighbor_info_tag:
        LOG_INFO("Set module config: Neighbor Info");
        moduleConfig.has_neighbor_info = true;
        if (moduleConfig.neighbor_info.update_interval < min_neighbor_info_broadcast_secs) {
            LOG_DEBUG("Tried to set update_interval too low, setting to %d", default_neighbor_info_broadcast_secs);
            moduleConfig.neighbor_info.update_interval = default_neighbor_info_broadcast_secs;
        }
        moduleConfig.neighbor_info = c.payload_variant.neighbor_info;
        break;
    case meshtastic_ModuleConfig_detection_sensor_tag:
        LOG_INFO("Set module config: Detection Sensor");
        moduleConfig.has_detection_sensor = true;
        moduleConfig.detection_sensor = c.payload_variant.detection_sensor;
        break;
    case meshtastic_ModuleConfig_ambient_lighting_tag:
        LOG_INFO("Set module config: Ambient Lighting");
        moduleConfig.has_ambient_lighting = true;
        moduleConfig.ambient_lighting = c.payload_variant.ambient_lighting;
        break;
    case meshtastic_ModuleConfig_paxcounter_tag:
        LOG_INFO("Set module config: Paxcounter");
        moduleConfig.has_paxcounter = true;
        moduleConfig.paxcounter = c.payload_variant.paxcounter;
        break;
    }
    saveChanges(SEGMENT_MODULECONFIG);
}

void AdminModule::handleSetChannel(const meshtastic_Channel &cc)
{
    channels.setChannel(cc);
    channels.onConfigChanged(); // tell the radios about this change
    saveChanges(SEGMENT_CHANNELS, false);
}

/**
 * Getters
 */

void AdminModule::handleGetOwner(const meshtastic_MeshPacket &req)
{
    if (req.decoded.want_response) {
        // We create the reply here
        meshtastic_AdminMessage res = meshtastic_AdminMessage_init_default;
        res.get_owner_response = owner;

        res.which_payload_variant = meshtastic_AdminMessage_get_owner_response_tag;
        setPassKey(&res);
        myReply = allocDataProtobuf(res);
    }
}

void AdminModule::handleGetConfig(const meshtastic_MeshPacket &req, const uint32_t configType)
{
    meshtastic_AdminMessage res = meshtastic_AdminMessage_init_default;

    if (req.decoded.want_response) {
        switch (configType) {
        case meshtastic_AdminMessage_ConfigType_DEVICE_CONFIG:
            LOG_INFO("Get config: Device");
            res.get_config_response.which_payload_variant = meshtastic_Config_device_tag;
            res.get_config_response.payload_variant.device = config.device;
            break;
        case meshtastic_AdminMessage_ConfigType_POSITION_CONFIG:
            LOG_INFO("Get config: Position");
            res.get_config_response.which_payload_variant = meshtastic_Config_position_tag;
            res.get_config_response.payload_variant.position = config.position;
            break;
        case meshtastic_AdminMessage_ConfigType_POWER_CONFIG:
            LOG_INFO("Get config: Power");
            res.get_config_response.which_payload_variant = meshtastic_Config_power_tag;
            res.get_config_response.payload_variant.power = config.power;
            break;
        case meshtastic_AdminMessage_ConfigType_NETWORK_CONFIG:
            LOG_INFO("Get config: Network");
            res.get_config_response.which_payload_variant = meshtastic_Config_network_tag;
            res.get_config_response.payload_variant.network = config.network;
            writeSecret(res.get_config_response.payload_variant.network.wifi_psk,
                        sizeof(res.get_config_response.payload_variant.network.wifi_psk), config.network.wifi_psk);
            break;
        case meshtastic_AdminMessage_ConfigType_DISPLAY_CONFIG:
            LOG_INFO("Get config: Display");
            res.get_config_response.which_payload_variant = meshtastic_Config_display_tag;
            res.get_config_response.payload_variant.display = config.display;
            break;
        case meshtastic_AdminMessage_ConfigType_LORA_CONFIG:
            LOG_INFO("Get config: LoRa");
            res.get_config_response.which_payload_variant = meshtastic_Config_lora_tag;
            res.get_config_response.payload_variant.lora = config.lora;
            break;
        case meshtastic_AdminMessage_ConfigType_BLUETOOTH_CONFIG:
            LOG_INFO("Get config: Bluetooth");
            res.get_config_response.which_payload_variant = meshtastic_Config_bluetooth_tag;
            res.get_config_response.payload_variant.bluetooth = config.bluetooth;
            break;
        case meshtastic_AdminMessage_ConfigType_SECURITY_CONFIG:
            LOG_INFO("Get config: Security");
            res.get_config_response.which_payload_variant = meshtastic_Config_security_tag;
            res.get_config_response.payload_variant.security = config.security;
            break;
        case meshtastic_AdminMessage_ConfigType_SESSIONKEY_CONFIG:
            LOG_INFO("Get config: Sessionkey");
            res.get_config_response.which_payload_variant = meshtastic_Config_sessionkey_tag;
            break;
        }
        // NOTE: The phone app needs to know the ls_secs value so it can properly expect sleep behavior.
        // So even if we internally use 0 to represent 'use default' we still need to send the value we are
        // using to the app (so that even old phone apps work with new device loads).
        // r.get_radio_response.preferences.ls_secs = getPref_ls_secs();
        // hideSecret(r.get_radio_response.preferences.wifi_ssid); // hmm - leave public for now, because only minimally private
        // and useful for users to know current provisioning) hideSecret(r.get_radio_response.preferences.wifi_password);
        // r.get_config_response.which_payloadVariant = Config_ModuleConfig_telemetry_tag;
        res.which_payload_variant = meshtastic_AdminMessage_get_config_response_tag;
        setPassKey(&res);
        myReply = allocDataProtobuf(res);
    }
}

void AdminModule::handleGetModuleConfig(const meshtastic_MeshPacket &req, const uint32_t configType)
{
    meshtastic_AdminMessage res = meshtastic_AdminMessage_init_default;

    if (req.decoded.want_response) {
        switch (configType) {
        case meshtastic_AdminMessage_ModuleConfigType_MQTT_CONFIG:
            LOG_INFO("Get module config: MQTT");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_mqtt_tag;
            res.get_module_config_response.payload_variant.mqtt = moduleConfig.mqtt;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_SERIAL_CONFIG:
            LOG_INFO("Get module config: Serial");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_serial_tag;
            res.get_module_config_response.payload_variant.serial = moduleConfig.serial;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_EXTNOTIF_CONFIG:
            LOG_INFO("Get module config: External Notification");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_external_notification_tag;
            res.get_module_config_response.payload_variant.external_notification = moduleConfig.external_notification;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_STOREFORWARD_CONFIG:
            LOG_INFO("Get module config: Store & Forward");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_store_forward_tag;
            res.get_module_config_response.payload_variant.store_forward = moduleConfig.store_forward;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_RANGETEST_CONFIG:
            LOG_INFO("Get module config: Range Test");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_range_test_tag;
            res.get_module_config_response.payload_variant.range_test = moduleConfig.range_test;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_TELEMETRY_CONFIG:
            LOG_INFO("Get module config: Telemetry");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_telemetry_tag;
            res.get_module_config_response.payload_variant.telemetry = moduleConfig.telemetry;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_CANNEDMSG_CONFIG:
            LOG_INFO("Get module config: Canned Message");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_canned_message_tag;
            res.get_module_config_response.payload_variant.canned_message = moduleConfig.canned_message;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_AUDIO_CONFIG:
            LOG_INFO("Get module config: Audio");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_audio_tag;
            res.get_module_config_response.payload_variant.audio = moduleConfig.audio;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_REMOTEHARDWARE_CONFIG:
            LOG_INFO("Get module config: Remote Hardware");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_remote_hardware_tag;
            res.get_module_config_response.payload_variant.remote_hardware = moduleConfig.remote_hardware;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_NEIGHBORINFO_CONFIG:
            LOG_INFO("Get module config: Neighbor Info");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_neighbor_info_tag;
            res.get_module_config_response.payload_variant.neighbor_info = moduleConfig.neighbor_info;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_DETECTIONSENSOR_CONFIG:
            LOG_INFO("Get module config: Detection Sensor");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_detection_sensor_tag;
            res.get_module_config_response.payload_variant.detection_sensor = moduleConfig.detection_sensor;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_AMBIENTLIGHTING_CONFIG:
            LOG_INFO("Get module config: Ambient Lighting");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_ambient_lighting_tag;
            res.get_module_config_response.payload_variant.ambient_lighting = moduleConfig.ambient_lighting;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_PAXCOUNTER_CONFIG:
            LOG_INFO("Get module config: Paxcounter");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_paxcounter_tag;
            res.get_module_config_response.payload_variant.paxcounter = moduleConfig.paxcounter;
            break;
        }

        // NOTE: The phone app needs to know the ls_secsvalue so it can properly expect sleep behavior.
        // So even if we internally use 0 to represent 'use default' we still need to send the value we are
        // using to the app (so that even old phone apps work with new device loads).
        // r.get_radio_response.preferences.ls_secs = getPref_ls_secs();
        // hideSecret(r.get_radio_response.preferences.wifi_ssid); // hmm - leave public for now, because only minimally private
        // and useful for users to know current provisioning) hideSecret(r.get_radio_response.preferences.wifi_password);
        // r.get_config_response.which_payloadVariant = Config_ModuleConfig_telemetry_tag;
        res.which_payload_variant = meshtastic_AdminMessage_get_module_config_response_tag;
        setPassKey(&res);
        myReply = allocDataProtobuf(res);
    }
}

void AdminModule::handleGetNodeRemoteHardwarePins(const meshtastic_MeshPacket &req)
{
    meshtastic_AdminMessage r = meshtastic_AdminMessage_init_default;
    r.which_payload_variant = meshtastic_AdminMessage_get_node_remote_hardware_pins_response_tag;
    for (uint8_t i = 0; i < devicestate.node_remote_hardware_pins_count; i++) {
        if (devicestate.node_remote_hardware_pins[i].node_num == 0 || !devicestate.node_remote_hardware_pins[i].has_pin) {
            continue;
        }
        r.get_node_remote_hardware_pins_response.node_remote_hardware_pins[i] = devicestate.node_remote_hardware_pins[i];
    }
    for (uint8_t i = 0; i < moduleConfig.remote_hardware.available_pins_count; i++) {
        if (!moduleConfig.remote_hardware.available_pins[i].gpio_pin) {
            continue;
        }
        meshtastic_NodeRemoteHardwarePin nodePin = meshtastic_NodeRemoteHardwarePin_init_default;
        nodePin.node_num = nodeDB->getNodeNum();
        nodePin.pin = moduleConfig.remote_hardware.available_pins[i];
        r.get_node_remote_hardware_pins_response.node_remote_hardware_pins[i + 12] = nodePin;
    }
    setPassKey(&r);
    myReply = allocDataProtobuf(r);
}

void AdminModule::handleGetDeviceMetadata(const meshtastic_MeshPacket &req)
{
    meshtastic_AdminMessage r = meshtastic_AdminMessage_init_default;
    r.get_device_metadata_response = getDeviceMetadata();
    r.which_payload_variant = meshtastic_AdminMessage_get_device_metadata_response_tag;
    setPassKey(&r);
    myReply = allocDataProtobuf(r);
}

void AdminModule::handleGetDeviceConnectionStatus(const meshtastic_MeshPacket &req)
{
    meshtastic_AdminMessage r = meshtastic_AdminMessage_init_default;

    meshtastic_DeviceConnectionStatus conn = meshtastic_DeviceConnectionStatus_init_zero;

#if HAS_WIFI
    conn.has_wifi = true;
    conn.wifi.has_status = true;
#ifdef ARCH_PORTDUINO
    conn.wifi.status.is_connected = true;
#else
    conn.wifi.status.is_connected = WiFi.status() == WL_CONNECTED;
#endif
    strncpy(conn.wifi.ssid, config.network.wifi_ssid, 33);
    if (conn.wifi.status.is_connected) {
        conn.wifi.rssi = WiFi.RSSI();
        conn.wifi.status.ip_address = WiFi.localIP();
#ifndef MESHTASTIC_EXCLUDE_MQTT
        conn.wifi.status.is_mqtt_connected = mqtt && mqtt->isConnectedDirectly();
#endif
        conn.wifi.status.is_syslog_connected = false; // FIXME wire this up
    }
#endif

#if HAS_ETHERNET
    conn.has_ethernet = true;
    conn.ethernet.has_status = true;
    if (Ethernet.linkStatus() == LinkON) {
        conn.ethernet.status.is_connected = true;
        conn.ethernet.status.ip_address = Ethernet.localIP();
#if !MESHTASTIC_EXCLUDE_MQTT
        conn.ethernet.status.is_mqtt_connected = mqtt && mqtt->isConnectedDirectly();
#endif
        conn.ethernet.status.is_syslog_connected = false; // FIXME wire this up
    } else {
        conn.ethernet.status.is_connected = false;
    }
#endif

#if HAS_BLUETOOTH
    conn.has_bluetooth = true;
    conn.bluetooth.pin = config.bluetooth.fixed_pin;
#ifdef ARCH_ESP32
    if (config.bluetooth.enabled && nimbleBluetooth) {
        conn.bluetooth.is_connected = nimbleBluetooth->isConnected();
        conn.bluetooth.rssi = nimbleBluetooth->getRssi();
    }
#elif defined(ARCH_NRF52)
    if (config.bluetooth.enabled && nrf52Bluetooth) {
        conn.bluetooth.is_connected = nrf52Bluetooth->isConnected();
    }
#endif
#endif
    conn.has_serial = true; // No serial-less devices
#if !EXCLUDE_POWER_FSM
    conn.serial.is_connected = powerFSM.getState() == &stateSERIAL;
#else
    conn.serial.is_connected = powerFSM.getState();
#endif
    conn.serial.baud = SERIAL_BAUD;

    r.get_device_connection_status_response = conn;
    r.which_payload_variant = meshtastic_AdminMessage_get_device_connection_status_response_tag;
    setPassKey(&r);
    myReply = allocDataProtobuf(r);
}

void AdminModule::handleGetChannel(const meshtastic_MeshPacket &req, uint32_t channelIndex)
{
    if (req.decoded.want_response) {
        // We create the reply here
        meshtastic_AdminMessage r = meshtastic_AdminMessage_init_default;
        r.get_channel_response = channels.getByIndex(channelIndex);
        r.which_payload_variant = meshtastic_AdminMessage_get_channel_response_tag;
        setPassKey(&r);
        myReply = allocDataProtobuf(r);
    }
}

void AdminModule::reboot(int32_t seconds)
{
    LOG_INFO("Reboot in %d seconds", seconds);
    screen->startAlert("Rebooting...");
    rebootAtMsec = (seconds < 0) ? 0 : (millis() + seconds * 1000);
}

void AdminModule::saveChanges(int saveWhat, bool shouldReboot)
{
    if (!hasOpenEditTransaction) {
        LOG_INFO("Save changes to disk");
        service->reloadConfig(saveWhat); // Calls saveToDisk among other things
    } else {
        LOG_INFO("Delay save of changes to disk until the open transaction is committed");
    }
    if (shouldReboot && !hasOpenEditTransaction) {
        reboot(DEFAULT_REBOOT_SECONDS);
    }
}

void AdminModule::handleSetHamMode(const meshtastic_HamParameters &p)
{
    // Set call sign and override lora limitations for licensed use
    strncpy(owner.long_name, p.call_sign, sizeof(owner.long_name));
    strncpy(owner.short_name, p.short_name, sizeof(owner.short_name));
    owner.is_licensed = true;
    config.lora.override_duty_cycle = true;
    config.lora.tx_power = p.tx_power;
    config.lora.override_frequency = p.frequency;
    // Set node info broadcast interval to 10 minutes
    // For FCC minimum call-sign announcement
    config.device.node_info_broadcast_secs = 600;

    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY;
    // Remove PSK of primary channel for plaintext amateur usage
    auto primaryChannel = channels.getByIndex(channels.getPrimaryIndex());
    auto &channelSettings = primaryChannel.settings;
    channelSettings.psk.bytes[0] = 0;
    channelSettings.psk.size = 0;
    channels.setChannel(primaryChannel);
    channels.onConfigChanged();

    service->reloadOwner(false);
    saveChanges(SEGMENT_CONFIG | SEGMENT_DEVICESTATE | SEGMENT_CHANNELS);
}

AdminModule::AdminModule() : ProtobufModule("Admin", meshtastic_PortNum_ADMIN_APP, &meshtastic_AdminMessage_msg)
{
    // restrict to the admin channel for rx
    // boundChannel = Channels::adminChannel;
}

void AdminModule::setPassKey(meshtastic_AdminMessage *res)
{
    if (session_time == 0 || millis() / 1000 > session_time + 150) {
        for (int i = 0; i < 8; i++) {
            session_passkey[i] = random();
        }
        session_time = millis() / 1000;
    }
    memcpy(res->session_passkey.bytes, session_passkey, 8);
    res->session_passkey.size = 8;
    printBytes("Set admin key to ", res->session_passkey.bytes, 8);
    // if halfway to session_expire, regenerate session_passkey, reset the timeout
    // set the key in the packet
}

bool AdminModule::checkPassKey(meshtastic_AdminMessage *res)
{ // check that the key in the packet is still valid
    printBytes("Incoming session key: ", res->session_passkey.bytes, 8);
    printBytes("Expected session key: ", session_passkey, 8);
    return (session_time + 300 > millis() / 1000 && res->session_passkey.size == 8 &&
            memcmp(res->session_passkey.bytes, session_passkey, 8) == 0);
}

bool AdminModule::messageIsResponse(const meshtastic_AdminMessage *r)
{
    if (r->which_payload_variant == meshtastic_AdminMessage_get_channel_response_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_owner_response_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_config_response_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_module_config_response_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_canned_message_module_messages_response_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_device_metadata_response_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_ringtone_response_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_device_connection_status_response_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_node_remote_hardware_pins_response_tag ||
        r->which_payload_variant == meshtastic_NodeRemoteHardwarePinsResponse_node_remote_hardware_pins_tag)
        return true;
    else
        return false;
}

bool AdminModule::messageIsRequest(const meshtastic_AdminMessage *r)
{
    if (r->which_payload_variant == meshtastic_AdminMessage_get_channel_request_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_owner_request_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_config_request_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_module_config_request_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_canned_message_module_messages_request_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_device_metadata_request_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_ringtone_request_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_device_connection_status_request_tag ||
        r->which_payload_variant == meshtastic_AdminMessage_get_node_remote_hardware_pins_request_tag)
        return true;
    else
        return false;
}

void AdminModule::sendWarning(const char *message)
{
    meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
    cn->level = meshtastic_LogRecord_Level_WARNING;
    cn->time = getValidTime(RTCQualityFromNet);
    strncpy(cn->message, message, sizeof(cn->message));
    service->sendClientNotification(cn);
}

void disableBluetooth()
{
#if HAS_BLUETOOTH
#ifdef ARCH_ESP32
    if (nimbleBluetooth)
        nimbleBluetooth->deinit();
#elif defined(ARCH_NRF52)
    if (nrf52Bluetooth)
        nrf52Bluetooth->shutdown();
#endif
#endif
}
