#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif

#include "Channels.h"
#include "Default.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PacketHistory.h"
#include "PhoneAPI.h"
#include "PowerFSM.h"
#include "RadioInterface.h"
#include "Router.h"
#include "SPILock.h"
#include "TypeConversions.h"
#include "concurrency/LockGuard.h"
#include "main.h"
#include "xmodem.h"

#if FromRadio_size > MAX_TO_FROM_RADIO_SIZE
#error FromRadio is too big
#endif

#if ToRadio_size > MAX_TO_FROM_RADIO_SIZE
#error ToRadio is too big
#endif
#if !MESHTASTIC_EXCLUDE_MQTT
#include "mqtt/MQTT.h"
#endif
#include "Throttle.h"
#include <RTC.h>

// Flag to indicate a heartbeat was received and we should send queue status
bool heartbeatReceived = false;

PhoneAPI::PhoneAPI()
{
    lastContactMsec = millis();
    std::fill(std::begin(recentToRadioPacketIds), std::end(recentToRadioPacketIds), 0);
}

PhoneAPI::~PhoneAPI()
{
    close();
}

void PhoneAPI::handleStartConfig()
{
    // Must be before setting state (because state is how we know !connected)
    if (!isConnected()) {
        onConnectionChanged(true);
        observe(&service->fromNumChanged);
#ifdef FSCom
        observe(&xModem.packetReady);
#endif
    }

    // Allow subclasses to prepare for high-throughput config traffic
    onConfigStart();

    // even if we were already connected - restart our state machine
    if (config_nonce == SPECIAL_NONCE_ONLY_NODES) {
        // If client only wants node info, jump directly to sending nodes
        state = STATE_SEND_OWN_NODEINFO;
        LOG_INFO("Client only wants node info, skipping other config");
    } else {
        state = STATE_SEND_MY_INFO;
    }
    pauseBluetoothLogging = true;
    spiLock->lock();
    filesManifest = getFiles("/", 10);
    spiLock->unlock();
    LOG_DEBUG("Got %d files in manifest", filesManifest.size());

    LOG_INFO("Start API client config millis=%u", millis());
    // Protect against concurrent BLE callbacks: they run in NimBLE's FreeRTOS task and also touch nodeInfoQueue.
    {
        concurrency::LockGuard guard(&nodeInfoMutex);
        nodeInfoForPhone = {};
        nodeInfoQueue.clear();
    }
    resetReadIndex();
}

void PhoneAPI::close()
{
    LOG_DEBUG("PhoneAPI::close()");

    if (state != STATE_SEND_NOTHING) {
        state = STATE_SEND_NOTHING;
        resetReadIndex();
        unobserve(&service->fromNumChanged);
#ifdef FSCom
        unobserve(&xModem.packetReady);
#endif
        releasePhonePacket(); // Don't leak phone packets on shutdown
        releaseQueueStatusPhonePacket();
        releaseMqttClientProxyPhonePacket();
        releaseClientNotification();
        onConnectionChanged(false);
        fromRadioScratch = {};
        toRadioScratch = {};
        // Clear cached node info under lock because NimBLE callbacks can still be draining it.
        {
            concurrency::LockGuard guard(&nodeInfoMutex);
            nodeInfoForPhone = {};
            nodeInfoQueue.clear();
        }
        packetForPhone = NULL;
        filesManifest.clear();
        fromRadioNum = 0;
        config_nonce = 0;
        config_state = 0;
        pauseBluetoothLogging = false;
        heartbeatReceived = false;
    }
}

bool PhoneAPI::checkConnectionTimeout()
{
    if (isConnected()) {
        bool newContact = checkIsConnected();
        if (!newContact) {
            LOG_INFO("Lost phone connection");
            close();
            return true;
        }
    }
    return false;
}

/**
 * Handle a ToRadio protobuf
 */
