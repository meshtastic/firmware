#pragma once

#include "MemoryPool.h"
#include "MeshTypes.h"
#include "PointerQueue.h"
#include "configuration.h"
#include "mesh.pb.h"

// US channel settings
#define CH0_US 903.08f      // MHz
#define CH_SPACING_US 2.16f // MHz
#define NUM_CHANNELS_US 13

// EU433 channel settings
#define CH0_EU433 433.175f    // MHz
#define CH_SPACING_EU433 0.2f // MHz
#define NUM_CHANNELS_EU433 8

// EU865 channel settings
#define CH0_EU865 865.2f      // MHz
#define CH_SPACING_EU865 0.3f // MHz
#define NUM_CHANNELS_EU865 10

// CN channel settings
#define CH0_CN 470.0f      // MHz
#define CH_SPACING_CN 2.0f // MHz FIXME, this is just a guess for 470-510
#define NUM_CHANNELS_CN 20

// JP channel settings (AS1 bandplan)
#define CH0_JP 920.0f // MHz
#define CH_SPACING_JP 0.5f
#define NUM_CHANNELS_JP 10

// TW channel settings (AS2 bandplan 923-925MHz)
#define CH0_TW 923.0f // MHz
#define CH_SPACING_TW 0.2
#define NUM_CHANNELS_TW 10

// FIXME add defs for other regions and use them here
#ifdef HW_VERSION_US
#define CH0 CH0_US
#define CH_SPACING CH_SPACING_US
#define NUM_CHANNELS NUM_CHANNELS_US
#elif defined(HW_VERSION_EU433)
#define CH0 CH0_EU433
#define CH_SPACING CH_SPACING_EU433
#define NUM_CHANNELS NUM_CHANNELS_EU433
#elif defined(HW_VERSION_EU865)
#define CH0 CH0_EU865
#define CH_SPACING CH_SPACING_EU865
#define NUM_CHANNELS NUM_CHANNELS_EU865
#elif defined(HW_VERSION_CN)
#define CH0 CH0_CN
#define CH_SPACING CH_SPACING_CN
#define NUM_CHANNELS NUM_CHANNELS_CN
#elif defined(HW_VERSION_JP)
// Also called AS1 bandplan
#define CH0 CH0_JP
#define CH_SPACING CH_SPACING_JP
#define NUM_CHANNELS NUM_CHANNELS_JP
#elif defined(HW_VERSION_TW)
// Also called AS2 bandplan
#define CH0 CH0_TW
#define CH_SPACING CH_SPACING_TW
#define NUM_CHANNELS NUM_CHANNELS_TW
#else
// HW version not set - assume US
#define CH0 CH0_US
#define CH_SPACING CH_SPACING_US
#define NUM_CHANNELS NUM_CHANNELS_US
#endif
