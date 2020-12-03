#include "MeshPlugin.h"
#include "Router.h"

/**
 * A base class for mesh plugins that assume that they are sending/receiving one particular protobuf based
 * payload.  Using one particular app ID.
 *
 * If you are using protobufs to encode your packets (recommended) you can use this as a baseclass for your plugin
 * and avoid a bunch of boilerplate code.
 */
template <class T> class ProtobufPlugin : private MeshPlugin
{
    const pb_msgdesc_t *fields;
    PortNum ourPortNum;

  public:
    /** Constructor
     * name is for debugging output
     */
    ProtobufPlugin(const char *_name, PortNum _ourPortNum, const pb_msgdesc_t *_fields)
        : MeshPlugin(_name), fields(_fields), ourPortNum(_ourPortNum)
    {
    }

  protected:
    /**
     * @return true if you want to receive the specified portnum
     */
    virtual bool wantPortnum(PortNum p) { return p == ourPortNum; }

    /**
     * Handle a received message, the data field in the message is already decoded and is provided
     */
    virtual bool handleReceivedProtobuf(const MeshPacket &mp, const T &decoded) = 0;

    /**
     * Return a mesh packet which has been preinited with a particular protobuf data payload and port number.
     * You can then send this packet (after customizing any of the payload fields you might need) with
     * service.sendToMesh()
     */
    MeshPacket *allocForSending(const T &payload)
    {
        // Update our local node info with our position (even if we don't decide to update anyone else)
        MeshPacket *p = router->allocForSending();
        p->decoded.which_payload = SubPacket_data_tag;
        p->decoded.data.portnum = ourPortNum;

        p->decoded.data.payload.size =
            pb_encode_to_bytes(p->decoded.data.payload.bytes, sizeof(p->decoded.data.payload.bytes), fields, &payload);
        return p;
    }

  private:
    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceived(const MeshPacket &mp)
    {
        // FIXME - we currently update position data in the DB only if the message was a broadcast or destined to us
        // it would be better to update even if the message was destined to others.

        auto &p = mp.decoded.data;
        DEBUG_MSG("Received %s from=0x%0x, id=%d, msg=%.*s\n", name, mp.from, mp.id, p.payload.size, p.payload.bytes);

        T scratch;
        if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, fields, &scratch))
            handleReceivedProtobuf(mp, scratch);

        return false; // Let others look at this message also if they want
    }
};
