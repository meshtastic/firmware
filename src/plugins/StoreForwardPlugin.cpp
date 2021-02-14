#include "StoreForwardPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include <Arduino.h>

#include <assert.h>



StoreForwardPlugin *storeForwardPlugin;
StoreForwardPluginRadio *storeForwardPluginRadio;

StoreForwardPlugin::StoreForwardPlugin() : concurrency::OSThread("StoreForwardPlugin") {}

int32_t StoreForwardPlugin::runOnce()
{
#ifndef NO_ESP32

    /*
        Uncomment the preferences below if you want to use the plugin
        without having to configure it from the PythonAPI or WebUI.
    */

    if (0) {

        if (firstTime) {


            storeForwardPluginRadio = new StoreForwardPluginRadio();

            firstTime = 0;

        } else {

        }

        return (10);
    } else {
        DEBUG_MSG("StoreForwardPlugin Disabled\n");

        return (INT32_MAX);
    }

#endif
}

MeshPacket *StoreForwardPluginRadio::allocReply()
{

    auto reply = allocDataPacket(); // Allocate a packet for sending

    return reply;
}
void StoreForwardPluginRadio::sendPayload(NodeNum dest, bool wantReplies)
{
    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;

    service.sendToMesh(p);
}

bool StoreForwardPluginRadio::handleReceived(const MeshPacket &mp)
{
#ifndef NO_ESP32


#endif

    return true; // Let others look at this message also if they want
}
