#include "configuration.h"
#include "AdminPlugin.h"
#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "main.h"

#ifdef PORTDUINO
#include "unistd.h"
#endif

AdminPlugin *adminPlugin;

/// A special reserved string to indicate strings we can not share with external nodes.  We will use this 'reserved' word instead.
/// Also, to make setting work correctly, if someone tries to set a string to this reserved value we assume they don't really want a change.
static const char *secretReserved = "sekrit";

/// If buf is !empty, change it to secret
static void hideSecret(char *buf) {
    if(*buf) {
        strcpy(buf, secretReserved);
    }
}

/// If buf is the reserved secret word, replace the buffer with currentVal
static void writeSecret(char *buf, const char *currentVal) {
    if(strcmp(buf, secretReserved) == 0) {
        strcpy(buf, currentVal);
    }
}

void AdminPlugin::handleGetChannel(const MeshPacket &req, uint32_t channelIndex)
{
    if (req.decoded.want_response) {
        // We create the reply here
        AdminMessage r = AdminMessage_init_default;
        r.get_channel_response = channels.getByIndex(channelIndex);
        r.which_variant = AdminMessage_get_channel_response_tag;
        myReply = allocDataProtobuf(r);
    }
}

void AdminPlugin::handleGetRadio(const MeshPacket &req)
{
    if (req.decoded.want_response) {
        // We create the reply here
        AdminMessage r = AdminMessage_init_default;
        r.get_radio_response = radioConfig;

        // NOTE: The phone app needs to know the ls_secs & phone_timeout value so it can properly expect sleep behavior.
        // So even if we internally use 0 to represent 'use default' we still need to send the value we are
        // using to the app (so that even old phone apps work with new device loads).
        r.get_radio_response.preferences.ls_secs = getPref_ls_secs();
        r.get_radio_response.preferences.phone_timeout_secs = getPref_phone_timeout_secs();
        // hideSecret(r.get_radio_response.preferences.wifi_ssid); // hmm - leave public for now, because only minimally private and useful for users to know current provisioning)
        hideSecret(r.get_radio_response.preferences.wifi_password);

        r.which_variant = AdminMessage_get_radio_response_tag;
        myReply = allocDataProtobuf(r);
    }
}

bool AdminPlugin::handleReceivedProtobuf(const MeshPacket &mp, AdminMessage *r)
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
        DEBUG_MSG("Client is setting channel %d\n", r->set_channel.index);
        if (r->set_channel.index < 0 || r->set_channel.index >= (int)MAX_NUM_CHANNELS)
            myReply = allocErrorResponse(Routing_Error_BAD_REQUEST, &mp);
        else
            handleSetChannel(r->set_channel);
        break;

    case AdminMessage_get_channel_request_tag: {
        uint32_t i = r->get_channel_request - 1;
        DEBUG_MSG("Client is getting channel %u\n", i);
        if (i >= MAX_NUM_CHANNELS)
            myReply = allocErrorResponse(Routing_Error_BAD_REQUEST, &mp);
        else
            handleGetChannel(mp, i);
        break;
    }

    case AdminMessage_get_radio_request_tag:
        DEBUG_MSG("Client is getting radio\n");
        handleGetRadio(mp);
        break;

    case AdminMessage_reboot_seconds_tag: {
        int32_t s = r->reboot_seconds;
        DEBUG_MSG("Rebooting in %d seconds\n", s);
        rebootAtMsec = (s < 0) ? 0 : (millis() + s * 1000);
        break;
    }

#ifdef PORTDUINO
    case AdminMessage_exit_simulator_tag:
        DEBUG_MSG("Exiting simulator\n");
        _exit(0);
        break;
#endif

    default:
        // Probably a message sent by us or sent to our local node.  FIXME, we should avoid scanning these messages
        DEBUG_MSG("Ignoring nonrelevant admin %d\n", r->which_variant);
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
    if (owner.is_licensed != o.is_licensed) {
        changed = 1;
        owner.is_licensed = o.is_licensed;
    }

    if ((!changed || o.team) && (owner.team != o.team)) {
        changed = 1;
        owner.team = o.team;
    }

    if (changed) // If nothing really changed, don't broadcast on the network or write to flash
        service.reloadOwner();
}

void AdminPlugin::handleSetChannel(const Channel &cc)
{
    channels.setChannel(cc);

    // Just update and save the channels - no need to update the radio for ! primary channel changes
    if (cc.index == 0) {
        // FIXME, this updates the user preferences also, which isn't needed - we really just want to notify on configChanged
        service.reloadConfig();
    } else {
        channels.onConfigChanged(); // tell the radios about this change
        nodeDB.saveChannelsToDisk();
    }
}

void AdminPlugin::handleSetRadio(RadioConfig &r)
{
    writeSecret(r.preferences.wifi_password, radioConfig.preferences.wifi_password);
    radioConfig = r;

    service.reloadConfig();
}

AdminPlugin::AdminPlugin() : ProtobufPlugin("Admin", PortNum_ADMIN_APP, AdminMessage_fields)
{
    // restrict to the admin channel for rx
    boundChannel = Channels::adminChannel;
}
