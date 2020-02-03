#pragma once

#include "mesh.pb.h"

// this file defines constants which come from mesh.options

// Tricky macro to let you find the sizeof a type member
#define member_size(type, member) sizeof(((type *)0)->member)

/// max number of packets which can be waiting for delivery to android - note, this value comes from mesh.options protobuf
#define MAX_RX_TOPHONE (member_size(DeviceState, receive_queue) / member_size(DeviceState, receive_queue[0])) 

/// max number of nodes allowed in the mesh
#define MAX_NUM_NODES (member_size(DeviceState, node_db) / member_size(DeviceState, node_db[0])) 
