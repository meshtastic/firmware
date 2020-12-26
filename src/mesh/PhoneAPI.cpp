#include "PhoneAPI.h"
#include "GPS.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RadioInterface.h"
#include <assert.h>

#if FromRadio_size > MAX_TO_FROM_RADIO_SIZE
    #error FromRadio is too big
#endif

#if ToRadio_size > MAX_TO_FROM_RADIO_SIZE
    #error ToRadio is too big
#endif

PhoneAPI::PhoneAPI()
{
}

void PhoneAPI::init()
{
    observe(&service.fromNumChanged);
}

void PhoneAPI::checkConnectionTimeout()
{
    if (isConnected) {
        bool newConnected = (millis() - lastContactMsec < getPref_phone_timeout_secs() * 1000L);
        if (!newConnected) {
            isConnected = false;
            onConnectionChanged(isConnected);
        }
    }
}

/**
 * Handle a ToRadio protobuf
 */
void PhoneAPI::handleToRadio(const uint8_t *buf, size_t bufLength)
{
    powerFSM.trigger(EVENT_CONTACT_FROM_PHONE); // As long as the phone keeps talking to us, don't let the radio go to sleep
    lastContactMsec = millis();
    if (!isConnected) {
        isConnected = true;
        onConnectionChanged(isConnected);
    }
    // return (lastContactMsec != 0) &&

    if (pb_decode_from_bytes(buf, bufLength, ToRadio_fields, &toRadioScratch)) {
        switch (toRadioScratch.which_variant) {
        case ToRadio_packet_tag: {
            MeshPacket &p = toRadioScratch.variant.packet;
            printPacket("PACKET FROM PHONE", &p);
            service.handleToRadio(p);
            break;
        }
        case ToRadio_want_config_id_tag:
            config_nonce = toRadioScratch.variant.want_config_id;
            DEBUG_MSG("Client wants config, nonce=%u\n", config_nonce);
            state = STATE_SEND_MY_INFO;

            DEBUG_MSG("Reset nodeinfo read pointer\n");
            nodeInfoForPhone = NULL;   // Don't keep returning old nodeinfos
            nodeDB.resetReadPointer(); // FIXME, this read pointer should be moved out of nodeDB and into this class - because
                                       // this will break once we have multiple instances of PhoneAPI running independently
            break;

        case ToRadio_set_owner_tag:
            DEBUG_MSG("Client is setting owner\n");
            handleSetOwner(toRadioScratch.variant.set_owner);
            break;

        case ToRadio_set_radio_tag:
            DEBUG_MSG("Client is setting radio\n");
            handleSetRadio(toRadioScratch.variant.set_radio);
            break;

        default:
            DEBUG_MSG("Error: unexpected ToRadio variant\n");
            break;
        }
    } else {
        DEBUG_MSG("Error: ignoring malformed toradio\n");
    }
}

/**
 * Get the next packet we want to send to the phone, or NULL if no such packet is available.
 *
 * We assume buf is at least FromRadio_size bytes long.
 *
 * Our sending states progress in the following sequence (the client app ASSUMES THIS SEQUENCE, DO NOT CHANGE IT):
 *      STATE_SEND_MY_INFO, // send our my info record
        STATE_SEND_RADIO,
        STATE_SEND_NODEINFO, // states progress in this order as the device sends to to the client
        STATE_SEND_COMPLETE_ID,
        STATE_SEND_PACKETS // send packets or debug strings
 */