bool PhoneAPI::handleToRadio(const uint8_t *buf, size_t bufLength)
{
    powerFSM.trigger(EVENT_CONTACT_FROM_PHONE); // As long as the phone keeps talking to us, don't let the radio go to sleep
    lastContactMsec = millis();

    memset(&toRadioScratch, 0, sizeof(toRadioScratch));
    if (pb_decode_from_bytes(buf, bufLength, &meshtastic_ToRadio_msg, &toRadioScratch)) {
        switch (toRadioScratch.which_payload_variant) {
        case meshtastic_ToRadio_packet_tag:
            return handleToRadioPacket(toRadioScratch.packet);
        case meshtastic_ToRadio_want_config_id_tag:
            config_nonce = toRadioScratch.want_config_id;
            LOG_INFO("Client wants config, nonce=%u", config_nonce);
            handleStartConfig();
            break;
        case meshtastic_ToRadio_disconnect_tag:
            LOG_INFO("Disconnect from phone");
            close();
            break;
        case meshtastic_ToRadio_xmodemPacket_tag:
            LOG_INFO("Got xmodem packet");
#ifdef FSCom
            xModem.handlePacket(toRadioScratch.xmodemPacket);
#endif
            break;
#if !MESHTASTIC_EXCLUDE_MQTT
        case meshtastic_ToRadio_mqttClientProxyMessage_tag:
            LOG_DEBUG("Got MqttClientProxy message");
            if (state != STATE_SEND_PACKETS) {
                LOG_WARN("Ignore MqttClientProxy message while completing config handshake");
                break;
            }
            if (mqtt && moduleConfig.mqtt.proxy_to_client_enabled && moduleConfig.mqtt.enabled &&
                (channels.anyMqttEnabled() || moduleConfig.mqtt.map_reporting_enabled)) {
                mqtt->onClientProxyReceive(toRadioScratch.mqttClientProxyMessage);
            } else {
                LOG_WARN("MqttClientProxy received but proxy is not enabled, no channels have up/downlink, or map reporting "
                         "not enabled");
            }
            break;
#endif
        case meshtastic_ToRadio_heartbeat_tag:
            LOG_DEBUG("Got client heartbeat");
            heartbeatReceived = true;
            break;
        default:
            // Ignore nop messages
            break;
        }
    } else {
        LOG_ERROR("Error: ignore malformed toradio");
    }

    return false;
}

/**
 * Get the next packet we want to send to the phone, or NULL if no such packet is available.
 *
 * We assume buf is at least FromRadio_size bytes long.
 *
 * Our sending states progress in the following sequence (the client apps ASSUME THIS SEQUENCE, DO NOT CHANGE IT):
    STATE_SEND_MY_INFO, // send our my info record
    STATE_SEND_UIDATA,
    STATE_SEND_OWN_NODEINFO,
    STATE_SEND_METADATA,
    STATE_SEND_CHANNELS
    STATE_SEND_CONFIG,
    STATE_SEND_MODULE_CONFIG,
    STATE_SEND_OTHER_NODEINFOS, // states progress in this order as the device sends to the client
    STATE_SEND_FILEMANIFEST,
    STATE_SEND_COMPLETE_ID,
    STATE_SEND_PACKETS // send packets or debug strings
 */

