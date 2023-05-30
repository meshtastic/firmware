#include "PhoneAPI.h"
#include "Channels.h"
#include "GPS.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RadioInterface.h"
#include "configuration.h"
#include "main.h"
#include "xmodem.h"

#if FromRadio_size > MAX_TO_FROM_RADIO_SIZE
#error FromRadio is too big
#endif

#if ToRadio_size > MAX_TO_FROM_RADIO_SIZE
#error ToRadio is too big
#endif

PhoneAPI::PhoneAPI()
{
    lastContactMsec = millis();
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
        observe(&service.fromNumChanged);
        observe(&xModem.packetReady);
    }

    // even if we were already connected - restart our state machine
    state = STATE_SEND_MY_INFO;

    LOG_INFO("Starting API client config\n");
    nodeInfoForPhone = NULL; // Don't keep returning old nodeinfos
    resetReadIndex();
}

void PhoneAPI::close()
{
    if (state != STATE_SEND_NOTHING) {
        state = STATE_SEND_NOTHING;

        unobserve(&service.fromNumChanged);
        unobserve(&xModem.packetReady);
        releasePhonePacket(); // Don't leak phone packets on shutdown
        releaseQueueStatusPhonePacket();

        onConnectionChanged(false);
    }
}

void PhoneAPI::checkConnectionTimeout()
{
    if (isConnected()) {
        bool newContact = checkIsConnected();
        if (!newContact) {
            LOG_INFO("Lost phone connection\n");
            close();
        }
    }
}

/**
 * Handle a ToRadio protobuf
 */
bool PhoneAPI::handleToRadio(const uint8_t *buf, size_t bufLength)
{
    powerFSM.trigger(EVENT_CONTACT_FROM_PHONE); // As long as the phone keeps talking to us, don't let the radio go to sleep
    lastContactMsec = millis();

    // return (lastContactMsec != 0) &&

    memset(&toRadioScratch, 0, sizeof(toRadioScratch));
    if (pb_decode_from_bytes(buf, bufLength, &meshtastic_ToRadio_msg, &toRadioScratch)) {
        switch (toRadioScratch.which_payload_variant) {
        case meshtastic_ToRadio_packet_tag:
            return handleToRadioPacket(toRadioScratch.packet);
        case meshtastic_ToRadio_want_config_id_tag:
            config_nonce = toRadioScratch.want_config_id;
            LOG_INFO("Client wants config, nonce=%u\n", config_nonce);
            handleStartConfig();
            break;
        case meshtastic_ToRadio_disconnect_tag:
            LOG_INFO("Disconnecting from phone\n");
            close();
            break;
        case meshtastic_ToRadio_xmodemPacket_tag:
            LOG_INFO("Got xmodem packet\n");
            xModem.handlePacket(toRadioScratch.xmodemPacket);
            break;
        default:
            // Ignore nop messages
            // LOG_DEBUG("Error: unexpected ToRadio variant\n");
            break;
        }
    } else {
        LOG_ERROR("Error: ignoring malformed toradio\n");
    }

    return false;
}

/**
 * Get the next packet we want to send to the phone, or NULL if no such packet is available.
 *
 * We assume buf is at least FromRadio_size bytes long.
 *
 * Our sending states progress in the following sequence (the client app ASSUMES THIS SEQUENCE, DO NOT CHANGE IT):
 *      STATE_SEND_MY_INFO, // send our my info record
 *      STATE_SEND_CHANNELS
 *      STATE_SEND_NODEINFO, // states progress in this order as the device sends to the client
        STATE_SEND_CONFIG,
        STATE_SEND_MODULE_CONFIG,
        STATE_SEND_METADATA,
        STATE_SEND_COMPLETE_ID,
        STATE_SEND_PACKETS // send packets or debug strings
 */
