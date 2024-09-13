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

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
#include "AccelerometerThread.h"
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
    bool fromOthers = mp.from != 0 && mp.from != nodeDB->getNodeNum();
    if (mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        return handled;
    }
    meshtastic_Channel *ch = &channels.getByIndex(mp.channel);
    // Could tighten this up further by tracking the last public_key we went an AdminMessage request to
    // and only allowing responses from that remote.
    if (!((mp.from == 0 && !config.security.is_managed) || messageIsResponse(r) ||
          (strcasecmp(ch->settings.name, Channels::adminChannel) == 0 && config.security.admin_channel_enabled) ||
          (mp.pki_encrypted && memcmp(mp.public_key.bytes, config.security.admin_key[0].bytes, 32) == 0))) {
        LOG_INFO("Ignoring admin payload %i\n", r->which_payload_variant);
        return handled;
    }
    LOG_INFO("Handling admin payload %i\n", r->which_payload_variant);

    // all of the get and set messages, including those for other modules, flow through here first.
    // any message that changes state, we want to check the passkey for
    if (mp.from != 0 && !messageIsRequest(r) && !messageIsResponse(r)) {
        if (!checkPassKey(r)) {
            LOG_WARN("Admin message without session_key!\n");
            return handled;
        }
    }
    switch (r->which_payload_variant) {

    /**
     * Getters
     */
    case meshtastic_AdminMessage_get_owner_request_tag:
        LOG_INFO("Client is getting owner\n");
        handleGetOwner(mp);
        break;

    case meshtastic_AdminMessage_get_config_request_tag:
        LOG_INFO("Client is getting config\n");
        handleGetConfig(mp, r->get_config_request);
        break;

    case meshtastic_AdminMessage_get_module_config_request_tag:
        LOG_INFO("Client is getting module config\n");
        handleGetModuleConfig(mp, r->get_module_config_request);
        break;

    case meshtastic_AdminMessage_get_channel_request_tag: {
        uint32_t i = r->get_channel_request - 1;
        LOG_INFO("Client is getting channel %u\n", i);
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
        LOG_INFO("Client is setting owner\n");
        handleSetOwner(r->set_owner);
        break;

    case meshtastic_AdminMessage_set_config_tag:
        LOG_INFO("Client is setting the config\n");
        handleSetConfig(r->set_config);
        break;

    case meshtastic_AdminMessage_set_module_config_tag:
        LOG_INFO("Client is setting the module config\n");
        handleSetModuleConfig(r->set_module_config);
        break;

    case meshtastic_AdminMessage_set_channel_tag:
        LOG_INFO("Client is setting channel %d\n", r->set_channel.index);
        if (r->set_channel.index < 0 || r->set_channel.index >= (int)MAX_NUM_CHANNELS)
            myReply = allocErrorResponse(meshtastic_Routing_Error_BAD_REQUEST, &mp);
        else
            handleSetChannel(r->set_channel);
        break;
    case meshtastic_AdminMessage_set_ham_mode_tag:
        LOG_INFO("Client is setting ham mode\n");
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
            LOG_INFO("No OTA firmware available, scheduling regular reboot in %d seconds\n", s);
            screen->startAlert("Rebooting...");
        } else {
            screen->startFirmwareUpdateScreen();
            BleOta::switchToOtaApp();
            LOG_INFO("Rebooting to OTA in %d seconds\n", s);
        }
#else
        LOG_INFO("Not on ESP32, scheduling regular reboot in %d seconds\n", s);
        screen->startAlert("Rebooting...");
#endif
        rebootAtMsec = (s < 0) ? 0 : (millis() + s * 1000);
        break;
    }
    case meshtastic_AdminMessage_shutdown_seconds_tag: {
        int32_t s = r->shutdown_seconds;
        LOG_INFO("Shutdown in %d seconds\n", s);
        shutdownAtMsec = (s < 0) ? 0 : (millis() + s * 1000);
        break;
    }
    case meshtastic_AdminMessage_get_device_metadata_request_tag: {
        LOG_INFO("Client is getting device metadata\n");
        handleGetDeviceMetadata(mp);
        break;
    }
    case meshtastic_AdminMessage_factory_reset_config_tag: {
        disableBluetooth();
        LOG_INFO("Initiating factory config reset\n");
        nodeDB->factoryReset();
        LOG_INFO("Factory config reset finished, rebooting soon.\n");
        reboot(DEFAULT_REBOOT_SECONDS);
        break;
    }
    case meshtastic_AdminMessage_factory_reset_device_tag: {
        disableBluetooth();
        LOG_INFO("Initiating full factory reset\n");
        nodeDB->factoryReset(true);
        reboot(DEFAULT_REBOOT_SECONDS);
        break;
    }
    case meshtastic_AdminMessage_nodedb_reset_tag: {
        disableBluetooth();
        LOG_INFO("Initiating node-db reset\n");
        nodeDB->resetNodes();
        reboot(DEFAULT_REBOOT_SECONDS);
        break;
    }
    case meshtastic_AdminMessage_begin_edit_settings_tag: {
        LOG_INFO("Beginning transaction for editing settings\n");
        hasOpenEditTransaction = true;
        break;
    }
    case meshtastic_AdminMessage_commit_edit_settings_tag: {
        disableBluetooth();
        LOG_INFO("Committing transaction for edited settings\n");
        hasOpenEditTransaction = false;
        saveChanges(SEGMENT_CONFIG | SEGMENT_MODULECONFIG | SEGMENT_DEVICESTATE | SEGMENT_CHANNELS);
        break;
    }
    case meshtastic_AdminMessage_get_device_connection_status_request_tag: {
        LOG_INFO("Client is getting device connection status\n");
        handleGetDeviceConnectionStatus(mp);
        break;
    }
    case meshtastic_AdminMessage_get_module_config_response_tag: {
        LOG_INFO("Client is receiving a get_module_config response.\n");
        if (fromOthers && r->get_module_config_response.which_payload_variant ==
                              meshtastic_AdminMessage_ModuleConfigType_REMOTEHARDWARE_CONFIG) {
            handleGetModuleConfigResponse(mp, r);
        }
        break;
    }
    case meshtastic_AdminMessage_remove_by_nodenum_tag: {
        LOG_INFO("Client is receiving a remove_nodenum command.\n");
        nodeDB->removeNodeByNum(r->remove_by_nodenum);
        this->notifyObservers(r); // Observed by screen
        break;
    }
    case meshtastic_AdminMessage_set_favorite_node_tag: {
        LOG_INFO("Client is receiving a set_favorite_node command.\n");
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(r->set_favorite_node);
        if (node != NULL) {
            node->is_favorite = true;
            saveChanges(SEGMENT_DEVICESTATE, false);
        }
        break;
    }
    case meshtastic_AdminMessage_remove_favorite_node_tag: {
        LOG_INFO("Client is receiving a remove_favorite_node command.\n");
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(r->remove_favorite_node);
        if (node != NULL) {
            node->is_favorite = false;
            saveChanges(SEGMENT_DEVICESTATE, false);
        }
        break;
    }
    case meshtastic_AdminMessage_set_fixed_position_tag: {
        if (fromOthers) {
            LOG_INFO("Ignoring set_fixed_position command from another node.\n");
        } else {
            LOG_INFO("Client is receiving a set_fixed_position command.\n");
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
        }
        break;
    }
    case meshtastic_AdminMessage_remove_fixed_position_tag: {
        if (fromOthers) {
            LOG_INFO("Ignoring remove_fixed_position command from another node.\n");
        } else {
            LOG_INFO("Client is receiving a remove_fixed_position command.\n");
            nodeDB->clearLocalPosition();
            config.position.fixed_position = false;
            saveChanges(SEGMENT_DEVICESTATE | SEGMENT_CONFIG, false);
        }
        break;
    }
    case meshtastic_AdminMessage_set_time_only_tag: {
        LOG_INFO("Client is receiving a set_time_only command.\n");
        struct timeval tv;
        tv.tv_sec = r->set_time_only;
        tv.tv_usec = 0;

        perhapsSetRTC(RTCQualityNTP, &tv, false);
        break;
    }
    case meshtastic_AdminMessage_enter_dfu_mode_request_tag: {
        LOG_INFO("Client is requesting to enter DFU mode.\n");
#if defined(ARCH_NRF52) || defined(ARCH_RP2040)
        enterDfuMode();
#endif
        break;
    }
    case meshtastic_AdminMessage_delete_file_request_tag: {
        LOG_DEBUG("Client is requesting to delete file: %s\n", r->delete_file_request);
#ifdef FSCom
        if (FSCom.remove(r->delete_file_request)) {
            LOG_DEBUG("Successfully deleted file\n");
        } else {
            LOG_DEBUG("Failed to delete file\n");
        }
#endif
        break;
    }
