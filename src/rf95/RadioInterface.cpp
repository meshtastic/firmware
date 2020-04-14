#include "CustomRF95.h"
#include "NodeDB.h"
#include "assert.h"
#include "configuration.h"
#include <pb_decode.h>
#include <pb_encode.h>

RadioInterface::RadioInterface(MemoryPool<MeshPacket> &_pool, PointerQueue<MeshPacket> &_rxDest) : pool(_pool), rxDest(_rxDest) {}

ErrorCode SimRadio::send(MeshPacket *p)
{
    DEBUG_MSG("SimRadio.send\n");
    pool.release(p);
    return ERRNO_OK;
}