size_t PhoneAPI::getFromRadio(uint8_t *buf)
{
    // Respond to heartbeat by sending queue status
    if (heartbeatReceived) {
        memset(&fromRadioScratch, 0, sizeof(fromRadioScratch));
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
        fromRadioScratch.queueStatus = router->getQueueStatus();
        heartbeatReceived = false;
        size_t numbytes = pb_encode_to_bytes(buf, meshtastic_FromRadio_size, &meshtastic_FromRadio_msg, &fromRadioScratch);
        LOG_DEBUG("FromRadio=STATE_SEND_QUEUE_STATUS, numbytes=%u", numbytes);
        return numbytes;
    }

    if (!available()) {
        return 0;
    }
    // In case we send a FromRadio packet
    memset(&fromRadioScratch, 0, sizeof(fromRadioScratch));

    // Advance states as needed
    switch (state) {
    case STATE_SEND_NOTHING:
        LOG_DEBUG("FromRadio=STATE_SEND_NOTHING");
        break;
    case STATE_SEND_MY_INFO:
        LOG_DEBUG("FromRadio=STATE_SEND_MY_INFO");
        // If the user has specified they don't want our node to share its location, make sure to tell the phone
        // app not to send locations on our behalf.
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_my_info_tag;
        strncpy(myNodeInfo.pio_env, optstr(APP_ENV), sizeof(myNodeInfo.pio_env));
        myNodeInfo.nodedb_count = static_cast<uint16_t>(nodeDB->getNumMeshNodes());
        fromRadioScratch.my_info = myNodeInfo;
        state = STATE_SEND_UIDATA;

        service->refreshLocalMeshNode(); // Update my NodeInfo because the client will be asking for it soon.
        break;

    case STATE_SEND_UIDATA:
        LOG_INFO("getFromRadio=STATE_SEND_UIDATA");
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_deviceuiConfig_tag;
        fromRadioScratch.deviceuiConfig = uiconfig;
        state = STATE_SEND_OWN_NODEINFO;
        break;

    case STATE_SEND_OWN_NODEINFO: {
        LOG_DEBUG("Send My NodeInfo");
        auto us = nodeDB->readNextMeshNode(readIndex);
        if (us) {
            auto info = TypeConversions::ConvertToNodeInfo(us);
            info.has_hops_away = false;
            info.is_favorite = true;
            {
                concurrency::LockGuard guard(&nodeInfoMutex);
                nodeInfoForPhone = info;
            }
            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_node_info_tag;
            fromRadioScratch.node_info = info;
            // Should allow us to resume sending NodeInfo in STATE_SEND_OTHER_NODEINFOS
            {
                concurrency::LockGuard guard(&nodeInfoMutex);
                nodeInfoForPhone.num = 0;
            }
        }
        if (config_nonce == SPECIAL_NONCE_ONLY_NODES) {
            // If client only wants node info, jump directly to sending nodes
            state = STATE_SEND_OTHER_NODEINFOS;
            onNowHasData(0);
        } else {
            state = STATE_SEND_METADATA;
        }
        break;
    }

    case STATE_SEND_METADATA:
        LOG_DEBUG("Send device metadata");
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_metadata_tag;
        fromRadioScratch.metadata = getDeviceMetadata();
        state = STATE_SEND_CHANNELS;
        break;

    case STATE_SEND_CHANNELS:
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_channel_tag;
        fromRadioScratch.channel = channels.getByIndex(config_state);
        config_state++;
        // Advance when we have sent all of our Channels
        if (config_state >= MAX_NUM_CHANNELS) {
            LOG_DEBUG("Send channels %d", config_state);
            state = STATE_SEND_CONFIG;
            config_state = _meshtastic_AdminMessage_ConfigType_MIN + 1;
        }
        break;

    case STATE_SEND_CONFIG:
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_config_tag;
        switch (config_state) {
        case meshtastic_Config_device_tag:
            LOG_DEBUG("Send config: device");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_device_tag;
            fromRadioScratch.config.payload_variant.device = config.device;
            break;
        case meshtastic_Config_position_tag:
            LOG_DEBUG("Send config: position");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_position_tag;
            fromRadioScratch.config.payload_variant.position = config.position;
            break;
        case meshtastic_Config_power_tag:
            LOG_DEBUG("Send config: power");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_power_tag;
            fromRadioScratch.config.payload_variant.power = config.power;
            fromRadioScratch.config.payload_variant.power.ls_secs = default_ls_secs;
            break;
        case meshtastic_Config_network_tag:
            LOG_DEBUG("Send config: network");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_network_tag;
            fromRadioScratch.config.payload_variant.network = config.network;
            break;
        case meshtastic_Config_display_tag:
            LOG_DEBUG("Send config: display");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_display_tag;
            fromRadioScratch.config.payload_variant.display = config.display;
            break;
        case meshtastic_Config_lora_tag:
            LOG_DEBUG("Send config: lora");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_lora_tag;
            fromRadioScratch.config.payload_variant.lora = config.lora;
            break;
        case meshtastic_Config_bluetooth_tag:
            LOG_DEBUG("Send config: bluetooth");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_bluetooth_tag;
            fromRadioScratch.config.payload_variant.bluetooth = config.bluetooth;
            break;
        case meshtastic_Config_security_tag:
            LOG_DEBUG("Send config: security");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_security_tag;
            fromRadioScratch.config.payload_variant.security = config.security;
            break;
        case meshtastic_Config_sessionkey_tag:
            LOG_DEBUG("Send config: sessionkey");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_sessionkey_tag;
            break;
        case meshtastic_Config_device_ui_tag: // NOOP!
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_device_ui_tag;
            break;
        default:
            LOG_ERROR("Unknown config type %d", config_state);
        }
        // NOTE: The phone app needs to know the ls_secs value so it can properly expect sleep behavior.
        // So even if we internally use 0 to represent 'use default' we still need to send the value we are
        // using to the app (so that even old phone apps work with new device loads).

        config_state++;
        // Advance when we have sent all of our config objects
        if (config_state > (_meshtastic_AdminMessage_ConfigType_MAX + 1)) {
            state = STATE_SEND_MODULECONFIG;
            config_state = _meshtastic_AdminMessage_ModuleConfigType_MIN + 1;
        }
        break;

    case STATE_SEND_MODULECONFIG:
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_moduleConfig_tag;
        switch (config_state) {
        case meshtastic_ModuleConfig_mqtt_tag:
            LOG_DEBUG("Send module config: mqtt");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_mqtt_tag;
            fromRadioScratch.moduleConfig.payload_variant.mqtt = moduleConfig.mqtt;
            break;
        case meshtastic_ModuleConfig_serial_tag:
            LOG_DEBUG("Send module config: serial");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_serial_tag;
            fromRadioScratch.moduleConfig.payload_variant.serial = moduleConfig.serial;
            break;
        case meshtastic_ModuleConfig_external_notification_tag:
            LOG_DEBUG("Send module config: ext notification");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_external_notification_tag;
            fromRadioScratch.moduleConfig.payload_variant.external_notification = moduleConfig.external_notification;
            break;
        case meshtastic_ModuleConfig_store_forward_tag:
            LOG_DEBUG("Send module config: store forward");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_store_forward_tag;
            fromRadioScratch.moduleConfig.payload_variant.store_forward = moduleConfig.store_forward;
            break;
        case meshtastic_ModuleConfig_range_test_tag:
            LOG_DEBUG("Send module config: range test");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_range_test_tag;
            fromRadioScratch.moduleConfig.payload_variant.range_test = moduleConfig.range_test;
            break;
        case meshtastic_ModuleConfig_telemetry_tag:
            LOG_DEBUG("Send module config: telemetry");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_telemetry_tag;
            fromRadioScratch.moduleConfig.payload_variant.telemetry = moduleConfig.telemetry;
            break;
        case meshtastic_ModuleConfig_canned_message_tag:
            LOG_DEBUG("Send module config: canned message");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_canned_message_tag;
            fromRadioScratch.moduleConfig.payload_variant.canned_message = moduleConfig.canned_message;
            break;
        case meshtastic_ModuleConfig_audio_tag:
            LOG_DEBUG("Send module config: audio");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_audio_tag;
            fromRadioScratch.moduleConfig.payload_variant.audio = moduleConfig.audio;
            break;
        case meshtastic_ModuleConfig_remote_hardware_tag:
            LOG_DEBUG("Send module config: remote hardware");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_remote_hardware_tag;
            fromRadioScratch.moduleConfig.payload_variant.remote_hardware = moduleConfig.remote_hardware;
            break;
        case meshtastic_ModuleConfig_neighbor_info_tag:
            LOG_DEBUG("Send module config: neighbor info");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_neighbor_info_tag;
            fromRadioScratch.moduleConfig.payload_variant.neighbor_info = moduleConfig.neighbor_info;
            break;
        case meshtastic_ModuleConfig_detection_sensor_tag:
            LOG_DEBUG("Send module config: detection sensor");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_detection_sensor_tag;
            fromRadioScratch.moduleConfig.payload_variant.detection_sensor = moduleConfig.detection_sensor;
            break;
        case meshtastic_ModuleConfig_ambient_lighting_tag:
            LOG_DEBUG("Send module config: ambient lighting");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_ambient_lighting_tag;
            fromRadioScratch.moduleConfig.payload_variant.ambient_lighting = moduleConfig.ambient_lighting;
            break;
        case meshtastic_ModuleConfig_paxcounter_tag:
            LOG_DEBUG("Send module config: paxcounter");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_paxcounter_tag;
            fromRadioScratch.moduleConfig.payload_variant.paxcounter = moduleConfig.paxcounter;
            break;
        default:
            LOG_ERROR("Unknown module config type %d", config_state);
        }

        config_state++;
        // Advance when we have sent all of our ModuleConfig objects
        if (config_state > (_meshtastic_AdminMessage_ModuleConfigType_MAX + 1)) {
            // Handle special nonce behaviors:
            // - SPECIAL_NONCE_ONLY_CONFIG: Skip node info, go directly to file manifest
            // - SPECIAL_NONCE_ONLY_NODES: After sending nodes, skip to complete
            if (config_nonce == SPECIAL_NONCE_ONLY_CONFIG) {
                state = STATE_SEND_FILEMANIFEST;
            } else {
                state = STATE_SEND_OTHER_NODEINFOS;
                onNowHasData(0);
            }
            config_state = 0;
        }
        break;

    case STATE_SEND_OTHER_NODEINFOS: {
        if (readIndex == 2) { //  readIndex==2 will be true for the first non-us node
            LOG_INFO("Start sending nodeinfos millis=%u", millis());
        }

        meshtastic_NodeInfo infoToSend = {};
        {
            concurrency::LockGuard guard(&nodeInfoMutex);
            if (nodeInfoForPhone.num == 0 && !nodeInfoQueue.empty()) {
                // Serve the next cached node without re-reading from the DB iterator.
                nodeInfoForPhone = nodeInfoQueue.front();
                nodeInfoQueue.pop_front();
            }
            infoToSend = nodeInfoForPhone;
            if (infoToSend.num != 0)
                nodeInfoForPhone = {};
        }

        if (infoToSend.num != 0) {
            // Just in case we stored a different user.id in the past, but should never happen going forward
            sprintf(infoToSend.user.id, "!%08x", infoToSend.num);

            // Logging this really slows down sending nodes on initial connection because the serial console is so slow, so only
            // uncomment if you really need to:
            // LOG_INFO("nodeinfo: num=0x%x, lastseen=%u, id=%s, name=%s", nodeInfoForPhone.num, nodeInfoForPhone.last_heard,
            // nodeInfoForPhone.user.id, nodeInfoForPhone.user.long_name);

            // Occasional progress logging. (readIndex==2 will be true for the first non-us node)
            if (readIndex == 2 || readIndex % 20 == 0) {
                LOG_DEBUG("nodeinfo: %d/%d", readIndex, nodeDB->getNumMeshNodes());
            }

            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_node_info_tag;
            fromRadioScratch.node_info = infoToSend;
            prefetchNodeInfos();
        } else {
            LOG_DEBUG("Done sending %d of %d nodeinfos millis=%u", readIndex, nodeDB->getNumMeshNodes(), millis());
            concurrency::LockGuard guard(&nodeInfoMutex);
            nodeInfoQueue.clear();
            state = STATE_SEND_FILEMANIFEST;
            // Go ahead and send that ID right now
            return getFromRadio(buf);
        }
        break;
    }

    case STATE_SEND_FILEMANIFEST: {
        LOG_DEBUG("FromRadio=STATE_SEND_FILEMANIFEST");
        // last element
        if (config_state == filesManifest.size() ||
            config_nonce == SPECIAL_NONCE_ONLY_NODES) { // also handles an empty filesManifest
            config_state = 0;
            filesManifest.clear();
            // Skip to complete packet
            sendConfigComplete();
        } else {
            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_fileInfo_tag;
            fromRadioScratch.fileInfo = filesManifest.at(config_state);
            LOG_DEBUG("File: %s (%d) bytes", fromRadioScratch.fileInfo.file_name, fromRadioScratch.fileInfo.size_bytes);
            config_state++;
        }
        break;
    }

    case STATE_SEND_COMPLETE_ID:
        sendConfigComplete();
        break;

    case STATE_SEND_PACKETS:
        pauseBluetoothLogging = false;
        // Do we have a message from the mesh or packet from the local device?
        LOG_DEBUG("FromRadio=STATE_SEND_PACKETS");
        if (queueStatusPacketForPhone) {
            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
            fromRadioScratch.queueStatus = *queueStatusPacketForPhone;
            releaseQueueStatusPhonePacket();
        } else if (mqttClientProxyMessageForPhone) {
            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_mqttClientProxyMessage_tag;
            fromRadioScratch.mqttClientProxyMessage = *mqttClientProxyMessageForPhone;
            releaseMqttClientProxyPhonePacket();
        } else if (xmodemPacketForPhone.control != meshtastic_XModem_Control_NUL) {
            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_xmodemPacket_tag;
            fromRadioScratch.xmodemPacket = xmodemPacketForPhone;
            xmodemPacketForPhone = meshtastic_XModem_init_zero;
        } else if (clientNotification) {
            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_clientNotification_tag;
            fromRadioScratch.clientNotification = *clientNotification;
            releaseClientNotification();
        } else if (packetForPhone) {
            printPacket("phone downloaded packet", packetForPhone);

            // Encapsulate as a FromRadio packet
            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_packet_tag;
            fromRadioScratch.packet = *packetForPhone;
            releasePhonePacket();
        }
        break;

    default:
        LOG_ERROR("getFromRadio unexpected state %d", state);
    }

    // Do we have a message from the mesh?
    if (fromRadioScratch.which_payload_variant != 0) {
        // Encapsulate as a FromRadio packet
        size_t numbytes = pb_encode_to_bytes(buf, meshtastic_FromRadio_size, &meshtastic_FromRadio_msg, &fromRadioScratch);

        // VERY IMPORTANT to not print debug messages while writing to fromRadioScratch - because we use that same buffer
        // for logging (when we are encapsulating with protobufs)
        return numbytes;
    }

    LOG_DEBUG("No FromRadio packet available");
    return 0;
}

