#include "AdminPlugin.h"
#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

AdminPlugin *adminPlugin;

void AdminPlugin::handleGetChannel(const MeshPacket &req, uint32_t channelIndex) {
        if (req.decoded.want_response) {
        // We create the reply here
        AdminMessage r = AdminMessage_init_default;
        r.get_channel_response = channels.getByIndex(channelIndex);
        reply = allocDataProtobuf(r);
    }
}

void AdminPlugin::handleGetRadio(const MeshPacket &req)
{
    if (req.decoded.want_response) {
        // We create the reply here
        AdminMessage r = AdminMessage_init_default;
        r.get_radio_response = devicestate.radio;
        reply = allocDataProtobuf(r);
    }
}

bool AdminPlugin::handleReceivedProtobuf(const MeshPacket &mp, const AdminMessage *r)
{
    assert(r);
    switch (r->which_variant) {
    case AdminMessage_set_owner_tag:
        DEBUG_MSG("Client is setting owner\n");
        handleSetOwner(r->set_owner);
        break;

    case AdminMessage_set_radio_tag:
        DEBUG_MSG("Client is setting radio\n");
        handleSetRadio(r->set_radio);
        break;

    case AdminMessage_set_channel_tag:
        DEBUG_MSG("Client is setting channel\n");
        handleSetChannel(r->set_channel);
        break;

    case AdminMessage_get_channel_request_tag:
        DEBUG_MSG("Client is getting channel %d\n", r->get_channel_request);
        handleGetChannel(mp, r->get_channel_request);
        break;

    case AdminMessage_get_radio_request_tag:
        DEBUG_MSG("Client is getting radio\n");
        handleGetRadio(mp);
        break;

    default:
        break;
    }
    return false; // Let others look at this message also if they want
}

void AdminPlugin::handleSetOwner(const User &o)
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

void AdminPlugin::handleSetChannel(const Channel &cc)
{
    channels.setChannel(cc);

    bool didReset = service.reloadConfig();
    /* FIXME - do we need this still?
    if (didReset) {
        state = STATE_SEND_MY_INFO; // Squirt a completely new set of configs to the client
    } */
}

void AdminPlugin::handleSetRadio(const RadioConfig &r)
{
    radioConfig = r;

    bool didReset = service.reloadConfig();
    /* FIXME - do we need this still?  if (didReset) {
        state = STATE_SEND_MY_INFO; // Squirt a completely new set of configs to the client
    } */
}

MeshPacket *AdminPlugin::allocReply()
{
    auto r = reply;
    reply = NULL; // Only use each reply once
    return r;
}

AdminPlugin::AdminPlugin() : ProtobufPlugin("Admin", PortNum_ADMIN_APP, AdminMessage_fields)
{
    // FIXME, restrict to the admin channel for rx
}