#ifdef ARCH_PORTDUINO
    case meshtastic_AdminMessage_exit_simulator_tag:
        LOG_INFO("Exiting simulator\n");
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
            LOG_DEBUG("We did not responded to a request that wanted a respond. req.variant=%d\n", r->which_payload_variant);
        } else if (handleResult != AdminMessageHandleResult::HANDLED) {
            // Probably a message sent by us or sent to our local node.  FIXME, we should avoid scanning these messages
            LOG_INFO("Ignoring nonrelevant admin %d\n", r->which_payload_variant);
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
        LOG_DEBUG("Remote hardware module disabled or no available_pins. Skipping...\n");
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
        LOG_INFO("Setting config: Device\n");
        config.has_device = true;
#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
        if (config.device.double_tap_as_button_press == false && c.payload_variant.device.double_tap_as_button_press == true &&
            accelerometerThread->enabled == false) {
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
        // If we're setting router role for the first time, install its intervals
        if (existingRole != c.payload_variant.device.role)
            nodeDB->installRoleDefaults(c.payload_variant.device.role);
        if (config.device.node_info_broadcast_secs < min_node_info_broadcast_secs) {
            LOG_DEBUG("Tried to set node_info_broadcast_secs too low, setting to %d\n", min_node_info_broadcast_secs);
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
#if EVENT_MODE
        // If we're in event mode, nobody is a Router or Repeater
        if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER ||
            config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER) {
            config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
        }
#endif
        break;
    case meshtastic_Config_position_tag:
        LOG_INFO("Setting config: Position\n");
        config.has_position = true;
        config.position = c.payload_variant.position;
        // Save nodedb as well in case we got a fixed position packet
        saveChanges(SEGMENT_DEVICESTATE, false);
        break;
    case meshtastic_Config_power_tag:
        LOG_INFO("Setting config: Power\n");
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
        break;
    case meshtastic_Config_network_tag:
        LOG_INFO("Setting config: WiFi\n");
        config.has_network = true;
        config.network = c.payload_variant.network;
        break;
    case meshtastic_Config_display_tag:
        LOG_INFO("Setting config: Display\n");
        config.has_display = true;
        if (config.display.screen_on_secs == c.payload_variant.display.screen_on_secs &&
            config.display.flip_screen == c.payload_variant.display.flip_screen &&
            config.display.oled == c.payload_variant.display.oled) {
            requiresReboot = false;
        }
#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
        if (config.display.wake_on_tap_or_motion == false && c.payload_variant.display.wake_on_tap_or_motion == true &&
            accelerometerThread->enabled == false) {
            accelerometerThread->start();
        }
#endif
        config.display = c.payload_variant.display;
        break;
    case meshtastic_Config_lora_tag:
        LOG_INFO("Setting config: LoRa\n");
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
        LOG_INFO("Setting config: Bluetooth\n");
        config.has_bluetooth = true;
        config.bluetooth = c.payload_variant.bluetooth;
        break;
    case meshtastic_Config_security_tag:
        LOG_INFO("Setting config: Security\n");
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
    if (requiresReboot) {
        disableBluetooth();
    }

    saveChanges(changes, requiresReboot);
}

void AdminModule::handleSetModuleConfig(const meshtastic_ModuleConfig &c)
{
    disableBluetooth();
    switch (c.which_payload_variant) {
    case meshtastic_ModuleConfig_mqtt_tag:
        LOG_INFO("Setting module config: MQTT\n");
        moduleConfig.has_mqtt = true;
        moduleConfig.mqtt = c.payload_variant.mqtt;
        break;
    case meshtastic_ModuleConfig_serial_tag:
        LOG_INFO("Setting module config: Serial\n");
        moduleConfig.has_serial = true;
        moduleConfig.serial = c.payload_variant.serial;
        break;
    case meshtastic_ModuleConfig_external_notification_tag:
        LOG_INFO("Setting module config: External Notification\n");
        moduleConfig.has_external_notification = true;
        moduleConfig.external_notification = c.payload_variant.external_notification;
        break;
    case meshtastic_ModuleConfig_store_forward_tag:
        LOG_INFO("Setting module config: Store & Forward\n");
        moduleConfig.has_store_forward = true;
        moduleConfig.store_forward = c.payload_variant.store_forward;
        break;
    case meshtastic_ModuleConfig_range_test_tag:
        LOG_INFO("Setting module config: Range Test\n");
        moduleConfig.has_range_test = true;
        moduleConfig.range_test = c.payload_variant.range_test;
        break;
    case meshtastic_ModuleConfig_telemetry_tag:
        LOG_INFO("Setting module config: Telemetry\n");
        moduleConfig.has_telemetry = true;
        moduleConfig.telemetry = c.payload_variant.telemetry;
        break;
    case meshtastic_ModuleConfig_canned_message_tag:
        LOG_INFO("Setting module config: Canned Message\n");
        moduleConfig.has_canned_message = true;
        moduleConfig.canned_message = c.payload_variant.canned_message;
        break;
    case meshtastic_ModuleConfig_audio_tag:
        LOG_INFO("Setting module config: Audio\n");
        moduleConfig.has_audio = true;
        moduleConfig.audio = c.payload_variant.audio;
        break;
    case meshtastic_ModuleConfig_remote_hardware_tag:
        LOG_INFO("Setting module config: Remote Hardware\n");
        moduleConfig.has_remote_hardware = true;
        moduleConfig.remote_hardware = c.payload_variant.remote_hardware;
        break;
    case meshtastic_ModuleConfig_neighbor_info_tag:
        LOG_INFO("Setting module config: Neighbor Info\n");
        moduleConfig.has_neighbor_info = true;
        if (moduleConfig.neighbor_info.update_interval < min_neighbor_info_broadcast_secs) {
            LOG_DEBUG("Tried to set update_interval too low, setting to %d\n", default_neighbor_info_broadcast_secs);
            moduleConfig.neighbor_info.update_interval = default_neighbor_info_broadcast_secs;
        }
        moduleConfig.neighbor_info = c.payload_variant.neighbor_info;
        break;
    case meshtastic_ModuleConfig_detection_sensor_tag:
        LOG_INFO("Setting module config: Detection Sensor\n");
        moduleConfig.has_detection_sensor = true;
        moduleConfig.detection_sensor = c.payload_variant.detection_sensor;
        break;
    case meshtastic_ModuleConfig_ambient_lighting_tag:
        LOG_INFO("Setting module config: Ambient Lighting\n");
        moduleConfig.has_ambient_lighting = true;
        moduleConfig.ambient_lighting = c.payload_variant.ambient_lighting;
        break;
    case meshtastic_ModuleConfig_paxcounter_tag:
        LOG_INFO("Setting module config: Paxcounter\n");
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
            LOG_INFO("Getting config: Device\n");
            res.get_config_response.which_payload_variant = meshtastic_Config_device_tag;
            res.get_config_response.payload_variant.device = config.device;
            break;
        case meshtastic_AdminMessage_ConfigType_POSITION_CONFIG:
            LOG_INFO("Getting config: Position\n");
            res.get_config_response.which_payload_variant = meshtastic_Config_position_tag;
            res.get_config_response.payload_variant.position = config.position;
            break;
        case meshtastic_AdminMessage_ConfigType_POWER_CONFIG:
            LOG_INFO("Getting config: Power\n");
            res.get_config_response.which_payload_variant = meshtastic_Config_power_tag;
            res.get_config_response.payload_variant.power = config.power;
            break;
        case meshtastic_AdminMessage_ConfigType_NETWORK_CONFIG:
            LOG_INFO("Getting config: Network\n");
            res.get_config_response.which_payload_variant = meshtastic_Config_network_tag;
            res.get_config_response.payload_variant.network = config.network;
            writeSecret(res.get_config_response.payload_variant.network.wifi_psk,
                        sizeof(res.get_config_response.payload_variant.network.wifi_psk), config.network.wifi_psk);
            break;
        case meshtastic_AdminMessage_ConfigType_DISPLAY_CONFIG:
            LOG_INFO("Getting config: Display\n");
            res.get_config_response.which_payload_variant = meshtastic_Config_display_tag;
            res.get_config_response.payload_variant.display = config.display;
            break;
        case meshtastic_AdminMessage_ConfigType_LORA_CONFIG:
            LOG_INFO("Getting config: LoRa\n");
            res.get_config_response.which_payload_variant = meshtastic_Config_lora_tag;
            res.get_config_response.payload_variant.lora = config.lora;
            break;
        case meshtastic_AdminMessage_ConfigType_BLUETOOTH_CONFIG:
            LOG_INFO("Getting config: Bluetooth\n");
            res.get_config_response.which_payload_variant = meshtastic_Config_bluetooth_tag;
            res.get_config_response.payload_variant.bluetooth = config.bluetooth;
            break;
        case meshtastic_AdminMessage_ConfigType_SECURITY_CONFIG:
            LOG_INFO("Getting config: Security\n");
            res.get_config_response.which_payload_variant = meshtastic_Config_security_tag;
            res.get_config_response.payload_variant.security = config.security;
            break;
        case meshtastic_AdminMessage_ConfigType_SESSIONKEY_CONFIG:
            LOG_INFO("Getting config: Sessionkey\n");
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
            LOG_INFO("Getting module config: MQTT\n");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_mqtt_tag;
            res.get_module_config_response.payload_variant.mqtt = moduleConfig.mqtt;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_SERIAL_CONFIG:
            LOG_INFO("Getting module config: Serial\n");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_serial_tag;
            res.get_module_config_response.payload_variant.serial = moduleConfig.serial;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_EXTNOTIF_CONFIG:
            LOG_INFO("Getting module config: External Notification\n");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_external_notification_tag;
            res.get_module_config_response.payload_variant.external_notification = moduleConfig.external_notification;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_STOREFORWARD_CONFIG:
            LOG_INFO("Getting module config: Store & Forward\n");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_store_forward_tag;
            res.get_module_config_response.payload_variant.store_forward = moduleConfig.store_forward;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_RANGETEST_CONFIG:
            LOG_INFO("Getting module config: Range Test\n");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_range_test_tag;
            res.get_module_config_response.payload_variant.range_test = moduleConfig.range_test;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_TELEMETRY_CONFIG:
            LOG_INFO("Getting module config: Telemetry\n");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_telemetry_tag;
            res.get_module_config_response.payload_variant.telemetry = moduleConfig.telemetry;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_CANNEDMSG_CONFIG:
            LOG_INFO("Getting module config: Canned Message\n");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_canned_message_tag;
            res.get_module_config_response.payload_variant.canned_message = moduleConfig.canned_message;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_AUDIO_CONFIG:
            LOG_INFO("Getting module config: Audio\n");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_audio_tag;
            res.get_module_config_response.payload_variant.audio = moduleConfig.audio;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_REMOTEHARDWARE_CONFIG:
            LOG_INFO("Getting module config: Remote Hardware\n");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_remote_hardware_tag;
            res.get_module_config_response.payload_variant.remote_hardware = moduleConfig.remote_hardware;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_NEIGHBORINFO_CONFIG:
            LOG_INFO("Getting module config: Neighbor Info\n");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_neighbor_info_tag;
            res.get_module_config_response.payload_variant.neighbor_info = moduleConfig.neighbor_info;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_DETECTIONSENSOR_CONFIG:
            LOG_INFO("Getting module config: Detection Sensor\n");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_detection_sensor_tag;
            res.get_module_config_response.payload_variant.detection_sensor = moduleConfig.detection_sensor;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_AMBIENTLIGHTING_CONFIG:
            LOG_INFO("Getting module config: Ambient Lighting\n");
            res.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_ambient_lighting_tag;
            res.get_module_config_response.payload_variant.ambient_lighting = moduleConfig.ambient_lighting;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_PAXCOUNTER_CONFIG:
            LOG_INFO("Getting module config: Paxcounter\n");
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
    conn.wifi.status.is_connected = WiFi.status() != WL_CONNECTED;
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
    conn.bluetooth.is_connected = nimbleBluetooth->isConnected();
    conn.bluetooth.rssi = nimbleBluetooth->getRssi();
#elif defined(ARCH_NRF52)
    conn.bluetooth.is_connected = nrf52Bluetooth->isConnected();
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
    LOG_INFO("Rebooting in %d seconds\n", seconds);
    screen->startAlert("Rebooting...");
    rebootAtMsec = (seconds < 0) ? 0 : (millis() + seconds * 1000);
}

void AdminModule::saveChanges(int saveWhat, bool shouldReboot)
{
    if (!hasOpenEditTransaction) {
        LOG_INFO("Saving changes to disk\n");
        service->reloadConfig(saveWhat); // Calls saveToDisk among other things
    } else {
        LOG_INFO("Delaying save of changes to disk until the open transaction is committed\n");
    }
    if (shouldReboot) {
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
    printBytes("Setting admin key to ", res->session_passkey.bytes, 8);
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

bool AdminModule::messageIsResponse(meshtastic_AdminMessage *r)
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

bool AdminModule::messageIsRequest(meshtastic_AdminMessage *r)
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