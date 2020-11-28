#pragma once

#include "mesh/MeshTypes.h"
#include <vector>
/** A baseclass for any mesh "plugin".
 *
 * A plugin allows you to add new features to meshtastic device code, without needing to know messaging details.
 *
 * A key concept for this is that your plugin should use a particular "portnum" for each message type you want to receive
 * and handle.
 *
 * Interally we use plugins to implement the core meshtastic text messaging and gps position sharing features.  You
 * can use these classes as examples for how to write your own custom plugin.  See here: (FIXME)
 */
class MeshPlugin
{
    const char *name;

    static std::vector<MeshPlugin *> *plugins;

  public:
    /** Constructor
     * name is for debugging output
     */
    MeshPlugin(const char *_name);

    virtual ~MeshPlugin();

    /**
     * Initialize your plugin.  This setup function is called once after all hardware and mesh protocol layers have
     * been initialized
     */
    virtual void setup();

    /**
     * @return true if you want to receive the specified portnum
     */
    virtual bool wantPortnum(PortNum p) = 0;

    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceived(const MeshPacket &mp) { return false; }

    /** For use only by MeshService 
     */
    static void callPlugins(const MeshPacket &mp);
};