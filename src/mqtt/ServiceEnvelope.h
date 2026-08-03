#pragma once

#include "mesh/generated/meshtastic/mqtt.pb.h"

// meshtastic_ServiceEnvelope that automatically releases dynamically allocated memory when it goes out of scope.
struct DecodedServiceEnvelope : public meshtastic_ServiceEnvelope {
    DecodedServiceEnvelope(const uint8_t *payload, size_t length);
    // const-qualified so std::variant instantiation works on Apple libc++ (copying stays ill-formed either way)
    DecodedServiceEnvelope(const DecodedServiceEnvelope &) = delete;
    DecodedServiceEnvelope(DecodedServiceEnvelope &&);
    ~DecodedServiceEnvelope();
    // Clients must check that this is true before using.
    const bool validDecode;
};