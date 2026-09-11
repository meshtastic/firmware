#pragma once

#include <sdkconfig.h>

// Some bundled SDK variants omit these sizes although BLE Mesh headers use them.
#ifndef CONFIG_BLE_MESH_MODEL_KEY_COUNT
#define CONFIG_BLE_MESH_MODEL_KEY_COUNT 3
#endif

#ifndef CONFIG_BLE_MESH_MODEL_GROUP_COUNT
#define CONFIG_BLE_MESH_MODEL_GROUP_COUNT 3
#endif
