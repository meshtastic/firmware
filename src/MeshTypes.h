#pragma once

// low level types

#include "MemoryPool.h"
#include "mesh.pb.h"
#include <Arduino.h>

typedef uint8_t NodeNum;

#define NODENUM_BROADCAST 255
#define ERRNO_OK 0
#define ERRNO_UNKNOWN 32 // pick something that doesn't conflict with RH_ROUTER_ERROR_UNABLE_TO_DELIVER

typedef int ErrorCode;

/// Alloc and free packets to our global, ISR safe pool
extern MemoryPool<MeshPacket> packetPool;