#include "modules/RoutingHintModule.h"

#include "NodeDB.h"
#include "configuration.h"
#include "serialization/JSON.h"
#include <cstdlib>
#include <memory>
#include <string>

static uint32_t parseNodeIdString(const std::string &s)
{
    // Accept "!hexhex" or "hexhex" — node IDs in our contract use the "!"-prefixed form.
    const char *cs = s.c_str();
    if (*cs == '!')
        cs++;
    return (uint32_t)strtoul(cs, nullptr, 16);
}

bool RoutingHintModule::handleRecommendation(const uint8_t *payload, size_t length)
{
    std::string raw(reinterpret_cast<const char *>(payload), length);
    std::unique_ptr<JSONValue> value(JSON::Parse(raw.c_str()));
    if (!value || !value->IsObject()) {
        LOG_WARN("[RoutingHint] payload is not a JSON object: %s", raw.c_str());
        return false;
    }

    JSONObject obj = value->AsObject();
    if (obj.find("for_destination") == obj.end() || !obj["for_destination"]->IsString() ||
        obj.find("use_next_hop") == obj.end() || !obj["use_next_hop"]->IsString()) {
        LOG_WARN("[RoutingHint] missing required fields for_destination / use_next_hop");
        return false;
    }

    uint32_t destNum = parseNodeIdString(obj["for_destination"]->AsString());
    uint32_t hopNum = parseNodeIdString(obj["use_next_hop"]->AsString());
    if (destNum == 0 || hopNum == 0) {
        LOG_WARN("[RoutingHint] zero node id parsed (dest=0x%x hop=0x%x)", destNum, hopNum);
        return false;
    }

    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(destNum);
    if (!node) {
        // Destination not yet known to this node's NodeDB — can't set a
        // next-hop hint without an existing entry. ML side should retry once
        // NodeInfo from the destination has been observed.
        LOG_WARN("[RoutingHint] node 0x%x unknown to NodeDB, skipping", destNum);
        return false;
    }
    uint8_t prev = node->next_hop;
    node->next_hop = (uint8_t)(hopNum & 0xff);
    LOG_INFO("[RoutingHint] dest=0x%x next_hop %u -> %u (use_next_hop=0x%x)", destNum, prev, node->next_hop, hopNum);
    return true;
}
