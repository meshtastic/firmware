#pragma once

#include <cstddef>
#include <cstdint>

/**
 * External-routing-hint receiver. Accepts JSON recommendations of the form
 *   { "for_destination": "!hexhex", "use_next_hop": "!hexhex", ... }
 * and writes the hint into NodeDB so the existing NextHopRouter picks it up.
 *
 * Wired into MQTT.cpp's receive path; not a MeshModule (no portnum), since
 * the message arrives directly off the broker on a custom topic.
 */
class RoutingHintModule
{
  public:
    static bool handleRecommendation(const uint8_t *payload, size_t length);
};
