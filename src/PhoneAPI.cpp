#include "PhoneAPI.h"
#include "MeshService.h"
#include "NodeDB.h"
#include <assert.h>

PhoneAPI::PhoneAPI()
{
    // Make sure that we never let our packets grow too large for one BLE packet
    assert(FromRadio_size <= 512);
    assert(ToRadio_size <= 512);
}

void PhoneAPI::init()
{
    observe(&service.fromNumChanged);
}

/**
 * Handle a ToRadio protobuf
 */
void PhoneAPI::handleToRadio(const uint8_t *buf, size_t bufLength)
{
    if (pb_decode_from_bytes(buf, bufLength, ToRadio_fields, &toRadioScratch)) {
        switch (toRadioScratch.which_variant) {
        case ToRadio_packet_tag: {
            // If our phone is sending a position, see if we can use it to set our RTC
            MeshPacket &p = toRadioScratch.variant.packet;
            service.handleToRadio(p);
            break;
        }
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
 */
size_t PhoneAPI::getFromRadio(uint8_t *buf)
{
    if (!available())
        return false;

    // Do we have a message from the mesh?
    if (packetForPhone) {
        // Encapsulate as a FromRadio packet
        memset(&fromRadioScratch, 0, sizeof(fromRadioScratch));
        fromRadioScratch.which_variant = FromRadio_packet_tag;
        fromRadioScratch.variant.packet = *packetForPhone;

        size_t numbytes = pb_encode_to_bytes(buf, sizeof(FromRadio_size), FromRadio_fields, &fromRadioScratch);
        DEBUG_MSG("delivering toPhone packet to phone %d bytes\n", numbytes);

        service.releaseToPool(packetForPhone); // we just copied the bytes, so don't need this buffer anymore
        packetForPhone = NULL;
        return numbytes;
    }

    DEBUG_MSG("toPhone queue is empty\n");
    return 0;
}

/**
 * Return true if we have data available to send to the phone
 */
bool PhoneAPI::available()
{
    packetForPhone = service.getForPhone();

    return true; // FIXME
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

    service.reloadConfig();
}

/**
 * The client wants to start a new set of config reads
 */
void PhoneAPI::handleWantConfig(uint32_t nonce) {}

/**
 * Handle a packet that the phone wants us to send.  It is our responsibility to free the packet to the pool
 */
void PhoneAPI::handleToRadioPacket(MeshPacket *p) {}

/// If the mesh service tells us fromNum has changed, tell the phone
int PhoneAPI::onNotify(uint32_t newValue)
{
    onNowHasData(newValue);
    return 0;
}