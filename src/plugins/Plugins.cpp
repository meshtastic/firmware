#include "plugins/ExternalNotificationPlugin.h"
#include "plugins/NodeInfoPlugin.h"
#include "plugins/PositionPlugin.h"
#include "plugins/RemoteHardwarePlugin.h"
#include "plugins/ReplyPlugin.h"
#include "plugins/TextMessagePlugin.h"

#ifndef NO_ESP32
#include "plugins/esp32/RangeTestPlugin.h"
#include "plugins/SerialPlugin.h"
#include "plugins/StoreForwardPlugin.h"
#endif

/**
 * Create plugin instances here.  If you are adding a new plugin, you must 'new' it here (or somewhere else)
 */
void setupPlugins()
{
    nodeInfoPlugin = new NodeInfoPlugin();
    positionPlugin = new PositionPlugin();
    textMessagePlugin = new TextMessagePlugin();

    // Note: if the rest of meshtastic doesn't need to explicitly use your plugin, you do not need to assign the instance
    // to a global variable.

    new RemoteHardwarePlugin();
    new ReplyPlugin();

#ifndef NO_ESP32
    // Only run on an esp32 based device.

    /*
        Maintained by MC Hamster (Jm Casler) jm@casler.org
    */
    new SerialPlugin();
    new ExternalNotificationPlugin();

    // rangeTestPlugin = new RangeTestPlugin();
    storeForwardPlugin = new StoreForwardPlugin();

    new RangeTestPlugin();
    // new StoreForwardPlugin();
#endif
}