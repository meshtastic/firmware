#include "DMRelayModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"

#ifndef DM_RELAY_HOP_LIMIT
#define DM_RELAY_HOP_LIMIT 1
#endif

DMRelayModule *dmRelayModule;

ProcessMessage DMRelayModule::handleReceived(const meshtastic_MeshPacket &mp) {
    auto &p = mp.decoded;

    // Filter by Type: Only process TXT messages (SinglePortModule already filters this, but just to be sure)
    if (p.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP) {
        return ProcessMessage::CONTINUE;
    }

    // Verify DM Status: Intended for this specific node
    if (mp.to != nodeDB->getNodeNum()) {
        return ProcessMessage::CONTINUE;
    }

    // Identify the Sender
    meshtastic_NodeInfoLite* node = nodeDB->getMeshNode(mp.from);

    // Null Pointer Check (CRITICAL)
    if (!node) {
        return ProcessMessage::CONTINUE;
    }

    // Verify Favorite Status
    if (node->is_favorite) {
        // Create a new meshtastic_MeshPacket
        meshtastic_MeshPacket* rebroadcast = allocDataPacket();
        if(!rebroadcast) {
            return ProcessMessage::CONTINUE;
        }

        // Set the destination to NODENUM_BROADCAST
        rebroadcast->to = NODENUM_BROADCAST;
        // Set channel = 0 (Primary)
        rebroadcast->channel = 0;
        // Set hop_limit to the configured build limit
        rebroadcast->hop_limit = DM_RELAY_HOP_LIMIT;
        rebroadcast->want_ack = false;

        // Extract the exact text payload from the incoming packet
        rebroadcast->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        rebroadcast->decoded.payload.size = p.payload.size;
        memcpy(rebroadcast->decoded.payload.bytes, p.payload.bytes, p.payload.size);

        // Inject the new packet into the router using router->send()
        router->send(rebroadcast);
    }

    // Return State: Return CONTINUE so that the original DM displays normally on local interface
    return ProcessMessage::CONTINUE;
}