size_t PhoneAPI::getFromRadio(uint8_t *buf)
{
    if (!available()) {
        // DEBUG_MSG("getFromRadio, !available\n");
        return 0;
    } 

    DEBUG_MSG("getFromRadio, state=%d\n", state);

    // In case we send a FromRadio packet
    memset(&fromRadioScratch, 0, sizeof(fromRadioScratch));

    // Advance states as needed
    switch (state) {
    case STATE_SEND_NOTHING:
        break;

    case STATE_SEND_MY_INFO:
        // If the user has specified they don't want our node to share its location, make sure to tell the phone
        // app not to send locations on our behalf.
        myNodeInfo.has_gps = (radioConfig.preferences.location_share == LocationSharing_LocDisabled)
                                 ? true
                                 : (gps && gps->isConnected()); // Update with latest GPS connect info
        fromRadioScratch.which_variant = FromRadio_my_info_tag;
        fromRadioScratch.variant.my_info = myNodeInfo;
        state = STATE_SEND_RADIO;
        break;

    case STATE_SEND_RADIO:
        fromRadioScratch.which_variant = FromRadio_radio_tag;

        fromRadioScratch.variant.radio = radioConfig;

        // NOTE: The phone app needs to know the ls_secs value so it can properly expect sleep behavior.
        // So even if we internally use 0 to represent 'use default' we still need to send the value we are
        // using to the app (so that even old phone apps work with new device loads).
        fromRadioScratch.variant.radio.preferences.ls_secs = getPref_ls_secs();

        state = STATE_SEND_NODEINFO;
        break;

    case STATE_SEND_NODEINFO: {
        const NodeInfo *info = nodeInfoForPhone;
        nodeInfoForPhone = NULL; // We just consumed a nodeinfo, will need a new one next time

        if (info) {
            DEBUG_MSG("Sending nodeinfo: num=0x%x, lastseen=%u, id=%s, name=%s\n", info->num, info->position.time, info->user.id,
                      info->user.long_name);
            fromRadioScratch.which_variant = FromRadio_node_info_tag;
            fromRadioScratch.variant.node_info = *info;
            // Stay in current state until done sending nodeinfos
        } else {
            DEBUG_MSG("Done sending nodeinfos\n");
            state = STATE_SEND_COMPLETE_ID;
            // Go ahead and send that ID right now
            return getFromRadio(buf);
        }
        break;
    }

    case STATE_SEND_COMPLETE_ID:
        fromRadioScratch.which_variant = FromRadio_config_complete_id_tag;
        fromRadioScratch.variant.config_complete_id = config_nonce;
        config_nonce = 0;
        state = STATE_SEND_PACKETS;
        break;

    case STATE_LEGACY: // Treat as the same as send packets
    case STATE_SEND_PACKETS:
        // Do we have a message from the mesh?
        if (packetForPhone) {

            printPacket("phone downloaded packet", packetForPhone);
            
            // Encapsulate as a FromRadio packet
            fromRadioScratch.which_variant = FromRadio_packet_tag;
            fromRadioScratch.variant.packet = *packetForPhone;

            service.releaseToPool(packetForPhone); // we just copied the bytes, so don't need this buffer anymore
            packetForPhone = NULL;
        }
        break;

    default:
        assert(0); // unexpected state - FIXME, make an error code and reboot
    }

    // Do we have a message from the mesh?
    if (fromRadioScratch.which_variant != 0) {
        // Encapsulate as a FromRadio packet
        DEBUG_MSG("encoding toPhone packet to phone variant=%d", fromRadioScratch.which_variant);
        size_t numbytes = pb_encode_to_bytes(buf, FromRadio_size, FromRadio_fields, &fromRadioScratch);
        DEBUG_MSG(", %d bytes\n", numbytes);
        return numbytes;
    }

    DEBUG_MSG("no FromRadio packet available\n");
    return 0;
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

    case STATE_SEND_NODEINFO:
        if (!nodeInfoForPhone)
            nodeInfoForPhone = nodeDB.readNextInfo();
        return true; // Always say we have something, because we might need to advance our state machine

    case STATE_SEND_RADIO:
        return true;

    case STATE_SEND_COMPLETE_ID:
        return true;

    case STATE_LEGACY: // Treat as the same as send packets
    case STATE_SEND_PACKETS: {
        // Try to pull a new packet from the service (if we haven't already)
        if (!packetForPhone)
            packetForPhone = service.getForPhone();
        bool hasPacket = !!packetForPhone;
        // DEBUG_MSG("available hasPacket=%d\n", hasPacket);
        return hasPacket;
    }

    default:
        assert(0); // unexpected state - FIXME, make an error code and reboot
    }

    return false;
}

//
// The following routines are only public for now - until the rev1 bluetooth API is removed
//

void PhoneAPI::handleSetOwner(const User &o)
{
    int changed = 0;

    if (*o.long_name) {
        changed |= strcmp(owner.long_name, o.long_name);
        strcpy(owner.long_name, o.long_name);
    }
    if (*o.short_name) {
        changed |= strcmp(owner.short_name, o.short_name);
        strcpy(owner.short_name, o.short_name);
    }
    if (*o.id) {
        changed |= strcmp(owner.id, o.id);
        strcpy(owner.id, o.id);
    }

    if (changed) // If nothing really changed, don't broadcast on the network or write to flash
        service.reloadOwner();
}

void PhoneAPI::handleSetRadio(const RadioConfig &r)
{
    radioConfig = r;

    bool didReset = service.reloadConfig();
    if (didReset) {
        state = STATE_SEND_MY_INFO; // Squirt a completely new set of configs to the client
    }
}

/**
 * Handle a packet that the phone wants us to send.  It is our responsibility to free the packet to the pool
 */
void PhoneAPI::handleToRadioPacket(MeshPacket *p) {}

/// If the mesh service tells us fromNum has changed, tell the phone
int PhoneAPI::onNotify(uint32_t newValue)
{
    checkConnectionTimeout(); // a handy place to check if we've heard from the phone (since the BLE version doesn't call this
                              // from idle)

    if (state == STATE_SEND_PACKETS || state == STATE_LEGACY) {
        DEBUG_MSG("Telling client we have new packets %u\n", newValue);
        onNowHasData(newValue);
    } else
        DEBUG_MSG("(Client not yet interested in packets)\n");

    return 0;
}