#pragma once

#include <RH_RF95.h>
#include <RHMesh.h>

#define NODENUM_BROADCAST 255
#define ERRNO_OK 0
#define ERRNO_UNKNOWN 32 // pick something that doesn't conflict with RH_ROUTER_ERROR_UNABLE_TO_DELIVER

typedef int ErrorCode;
typedef uint8_t NodeNum;

/// Callback for a receive packet
typedef void (*MeshRXHandler)(NodeNum from, NodeNum to, std::string packet);

/**
 * A raw low level interface to our mesh.  Only understands nodenums and bytes (not protobufs or node ids)
 */
class MeshRadio {
public:
    MeshRadio();

    bool init();

    /// Prepare the radio to enter sleep mode, where it should draw only 0.2 uA
    void sleep() { rf95.sleep(); }

    /// Send a packet - the current implementation blocks for a while possibly (FIXME)
    ErrorCode sendTo(NodeNum dest, const uint8_t *buf, size_t len);

    /// Do loop callback operations (we currently FIXME poll the receive mailbox here)
    /// for received packets it will call the rx handler
    void loop();

    void setRXHandler(MeshRXHandler h) { rxHandler = h; }

private:
    RH_RF95 rf95; // the raw radio interface
    RHMesh manager;
    MeshRXHandler rxHandler;
};

extern MeshRadio radio;

void mesh_init();
void mesh_loop();