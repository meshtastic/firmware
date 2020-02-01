#pragma once

#include <RH_RF95.h>
#include <RHMesh.h>

#define NODENUM_BROADCAST 255

typedef int ErrorCode;
typedef uint8_t NodeNum;


/**
 * A raw low level interface to our mesh.  Only understands nodenums and bytes (not protobufs or node ids)
 */
class MeshRadio {
public:
    MeshRadio();

    bool init();

    /// Send a packet - the current implementation blocks for a while possibly (FIXME)
    ErrorCode sendTo(NodeNum dest, const uint8_t *buf, size_t len);

    /// Do loop callback operations (we currently FIXME poll the receive mailbox here)
    void loop();


private:
    RH_RF95 rf95; // the raw radio interface
    RHMesh manager;

};

extern MeshRadio radio;

void mesh_init();
void mesh_loop();