size_t PhoneAPI::getFromRadio(uint8_t *buf)
{
    if (!available()) {
        // LOG_DEBUG("getFromRadio=not available\n");
        return 0;
    }
    // In case we send a FromRadio packet
    memset(&fromRadioScratch, 0, sizeof(fromRadioScratch));

    // Advance states as needed
    switch (state) {
    case STATE_SEND_NOTHING:
        LOG_INFO("getFromRadio=STATE_SEND_NOTHING\n");
        break;

    case STATE_SEND_MY_INFO:
        LOG_INFO("getFromRadio=STATE_SEND_MY_INFO\n");
        // If the user has specified they don't want our node to share its location, make sure to tell the phone
        // app not to send locations on our behalf.
        myNodeInfo.has_gps = gps && gps->isConnected(); // Update with latest GPS connect info
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_my_info_tag;
        fromRadioScratch.my_info = myNodeInfo;
        state = STATE_SEND_NODEINFO;

        service.refreshMyNodeInfo(); // Update my NodeInfo because the client will be asking for it soon.
        break;

    case STATE_SEND_NODEINFO: {
        LOG_INFO("getFromRadio=STATE_SEND_NODEINFO\n");
        const meshtastic_NodeInfo *info = nodeInfoForPhone;
        nodeInfoForPhone = NULL; // We just consumed a nodeinfo, will need a new one next time

        if (info) {
            LOG_INFO("Sending nodeinfo: num=0x%x, lastseen=%u, id=%s, name=%s\n", info->num, info->last_heard, info->user.id,
                     info->user.long_name);
            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_node_info_tag;
            fromRadioScratch.node_info = *info;
            // Stay in current state until done sending nodeinfos
        } else {
            LOG_INFO("Done sending nodeinfos\n");
            state = STATE_SEND_CHANNELS;
            // Go ahead and send that ID right now
            return getFromRadio(buf);
        }
        break;
    }

    case STATE_SEND_CHANNELS:
        LOG_INFO("getFromRadio=STATE_SEND_CHANNELS\n");
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_channel_tag;
        fromRadioScratch.channel = channels.getByIndex(config_state);
        config_state++;
        // Advance when we have sent all of our Channels
        if (config_state >= MAX_NUM_CHANNELS) {
            state = STATE_SEND_CONFIG;
            config_state = _meshtastic_AdminMessage_ConfigType_MIN + 1;
        }
        break;

    case STATE_SEND_CONFIG:
        LOG_INFO("getFromRadio=STATE_SEND_CONFIG\n");
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_config_tag;
        switch (config_state) {
        case meshtastic_Config_device_tag:
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_device_tag;
            fromRadioScratch.config.payload_variant.device = config.device;
            break;
        case meshtastic_Config_position_tag:
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_position_tag;
            fromRadioScratch.config.payload_variant.position = config.position;
            break;
        case meshtastic_Config_power_tag:
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_power_tag;
            fromRadioScratch.config.payload_variant.power = config.power;
            fromRadioScratch.config.payload_variant.power.ls_secs = default_ls_secs;
            break;
        case meshtastic_Config_network_tag:
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_network_tag;
            fromRadioScratch.config.payload_variant.network = config.network;
            break;
        case meshtastic_Config_display_tag:
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_display_tag;
            fromRadioScratch.config.payload_variant.display = config.display;
            break;
        case meshtastic_Config_lora_tag:
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_lora_tag;
            fromRadioScratch.config.payload_variant.lora = config.lora;
            break;
        case meshtastic_Config_bluetooth_tag:
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_bluetooth_tag;
            fromRadioScratch.config.payload_variant.bluetooth = config.bluetooth;
            break;
        default:
            LOG_ERROR("Unknown config type %d\n", config_state);
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
        LOG_INFO("getFromRadio=STATE_SEND_MODULECONFIG\n");
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_moduleConfig_tag;
        switch (config_state) {
        case meshtastic_ModuleConfig_mqtt_tag:
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_mqtt_tag;
            fromRadioScratch.moduleConfig.payload_variant.mqtt = moduleConfig.mqtt;
            break;
        case meshtastic_ModuleConfig_serial_tag:
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_serial_tag;
            fromRadioScratch.moduleConfig.payload_variant.serial = moduleConfig.serial;
            break;
        case meshtastic_ModuleConfig_external_notification_tag:
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_external_notification_tag;
            fromRadioScratch.moduleConfig.payload_variant.external_notification = moduleConfig.external_notification;
            break;
        case meshtastic_ModuleConfig_store_forward_tag:
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_store_forward_tag;
            fromRadioScratch.moduleConfig.payload_variant.store_forward = moduleConfig.store_forward;
            break;
        case meshtastic_ModuleConfig_range_test_tag:
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_range_test_tag;
            fromRadioScratch.moduleConfig.payload_variant.range_test = moduleConfig.range_test;
            break;
        case meshtastic_ModuleConfig_telemetry_tag:
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_telemetry_tag;
            fromRadioScratch.moduleConfig.payload_variant.telemetry = moduleConfig.telemetry;
            break;
        case meshtastic_ModuleConfig_canned_message_tag:
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_canned_message_tag;
            fromRadioScratch.moduleConfig.payload_variant.canned_message = moduleConfig.canned_message;
            break;
        case meshtastic_ModuleConfig_audio_tag:
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_audio_tag;
            fromRadioScratch.moduleConfig.payload_variant.audio = moduleConfig.audio;
            break;
        case meshtastic_ModuleConfig_remote_hardware_tag:
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_remote_hardware_tag;
            fromRadioScratch.moduleConfig.payload_variant.remote_hardware = moduleConfig.remote_hardware;
            break;
        default:
            LOG_ERROR("Unknown module config type %d\n", config_state);
        }

        config_state++;
        // Advance when we have sent all of our ModuleConfig objects
        if (config_state > (_meshtastic_AdminMessage_ModuleConfigType_MAX + 1)) {
            state = STATE_SEND_METADATA;
            config_state = 0;
        }
        break;
    case STATE_SEND_METADATA:
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_metadata_tag;
        fromRadioScratch.metadata = getDeviceMetadata();
        state = STATE_SEND_COMPLETE_ID;
        break;
    case STATE_SEND_COMPLETE_ID:
        LOG_INFO("getFromRadio=STATE_SEND_COMPLETE_ID\n");
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
        fromRadioScratch.config_complete_id = config_nonce;
        config_nonce = 0;
        state = STATE_SEND_PACKETS;
        break;

    case STATE_SEND_PACKETS:
        // Do we have a message from the mesh?
        LOG_INFO("getFromRadio=STATE_SEND_PACKETS\n");
        if (queueStatusPacketForPhone) {
            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
            fromRadioScratch.queueStatus = *queueStatusPacketForPhone;
            releaseQueueStatusPhonePacket();
        } else if (xmodemPacketForPhone.control != meshtastic_XModem_Control_NUL) {
            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_xmodemPacket_tag;
            fromRadioScratch.xmodemPacket = xmodemPacketForPhone;
            xmodemPacketForPhone = meshtastic_XModem_init_zero;
        } else if (packetForPhone) {
            printPacket("phone downloaded packet", packetForPhone);

            // Encapsulate as a FromRadio packet
            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_packet_tag;
            fromRadioScratch.packet = *packetForPhone;
            releasePhonePacket();
        }
        break;

    default:
        LOG_ERROR("getFromRadio unexpected state %d\n", state);
    }

    // Do we have a message from the mesh?
    if (fromRadioScratch.which_payload_variant != 0) {
        // Encapsulate as a FromRadio packet
        size_t numbytes = pb_encode_to_bytes(buf, meshtastic_FromRadio_size, &meshtastic_FromRadio_msg, &fromRadioScratch);

        LOG_DEBUG("encoding toPhone packet to phone variant=%d, %d bytes\n", fromRadioScratch.which_payload_variant, numbytes);
        return numbytes;
    }

    LOG_DEBUG("no FromRadio packet available\n");
    return 0;
}

void PhoneAPI::handleDisconnect()
{
    LOG_INFO("PhoneAPI disconnect\n");
}

void PhoneAPI::releasePhonePacket()
{
    if (packetForPhone) {
        service.releaseToPool(packetForPhone); // we just copied the bytes, so don't need this buffer anymore
        packetForPhone = NULL;
    }
}

void PhoneAPI::releaseQueueStatusPhonePacket()
{
    if (queueStatusPacketForPhone) {
        service.releaseQueueStatusToPool(queueStatusPacketForPhone);
        queueStatusPacketForPhone = NULL;
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
    case STATE_SEND_CHANNELS:
    case STATE_SEND_CONFIG:
    case STATE_SEND_MODULECONFIG:
    case STATE_SEND_METADATA:
    case STATE_SEND_COMPLETE_ID:
        return true;

    case STATE_SEND_NODEINFO:
        if (!nodeInfoForPhone)
            nodeInfoForPhone = nodeDB.readNextInfo(readIndex);
        return true; // Always say we have something, because we might need to advance our state machine

    case STATE_SEND_PACKETS: {
        if (!queueStatusPacketForPhone)
            queueStatusPacketForPhone = service.getQueueStatusForPhone();
        bool hasPacket = !!queueStatusPacketForPhone;
        if (hasPacket)
            return true;

        if (xmodemPacketForPhone.control == meshtastic_XModem_Control_NUL)
            xmodemPacketForPhone = xModem.getForPhone();
        if (xmodemPacketForPhone.control != meshtastic_XModem_Control_NUL) {
            xModem.resetForPhone();
            return true;
        }

        if (!packetForPhone)
            packetForPhone = service.getForPhone();
        hasPacket = !!packetForPhone;
        // LOG_DEBUG("available hasPacket=%d\n", hasPacket);
        return hasPacket;
    }
    default:
        LOG_ERROR("PhoneAPI::available unexpected state %d\n", state);
    }

    return false;
}

/**
 * Handle a packet that the phone wants us to send.  It is our responsibility to free the packet to the pool
 */
bool PhoneAPI::handleToRadioPacket(meshtastic_MeshPacket &p)
{
    printPacket("PACKET FROM PHONE", &p);
    service.handleToRadio(p);

    return true;
}

/// If the mesh service tells us fromNum has changed, tell the phone
int PhoneAPI::onNotify(uint32_t newValue)
{
    checkConnectionTimeout(); // a handy place to check if we've heard from the phone (since the BLE version doesn't call this
                              // from idle)

    if (state == STATE_SEND_PACKETS) {
        LOG_INFO("Telling client we have new packets %u\n", newValue);
        onNowHasData(newValue);
    } else {
        LOG_DEBUG("(Client not yet interested in packets)\n");
    }

    return 0;
}
