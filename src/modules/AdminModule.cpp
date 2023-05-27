#include "AdminModule.h"
#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#ifdef ARCH_ESP32
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

#if HAS_WIFI || HAS_ETHERNET
#include "mqtt/MQTT.h"
#endif

#define DEFAULT_REBOOT_SECONDS 7

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
    bool fromOthers = mp.from != 0 && mp.from != nodeDB.getNodeNum();

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
#ifdef ARCH_ESP32
        if (BleOta::getOtaAppVersion().isEmpty()) {
            LOG_INFO("No OTA firmware available, scheduling regular reboot in %d seconds\n", s);
            screen->startRebootScreen();
        } else {
            screen->startFirmwareUpdateScreen();
            BleOta::switchToOtaApp();
            LOG_INFO("Rebooting to OTA in %d seconds\n", s);
        }
#else
        LOG_INFO("Not on ESP32, scheduling regular reboot in %d seconds\n", s);
        screen->startRebootScreen();
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
    case meshtastic_AdminMessage_factory_reset_tag: {
        LOG_INFO("Initiating factory reset\n");
        nodeDB.factoryReset();
        reboot(DEFAULT_REBOOT_SECONDS);
        break;
    }
    case meshtastic_AdminMessage_nodedb_reset_tag: {
        LOG_INFO("Initiating node-db reset\n");
        nodeDB.resetNodes();
        reboot(DEFAULT_REBOOT_SECONDS);
        break;
    }
    case meshtastic_AdminMessage_begin_edit_settings_tag: {
        LOG_INFO("Beginning transaction for editing settings\n");
        hasOpenEditTransaction = true;
        break;
    }
    case meshtastic_AdminMessage_commit_edit_settings_tag: {
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
#ifdef ARCH_PORTDUINO
    case meshtastic_AdminMessage_exit_simulator_tag:
        LOG_INFO("Exiting simulator\n");
        _exit(0);
        break;
#endif

    default:
        meshtastic_AdminMessage res = meshtastic_AdminMessage_init_default;
        AdminMessageHandleResult handleResult = MeshModule::handleAdminMessageForAllPlugins(mp, r, &res);

        if (handleResult == AdminMessageHandleResult::HANDLED_WITH_RESPONSE) {
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
        !r->get_module_config_response.payload_variant.remote_hardware.available_pins) {
        LOG_DEBUG("Remote hardware module disabled or no vailable_pins. Skipping...\n");
        return;
    }
    for (uint8_t i = 0; i < devicestate.node_remote_hardware_pins_count; i++) {
        if (devicestate.node_remote_hardware_pins[i].node_num == 0 || !devicestate.node_remote_hardware_pins[i].has_pin) {
            continue;
        }
        for (uint8_t j = 0; j < sizeof(r->get_module_config_response.payload_variant.remote_hardware.available_pins); j++) {
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
        service.reloadOwner(!hasOpenEditTransaction);
        saveChanges(SEGMENT_DEVICESTATE);
    }
}

void AdminModule::handleSetConfig(const meshtastic_Config &c)
{
    auto existingRole = config.device.role;
    bool isRegionUnset = (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET);

    switch (c.which_payload_variant) {
    case meshtastic_Config_device_tag:
        LOG_INFO("Setting config: Device\n");
        config.has_device = true;
        config.device = c.payload_variant.device;
        // If we're setting router role for the first time, install its intervals
        if (existingRole != c.payload_variant.device.role)
            nodeDB.installRoleDefaults(c.payload_variant.device.role);
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
        config.display = c.payload_variant.display;
        break;
    case meshtastic_Config_lora_tag:
        LOG_INFO("Setting config: LoRa\n");
        config.has_lora = true;
        config.lora = c.payload_variant.lora;
        if (isRegionUnset && config.lora.region > meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
            config.lora.tx_enabled = true;
        }
        break;
    case meshtastic_Config_bluetooth_tag:
        LOG_INFO("Setting config: Bluetooth\n");
        config.has_bluetooth = true;
        config.bluetooth = c.payload_variant.bluetooth;
        break;
    }

    saveChanges(SEGMENT_CONFIG);
}

void AdminModule::handleSetModuleConfig(const meshtastic_ModuleConfig &c)
{
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
        }
        // NOTE: The phone app needs to know the ls_secs value so it can properly expect sleep behavior.
        // So even if we internally use 0 to represent 'use default' we still need to send the value we are
        // using to the app (so that even old phone apps work with new device loads).
        // r.get_radio_response.preferences.ls_secs = getPref_ls_secs();
        // hideSecret(r.get_radio_response.preferences.wifi_ssid); // hmm - leave public for now, because only minimally private
        // and useful for users to know current provisioning) hideSecret(r.get_radio_response.preferences.wifi_password);
        // r.get_config_response.which_payloadVariant = Config_ModuleConfig_telemetry_tag;
        res.which_payload_variant = meshtastic_AdminMessage_get_config_response_tag;
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
        }

        // NOTE: The phone app needs to know the ls_secsvalue so it can properly expect sleep behavior.
        // So even if we internally use 0 to represent 'use default' we still need to send the value we are
        // using to the app (so that even old phone apps work with new device loads).
        // r.get_radio_response.preferences.ls_secs = getPref_ls_secs();
        // hideSecret(r.get_radio_response.preferences.wifi_ssid); // hmm - leave public for now, because only minimally private
        // and useful for users to know current provisioning) hideSecret(r.get_radio_response.preferences.wifi_password);
        // r.get_config_response.which_payloadVariant = Config_ModuleConfig_telemetry_tag;
        res.which_payload_variant = meshtastic_AdminMessage_get_module_config_response_tag;
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
        nodePin.node_num = nodeDB.getNodeNum();
        nodePin.pin = moduleConfig.remote_hardware.available_pins[i];
        r.get_node_remote_hardware_pins_response.node_remote_hardware_pins[i + 12] = nodePin;
    }
    myReply = allocDataProtobuf(r);
}