void PhoneAPI::sendConfigComplete()
{
    LOG_INFO("Config Send Complete millis=%u", millis());
    fromRadioScratch.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    fromRadioScratch.config_complete_id = config_nonce;
    config_nonce = 0;
    state = STATE_SEND_PACKETS;

    // Allow subclasses to know we've entered steady-state so they can lower power consumption
    onConfigComplete();

    pauseBluetoothLogging = false;
}

void PhoneAPI::releasePhonePacket()
{
    if (packetForPhone) {
        service->releaseToPool(packetForPhone); // we just copied the bytes, so don't need this buffer anymore
        packetForPhone = NULL;
    }
}

void PhoneAPI::releaseQueueStatusPhonePacket()
{
    if (queueStatusPacketForPhone) {
        service->releaseQueueStatusToPool(queueStatusPacketForPhone);
        queueStatusPacketForPhone = NULL;
    }
}

void PhoneAPI::prefetchNodeInfos()
{
    bool added = false;
    // Keep the queue topped up so BLE reads stay responsive even if DB fetches take a moment.
    {
        concurrency::LockGuard guard(&nodeInfoMutex);
        while (nodeInfoQueue.size() < kNodePrefetchDepth) {
            auto nextNode = nodeDB->readNextMeshNode(readIndex);
            if (!nextNode)
                break;

            auto info = TypeConversions::ConvertToNodeInfo(nextNode);
            bool isUs = info.num == nodeDB->getNodeNum();
            info.hops_away = isUs ? 0 : info.hops_away;
            info.last_heard = isUs ? getValidTime(RTCQualityFromNet) : info.last_heard;
            info.snr = isUs ? 0 : info.snr;
            info.via_mqtt = isUs ? false : info.via_mqtt;
            info.is_favorite = info.is_favorite || isUs;
            nodeInfoQueue.push_back(info);
            added = true;
        }
    }

    if (added)
        onNowHasData(0);
}

