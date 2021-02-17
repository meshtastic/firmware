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
    static std::vector<MeshPlugin *> *plugins;

  public:
    /** Constructor
     * name is for debugging output
     */
    MeshPlugin(const char *_name);

    virtual ~MeshPlugin();

    /** For use only by MeshService
     */
    static void callPlugins(const MeshPacket &mp);

  protected:
    const char *name;

    /* Most plugins only care about packets that are destined for their node (i.e. broadcasts or has their node as the specific recipient)
    But some plugs might want to 'sniff' packets that are merely being routed (passing through the current node).  Those plugins can set this to
    true and their handleReceived() will be called for every packet.
    */
    bool isPromiscuous = false;

    /**
     * If this plugin is currently handling a request currentRequest will be preset
     * to the packet with the request.  This is mostly useful for reply handlers.
     * 
     * Note: this can be static because we are guaranteed to be processing only one
     * plugin at a time.
     */
    static const MeshPacket *currentRequest;

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

    /** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
     * so that subclasses can (optionally) send a response back to the original sender.  */
    virtual MeshPacket *allocReply() { return NULL; }

  private:

    /** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
     * so that subclasses can (optionally) send a response back to the original sender.  This method calls allocReply()
     * to generate the reply message, and if !NULL that message will be delivered to whoever sent req
     */
    void sendResponse(const MeshPacket &req);
};

/** set the destination and packet parameters of packet p intended as a reply to a particular "to" packet 
 * This ensures that if the request packet was sent reliably, the reply is sent that way as well.
*/
void setReplyTo(MeshPacket *p, const MeshPacket &to);