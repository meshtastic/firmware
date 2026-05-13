#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_EXAMPLE

#include "ExampleModule.h"
#include "MeshService.h"
#include "main.h"

#include <assert.h>
#include <cstring>

ExampleModule::ExampleModule() : SinglePortModule("example", meshtastic_PortNum_PRIVATE_APP)
{
    // Keep construction lightweight. Hardware, mesh, and configuration setup
    // should happen in setup(), after the system has initialized.
}

void ExampleModule::setup()
{
    LOG_INFO("ExampleModule initialized");
}

ProcessMessage ExampleModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    assert(currentRequest);

    const auto &payload = mp.decoded.payload;

    LOG_INFO("ExampleModule received packet from=0x%0x, id=%d, payload=%.*s",
             mp.from, mp.id, payload.size, payload.bytes);

    messageCount++;

    // Return CONTINUE unless the module must prevent later modules from seeing
    // the packet. Most modules should not stop the processing chain.
    return ProcessMessage::CONTINUE;
}

meshtastic_MeshPacket *ExampleModule::allocReply()
{
    assert(currentRequest);

    // Avoid reply storms when a broadcast request has already crossed multiple hops.
    if (isMultiHopBroadcastRequest()) {
        LOG_DEBUG("ExampleModule skipping multi-hop broadcast reply");
        return nullptr;
    }

    char replyText[64];
    snprintf(replyText, sizeof(replyText), "ExampleModule saw %lu message(s).", messageCount);

    meshtastic_MeshPacket *reply = allocDataPacket();
    reply->decoded.payload.size = strlen(replyText);
    memcpy(reply->decoded.payload.bytes, replyText, reply->decoded.payload.size);

    return reply;
}

#endif // MESHTASTIC_EXCLUDE_EXAMPLE
