#include "SignalReplyModule.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"

SignalReplyModule *signalReplyModule;

ProcessMessage SignalReplyModule::handleReceived(const meshtastic_MeshPacket &currentRequest)
{
    auto &p = currentRequest.decoded;
    char messageRequest[250];
    for (size_t i = 0; i < p.payload.size; ++i)
    {
        messageRequest[i] = static_cast<char>(p.payload.bytes[i]);
    }
    messageRequest[p.payload.size] = '\0';

    //Received text msg from=0x0, id=0xf50fd53d, msg=Ping
    if ((strcasestr(messageRequest, const_cast<char *>("ping")) == 0 ||
         strcasestr(messageRequest, const_cast<char *>("seq ")) == 0) &&
         currentRequest.from != 0x0 &&  //fix 2025-05-08
         currentRequest.from != nodeDB->getNodeNum())
    {
        int hopLimit = currentRequest.hop_limit;
        int hopStart = currentRequest.hop_start;

        char idSender[10];
        char idReceipient[10];
        snprintf(idSender, sizeof(idSender), "%d", currentRequest.from);
        snprintf(idReceipient, sizeof(idReceipient), "%d", nodeDB->getNodeNum());

        char messageReply[250];
        meshtastic_NodeInfoLite *nodeSender = nodeDB->getMeshNode(currentRequest.from);
        const char *username = nodeSender->has_user ? nodeSender->user.short_name : idSender;
        meshtastic_NodeInfoLite *nodeReceiver = nodeDB->getMeshNode(nodeDB->getNodeNum());
        const char *usernameja = nodeReceiver->has_user ? nodeReceiver->user.short_name : idReceipient;

        LOG_ERROR("SignalReplyModule::handleReceived(): '%s' from %s.", messageRequest, username);

        if (hopLimit != hopStart)
        {
            snprintf(messageReply, sizeof(messageReply), "%s: RSSI/SNR cannot be determined due to indirect connection through %d nodes!", username, (hopLimit - hopStart));
        }
        else
        {
            snprintf(messageReply, sizeof(messageReply), "Request '%s'->'%s' : RSSI %d dBm, SNR %.1f dB (@%s).", username, usernameja, currentRequest.rx_rssi, currentRequest.rx_snr, usernameja);
        }

        auto reply = allocDataPacket();
        reply->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        reply->decoded.payload.size = strlen(messageReply);
        reply->from = getFrom(&currentRequest);
        reply->to = currentRequest.from;
        reply->channel = currentRequest.channel;
        reply->want_ack = (currentRequest.from != 0) ? currentRequest.want_ack : false;
        if (currentRequest.priority == meshtastic_MeshPacket_Priority_UNSET)
        {
            reply->priority = meshtastic_MeshPacket_Priority_RELIABLE;
        }
        reply->id = generatePacketId();
        memcpy(reply->decoded.payload.bytes, messageReply, reply->decoded.payload.size);
        service->handleToRadio(*reply);
    }
    notifyObservers(&currentRequest);
    return ProcessMessage::CONTINUE;
}

meshtastic_MeshPacket *SignalReplyModule::allocReply()
{
    assert(currentRequest); // should always be !NULL
#ifdef DEBUG_PORT
    auto req = *currentRequest;
    auto &p = req.decoded;
    // The incoming message is in p.payload
    LOG_INFO("Received message from=0x%0x, id=%d, msg=%.*s", req.from, req.id, p.payload.size, p.payload.bytes);
#endif
    screen->print("Send reply\n");
    const char *replyStr = "Message Received";
    auto reply = allocDataPacket();                 // Allocate a packet for sending
    reply->decoded.payload.size = strlen(replyStr); // You must specify how many bytes are in the reply
    memcpy(reply->decoded.payload.bytes, replyStr, reply->decoded.payload.size);
    return reply;
}

bool SignalReplyModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return MeshService::isTextPayload(p);
}