void PhoneAPI::releaseMqttClientProxyPhonePacket()
{
    if (mqttClientProxyMessageForPhone) {
        service->releaseMqttClientProxyMessageToPool(mqttClientProxyMessageForPhone);
        mqttClientProxyMessageForPhone = NULL;
    }
}

void PhoneAPI::releaseClientNotification()
{
    if (clientNotification) {
        service->releaseClientNotificationToPool(clientNotification);
        clientNotification = NULL;
    }
}

/**
 * Return true if we have data available to send to the phone
 */
bool PhoneAPI::available()
{
    switch (state) {
    case STATE_SEND_NOTHING:
        return false;
    case STATE_SEND_MY_INFO:
    case STATE_SEND_UIDATA:
    case STATE_SEND_CHANNELS:
    case STATE_SEND_CONFIG:
    case STATE_SEND_MODULECONFIG:
    case STATE_SEND_METADATA:
    case STATE_SEND_OWN_NODEINFO:
    case STATE_SEND_FILEMANIFEST:
    case STATE_SEND_COMPLETE_ID:
        return true;

    case STATE_SEND_OTHER_NODEINFOS: {
        concurrency::LockGuard guard(&nodeInfoMutex);
        if (nodeInfoQueue.empty()) {
            // Drop the lock before prefetching; prefetchNodeInfos() will re-acquire it.
            goto PREFETCH_NODEINFO;
        }
    }
        return true; // Always say we have something, because we might need to advance our state machine
    PREFETCH_NODEINFO:
        prefetchNodeInfos();
        return true;
    case STATE_SEND_PACKETS: {
        if (!queueStatusPacketForPhone)
            queueStatusPacketForPhone = service->getQueueStatusForPhone();
        if (!mqttClientProxyMessageForPhone)
            mqttClientProxyMessageForPhone = service->getMqttClientProxyMessageForPhone();
        if (!clientNotification)
            clientNotification = service->getClientNotificationForPhone();
        bool hasPacket = !!queueStatusPacketForPhone || !!mqttClientProxyMessageForPhone || !!clientNotification;
        if (hasPacket)
            return true;

#ifdef FSCom
        if (xmodemPacketForPhone.control == meshtastic_XModem_Control_NUL)
            xmodemPacketForPhone = xModem.getForPhone();
        if (xmodemPacketForPhone.control != meshtastic_XModem_Control_NUL) {
            xModem.resetForPhone();
            return true;
        }
#endif

#ifdef ARCH_ESP32
#if !MESHTASTIC_EXCLUDE_STOREFORWARD
        // Check if StoreForward has packets stored for us.
        if (!packetForPhone && storeForwardModule)
            packetForPhone = storeForwardModule->getForPhone();
#endif
#endif

        if (!packetForPhone)
            packetForPhone = service->getForPhone();
        hasPacket = !!packetForPhone;
        return hasPacket;
    }
    default:
        LOG_ERROR("PhoneAPI::available unexpected state %d", state);
    }

    return false;
}

