#include "PhoneAPI.h"
#include <assert.h>

PhoneAPI::PhoneAPI()
{
    // Make sure that we never let our packets grow too large for one BLE packet
    assert(FromRadio_size <= 512);
    assert(ToRadio_size <= 512);
}

/**
 * Handle a ToRadio protobuf
 */
void PhoneAPI::handleToRadio(const char *buf, size_t len)
{
    // FIXME
}

/**
 * Get the next packet we want to send to the phone, or NULL if no such packet is available.
 *
 * We assume buf is at least FromRadio_size bytes long.
 */
bool PhoneAPI::getFromRadio(char *buf)
{
    return false; // FIXME
}

/**
 * Return true if we have data available to send to the phone
 */
bool PhoneAPI::available()
{
    return true; // FIXME
}

//
// The following routines are only public for now - until the rev1 bluetooth API is removed
//

void PhoneAPI::handleSetOwner(const User &o) {}

void PhoneAPI::handleSetRadio(const RadioConfig &r) {}

/**
 * The client wants to start a new set of config reads
 */
void PhoneAPI::handleWantConfig(uint32_t nonce) {}

/**
 * Handle a packet that the phone wants us to send.  It is our responsibility to free the packet to the pool
 */
void PhoneAPI::handleToRadioPacket(MeshPacket *p) {}