void AdminModule::handleGetDeviceMetadata(const meshtastic_MeshPacket &req)
{
    meshtastic_AdminMessage r = meshtastic_AdminMessage_init_default;
    r.get_device_metadata_response = getDeviceMetadata();
    r.which_payload_variant = meshtastic_AdminMessage_get_device_metadata_response_tag;
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
        conn.wifi.status.is_mqtt_connected = mqtt && mqtt->connected();
        conn.wifi.status.is_syslog_connected = false; // FIXME wire this up
    }
#endif

#if HAS_ETHERNET
    conn.has_ethernet = true;
    conn.ethernet.has_status = true;
    if (Ethernet.linkStatus() == LinkON) {
        conn.ethernet.status.is_connected = true;
        conn.ethernet.status.ip_address = Ethernet.localIP();
        conn.ethernet.status.is_mqtt_connected = mqtt && mqtt->connected();
        conn.ethernet.status.is_syslog_connected = false; // FIXME wire this up
    } else {
        conn.ethernet.status.is_connected = false;
    }
#endif

#if HAS_BLUETOOTH
    conn.has_bluetooth = true;
    conn.bluetooth.pin = config.bluetooth.fixed_pin;
#endif
#ifdef ARCH_ESP32
    conn.bluetooth.is_connected = nimbleBluetooth->isConnected();
    conn.bluetooth.rssi = nimbleBluetooth->getRssi();
#elif defined(ARCH_NRF52)
    conn.bluetooth.is_connected = nrf52Bluetooth->isConnected();
#endif
    conn.has_serial = true; // No serial-less devices
    conn.serial.is_connected = powerFSM.getState() == &stateSERIAL;
    conn.serial.baud = SERIAL_BAUD;

    r.get_device_connection_status_response = conn;
    r.which_payload_variant = meshtastic_AdminMessage_get_device_connection_status_response_tag;
    myReply = allocDataProtobuf(r);
}

void AdminModule::handleGetChannel(const meshtastic_MeshPacket &req, uint32_t channelIndex)
{
    if (req.decoded.want_response) {
        // We create the reply here
        meshtastic_AdminMessage r = meshtastic_AdminMessage_init_default;
        r.get_channel_response = channels.getByIndex(channelIndex);
        r.which_payload_variant = meshtastic_AdminMessage_get_channel_response_tag;
        myReply = allocDataProtobuf(r);
    }
}

void AdminModule::reboot(int32_t seconds)
{
    LOG_INFO("Rebooting in %d seconds\n", seconds);
    screen->startRebootScreen();
    rebootAtMsec = (seconds < 0) ? 0 : (millis() + seconds * 1000);
}

void AdminModule::saveChanges(int saveWhat, bool shouldReboot)
{
    if (!hasOpenEditTransaction) {
        LOG_INFO("Saving changes to disk\n");
        service.reloadConfig(saveWhat); // Calls saveToDisk among other things
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

    service.reloadOwner(false);
    service.reloadConfig(SEGMENT_CONFIG | SEGMENT_DEVICESTATE | SEGMENT_CHANNELS);
}

AdminModule::AdminModule() : ProtobufModule("Admin", meshtastic_PortNum_ADMIN_APP, &meshtastic_AdminMessage_msg)
{
    // restrict to the admin channel for rx
    boundChannel = Channels::adminChannel;
}