void PhoneAPI::sendNotification(meshtastic_LogRecord_Level level, uint32_t replyId, const char *message)
{
    meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
    cn->has_reply_id = true;
    cn->reply_id = replyId;
    cn->level = meshtastic_LogRecord_Level_WARNING;
    cn->time = getValidTime(RTCQualityFromNet);
    strncpy(cn->message, message, sizeof(cn->message));
    service->sendClientNotification(cn);
}

bool PhoneAPI::wasSeenRecently(uint32_t id)
{
    for (int i = 0; i < 20; i++) {
        if (recentToRadioPacketIds[i] == id) {
            return true;
        }
        if (recentToRadioPacketIds[i] == 0) {
            recentToRadioPacketIds[i] = id;
            return false;
        }
    }
    // If the array is full, shift all elements to the left and add the new id at the end
    memmove(recentToRadioPacketIds, recentToRadioPacketIds + 1, (19) * sizeof(uint32_t));
    recentToRadioPacketIds[19] = id;
    return false;
}

/**
 * Handle a packet that the phone wants us to send.  It is our responsibility to free the packet to the pool
 */
bool PhoneAPI::handleToRadioPacket(meshtastic_MeshPacket &p)
{
    printPacket("PACKET FROM PHONE", &p);

#if defined(ARCH_PORTDUINO)
    // For use with the simulator, we should not ignore duplicate packets from the phone
    if (SimRadio::instance == nullptr)
#endif
        if (p.id > 0 && wasSeenRecently(p.id)) {
            LOG_DEBUG("Ignore packet from phone, already seen recently");
            return false;
        }

    if (p.decoded.portnum == meshtastic_PortNum_TRACEROUTE_APP && lastPortNumToRadio[p.decoded.portnum] &&
        Throttle::isWithinTimespanMs(lastPortNumToRadio[p.decoded.portnum], THIRTY_SECONDS_MS)) {
        LOG_WARN("Rate limit portnum %d", p.decoded.portnum);
        sendNotification(meshtastic_LogRecord_Level_WARNING, p.id, "TraceRoute can only be sent once every 30 seconds");
        meshtastic_QueueStatus qs = router->getQueueStatus();
        service->sendQueueStatusToPhone(qs, 0, p.id);
        return false;
    } else if (p.decoded.portnum == meshtastic_PortNum_TRACEROUTE_APP && isBroadcast(p.to) && p.hop_limit > 0) {
        sendNotification(meshtastic_LogRecord_Level_WARNING, p.id, "Multi-hop traceroute to broadcast address is not allowed");
        meshtastic_QueueStatus qs = router->getQueueStatus();
        service->sendQueueStatusToPhone(qs, 0, p.id);
        return false;
    } else if (IS_ONE_OF(p.decoded.portnum, meshtastic_PortNum_POSITION_APP, meshtastic_PortNum_WAYPOINT_APP,
                         meshtastic_PortNum_ALERT_APP, meshtastic_PortNum_TELEMETRY_APP) &&
               lastPortNumToRadio[p.decoded.portnum] &&
               Throttle::isWithinTimespanMs(lastPortNumToRadio[p.decoded.portnum], TEN_SECONDS_MS)) {
        // TODO: [Issue #6700] Make this rate limit throttling scale up / down with the preset
        LOG_WARN("Rate limit portnum %d", p.decoded.portnum);
        meshtastic_QueueStatus qs = router->getQueueStatus();
        service->sendQueueStatusToPhone(qs, 0, p.id);
        // FIXME: Figure out why this continues to happen
        // sendNotification(meshtastic_LogRecord_Level_WARNING, p.id, "Position can only be sent once every 5 seconds");
        return false;
    } else if (p.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && lastPortNumToRadio[p.decoded.portnum] &&
               Throttle::isWithinTimespanMs(lastPortNumToRadio[p.decoded.portnum], TWO_SECONDS_MS)) {
        LOG_WARN("Rate limit portnum %d", p.decoded.portnum);
        meshtastic_QueueStatus qs = router->getQueueStatus();
        service->sendQueueStatusToPhone(qs, 0, p.id);
        service->sendRoutingErrorResponse(meshtastic_Routing_Error_RATE_LIMIT_EXCEEDED, &p);
        // sendNotification(meshtastic_LogRecord_Level_WARNING, p.id, "Text messages can only be sent once every 2 seconds");
        return false;
    }

    // Upgrade traceroute requests from phone to use reliable delivery, matching TraceRouteModule
    if (p.decoded.portnum == meshtastic_PortNum_TRACEROUTE_APP && !isBroadcast(p.to)) {
        // Use reliable delivery for traceroute requests (which will be copied to traceroute responses by setReplyTo)
        p.want_ack = true;
    }

    lastPortNumToRadio[p.decoded.portnum] = millis();
    service->handleToRadio(p);
    return true;
}

/// If the mesh service tells us fromNum has changed, tell the phone
int PhoneAPI::onNotify(uint32_t newValue)
{
    bool timeout = checkConnectionTimeout(); // a handy place to check if we've heard from the phone (since the BLE version
                                             // doesn't call this from idle)

    if (state == STATE_SEND_PACKETS) {
        LOG_INFO("Tell client we have new packets %u", newValue);
        onNowHasData(newValue);
    } else {
        LOG_DEBUG("Client not yet interested in packets (state=%d)", state);
    }

    return timeout ? -1 : 0; // If we timed out, MeshService should stop iterating through observers as we just removed one
}
