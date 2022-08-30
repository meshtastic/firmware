#include "PhoneAPI.h"
#include "Channels.h"
#include "GPS.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RadioInterface.h"
#include "configuration.h"
#include <assert.h>

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
    }

    // even if we were already connected - restart our state machine
    state = STATE_SEND_MY_INFO;

    DEBUG_MSG("Starting API client config\n");
    nodeInfoForPhone = NULL;   // Don't keep returning old nodeinfos
    nodeDB.resetReadPointer(); // FIXME, this read pointer should be moved out of nodeDB and into this class - because
                               // this will break once we have multiple instances of PhoneAPI running independently
}

void PhoneAPI::close()
{
    if (state != STATE_SEND_NOTHING) {
        state = STATE_SEND_NOTHING;

        unobserve(&service.fromNumChanged);
        releasePhonePacket(); // Don't leak phone packets on shutdown

        onConnectionChanged(false);
    }
}

void PhoneAPI::checkConnectionTimeout()
{
    if (isConnected()) {
        bool newContact = checkIsConnected();
        if (!newContact) {
            DEBUG_MSG("Lost phone connection\n");
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
    if (pb_decode_from_bytes(buf, bufLength, ToRadio_fields, &toRadioScratch)) {
        switch (toRadioScratch.which_payloadVariant) {
            case ToRadio_packet_tag:
                return handleToRadioPacket(toRadioScratch.packet);
            case ToRadio_want_config_id_tag:
                config_nonce = toRadioScratch.want_config_id;
                DEBUG_MSG("Client wants config, nonce=%u\n", config_nonce);
                handleStartConfig();
                break;
            case ToRadio_disconnect_tag:
                DEBUG_MSG("Disconnecting from phone\n");
                close();
                break;
        default:
            // Ignore nop messages
            // DEBUG_MSG("Error: unexpected ToRadio variant\n");
            break;
        }
    } else {
        DEBUG_MSG("Error: ignoring malformed toradio\n");
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
 *      STATE_SEND_NODEINFO, // states progress in this order as the device sends to the client
        STATE_SEND_CONFIG,
        STATE_SEND_COMPLETE_ID,
        STATE_SEND_PACKETS // send packets or debug strings
 */
size_t PhoneAPI::getFromRadio(uint8_t *buf)
{
    if (!available()) {
        DEBUG_MSG("getFromRadio=not available\n");
        return 0;
    }
    // In case we send a FromRadio packet
    memset(&fromRadioScratch, 0, sizeof(fromRadioScratch));

    // Advance states as needed
    switch (state) {
    case STATE_SEND_NOTHING:
        DEBUG_MSG("getFromRadio=STATE_SEND_NOTHING\n");
        break;
        
    case STATE_SEND_MY_INFO:
        DEBUG_MSG("getFromRadio=STATE_SEND_MY_INFO\n");
        // If the user has specified they don't want our node to share its location, make sure to tell the phone
        // app not to send locations on our behalf.
        myNodeInfo.has_gps = gps && gps->isConnected(); // Update with latest GPS connect info
        fromRadioScratch.which_payloadVariant = FromRadio_my_info_tag;
        fromRadioScratch.my_info = myNodeInfo;
        state = STATE_SEND_NODEINFO;

        service.refreshMyNodeInfo(); // Update my NodeInfo because the client will be asking for it soon.
        break;

    case STATE_SEND_NODEINFO: {
        DEBUG_MSG("getFromRadio=STATE_SEND_NODEINFO\n");
        const NodeInfo *info = nodeInfoForPhone;
        nodeInfoForPhone = NULL; // We just consumed a nodeinfo, will need a new one next time

        if (info) {
            DEBUG_MSG("Sending nodeinfo: num=0x%x, lastseen=%u, id=%s, name=%s\n", info->num, info->last_heard, info->user.id,
                      info->user.long_name);
            fromRadioScratch.which_payloadVariant = FromRadio_node_info_tag;
            fromRadioScratch.node_info = *info;
            // Stay in current state until done sending nodeinfos
        } else {
            DEBUG_MSG("Done sending nodeinfos\n");
            state = STATE_SEND_CONFIG;
            // Go ahead and send that ID right now
            return getFromRadio(buf);
        }
        break;
    }

    case STATE_SEND_CONFIG:
        DEBUG_MSG("getFromRadio=STATE_SEND_CONFIG\n");
        fromRadioScratch.which_payloadVariant = FromRadio_config_tag;
        switch (config_state) {
            case Config_device_tag:
                fromRadioScratch.config.which_payloadVariant = Config_device_tag;
                fromRadioScratch.config.payloadVariant.device = config.device;
                break;
            case Config_position_tag:
                fromRadioScratch.config.which_payloadVariant = Config_position_tag;
                fromRadioScratch.config.payloadVariant.position = config.position;
                break;
            case Config_power_tag:
                fromRadioScratch.config.which_payloadVariant = Config_power_tag;
                fromRadioScratch.config.payloadVariant.power = config.power;
                fromRadioScratch.config.payloadVariant.power.ls_secs = default_ls_secs;
                break;
            case Config_wifi_tag:
                fromRadioScratch.config.which_payloadVariant = Config_wifi_tag;
                fromRadioScratch.config.payloadVariant.wifi = config.wifi;
                break;
            case Config_display_tag:
                fromRadioScratch.config.which_payloadVariant = Config_display_tag;
                fromRadioScratch.config.payloadVariant.display = config.display;
                break;
            case Config_lora_tag:
                fromRadioScratch.config.which_payloadVariant = Config_lora_tag;
                fromRadioScratch.config.payloadVariant.lora = config.lora;
                break;
            case Config_bluetooth_tag:
                fromRadioScratch.config.which_payloadVariant = Config_bluetooth_tag;
                fromRadioScratch.config.payloadVariant.bluetooth = config.bluetooth;
                break;
        }
        // NOTE: The phone app needs to know the ls_secs value so it can properly expect sleep behavior.
        // So even if we internally use 0 to represent 'use default' we still need to send the value we are
        // using to the app (so that even old phone apps work with new device loads).
        
        config_state++;
        // Advance when we have sent all of our config objects
        if (config_state > Config_bluetooth_tag) {
            state = STATE_SEND_MODULECONFIG;
            config_state = ModuleConfig_mqtt_tag;
        }
        break;

    case STATE_SEND_MODULECONFIG:
        DEBUG_MSG("getFromRadio=STATE_SEND_MODULECONFIG\n");
        fromRadioScratch.which_payloadVariant = FromRadio_moduleConfig_tag;
        switch (config_state) {
            case ModuleConfig_mqtt_tag:
                fromRadioScratch.moduleConfig.which_payloadVariant = ModuleConfig_mqtt_tag;
                fromRadioScratch.moduleConfig.payloadVariant.mqtt = moduleConfig.mqtt;
                break;
            case ModuleConfig_serial_tag:
                fromRadioScratch.moduleConfig.which_payloadVariant = ModuleConfig_serial_tag;
                fromRadioScratch.moduleConfig.payloadVariant.serial = moduleConfig.serial;
                break;
            case ModuleConfig_external_notification_tag:
                fromRadioScratch.moduleConfig.which_payloadVariant = ModuleConfig_external_notification_tag;
                fromRadioScratch.moduleConfig.payloadVariant.external_notification = moduleConfig.external_notification;
                break;
            case ModuleConfig_range_test_tag:
                fromRadioScratch.moduleConfig.which_payloadVariant = ModuleConfig_range_test_tag;
                fromRadioScratch.moduleConfig.payloadVariant.range_test = moduleConfig.range_test;
                break;
            case ModuleConfig_telemetry_tag:
                fromRadioScratch.moduleConfig.which_payloadVariant = ModuleConfig_telemetry_tag;
                fromRadioScratch.moduleConfig.payloadVariant.telemetry = moduleConfig.telemetry;
                break;
            case ModuleConfig_canned_message_tag:
                fromRadioScratch.moduleConfig.which_payloadVariant = ModuleConfig_canned_message_tag;
                fromRadioScratch.moduleConfig.payloadVariant.canned_message = moduleConfig.canned_message;
                break;
        }

        config_state++;
        // Advance when we have sent all of our ModuleConfig objects
        if (config_state > ModuleConfig_canned_message_tag) {
            state = STATE_SEND_COMPLETE_ID;
            config_state = Config_device_tag;
        }
        break;

    case STATE_SEND_COMPLETE_ID:
        DEBUG_MSG("getFromRadio=STATE_SEND_COMPLETE_ID\n");
        fromRadioScratch.which_payloadVariant = FromRadio_config_complete_id_tag;
        fromRadioScratch.config_complete_id = config_nonce;
        config_nonce = 0;
        state = STATE_SEND_PACKETS;
        break;

    case STATE_SEND_PACKETS:
        // Do we have a message from the mesh?
        DEBUG_MSG("getFromRadio=STATE_SEND_PACKETS\n");
        if (packetForPhone) {
            printPacket("phone downloaded packet", packetForPhone);

            // Encapsulate as a FromRadio packet
            fromRadioScratch.which_payloadVariant = FromRadio_packet_tag;
            fromRadioScratch.packet = *packetForPhone;
        }
        releasePhonePacket();
        break;

    default:
        assert(0); // unexpected state - FIXME, make an error code and reboot
    }

    // Do we have a message from the mesh?
    if (fromRadioScratch.which_payloadVariant != 0) {
        // Encapsulate as a FromRadio packet
        size_t numbytes = pb_encode_to_bytes(buf, FromRadio_size, FromRadio_fields, &fromRadioScratch);
        // DEBUG_MSG("encoding toPhone packet to phone variant=%d, %d bytes\n", fromRadioScratch.which_payloadVariant, numbytes);
        return numbytes;
    }

    DEBUG_MSG("no FromRadio packet available\n");
    return 0;
}

void PhoneAPI::handleDisconnect() 
{
    DEBUG_MSG("PhoneAPI disconnect\n");
}

void PhoneAPI::releasePhonePacket()
{
    if (packetForPhone) {
        service.releaseToPool(packetForPhone); // we just copied the bytes, so don't need this buffer anymore
        packetForPhone = NULL;
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
            return true;
        case STATE_SEND_CONFIG:
            return true;        
        case STATE_SEND_MODULECONFIG:
            return true;
        case STATE_SEND_NODEINFO:
            if (!nodeInfoForPhone)
                nodeInfoForPhone = nodeDB.readNextInfo();
            return true; // Always say we have something, because we might need to advance our state machine
        case STATE_SEND_COMPLETE_ID:
            return true;
        case STATE_SEND_PACKETS: {
            if (!packetForPhone)
                packetForPhone = service.getForPhone();
            bool hasPacket = !!packetForPhone;
            DEBUG_MSG("available hasPacket=%d\n", hasPacket);
            return hasPacket;
    }
    default:
        assert(0); // unexpected state - FIXME, make an error code and reboot
    }

    return false;
}

/**
 * Handle a packet that the phone wants us to send.  It is our responsibility to free the packet to the pool
 */
bool PhoneAPI::handleToRadioPacket(MeshPacket &p)
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
        DEBUG_MSG("Telling client we have new packets %u\n", newValue);
        onNowHasData(newValue);
    } else
        DEBUG_MSG("(Client not yet interested in packets)\n");

    return 0;
}
