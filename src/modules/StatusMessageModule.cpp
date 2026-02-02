#if !MESHTASTIC_EXCLUDE_STATUS

#include "StatusMessageModule.h"
#include "MeshService.h"
#include "ProtobufModule.h"

StatusMessageModule *statusMessageModule;

int32_t StatusMessageModule::runOnce()
{
    if (moduleConfig.has_statusmessage && moduleConfig.statusmessage.node_status[0] != '\0') {
        // create and send message with the status message set
        meshtastic_StatusMessage ourStatus = meshtastic_StatusMessage_init_zero;
        strncpy(ourStatus.status, moduleConfig.statusmessage.node_status, sizeof(ourStatus.status));
        ourStatus.status[sizeof(ourStatus.status) - 1] = '\0'; // ensure null termination
        meshtastic_MeshPacket *p = allocDataPacket();
        p->decoded.payload.size = pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes),
                                                     meshtastic_StatusMessage_fields, &ourStatus);
        p->to = NODENUM_BROADCAST;
        p->decoded.want_response = false;
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        p->channel = 0;
        service->sendToMesh(p);
    }

    return 1000 * 12 * 60 * 60;
}

ProcessMessage StatusMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        meshtastic_StatusMessage incomingMessage = meshtastic_StatusMessage_init_zero;

        if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, meshtastic_StatusMessage_fields,
                                 &incomingMessage)) {

            LOG_INFO("Received a NodeStatus message %s", incomingMessage.status);

            RecentStatus entry;
            entry.fromNodeId = mp.from;
            entry.statusText = incomingMessage.status;

            recentReceived.push_back(std::move(entry));

            // Keep only last MAX_RECENT_STATUSMESSAGES
            if (recentReceived.size() > MAX_RECENT_STATUSMESSAGES) {
                recentReceived.erase(recentReceived.begin()); // drop oldest
            }
        }
    }
    return ProcessMessage::CONTINUE;
}

#endif