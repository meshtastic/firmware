#pragma once

// low level types

#include "MemoryPool.h"
#include "mesh.pb.h"
#include <Arduino.h>

typedef uint32_t NodeNum;
typedef uint32_t PacketId; // A packet sequence number

#define NODENUM_BROADCAST (sizeof(NodeNum) == 4 ? UINT32_MAX : UINT8_MAX)
#define ERRNO_OK 0
#define ERRNO_NO_INTERFACES 33
#define ERRNO_UNKNOWN 32 // pick something that doesn't conflict with RH_ROUTER_ERROR_UNABLE_TO_DELIVER

/**
 * the max number of hops a message can pass through, used as the default max for hop_limit in MeshPacket.
 *
 * We reserve 3 bits in the header so this could be up to 7, but given the high range of lora and typical usecases, keeping
 * maxhops to 3 should be fine for a while.  This also serves to prevent routing/flooding attempts to be attempted for
 * too long.
 **/
#define HOP_MAX 7

/// We normally just use max 3 hops for sending reliable messages
#define HOP_RELIABLE 3

typedef int ErrorCode;

/// Alloc and free packets to our global, ISR safe pool
extern Allocator<MeshPacket> &packetPool;