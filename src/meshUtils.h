#pragma once
#include "DebugConfiguration.h"
#include <algorithm>
#include <cstdarg>
#include <iterator>
#include <mesh/generated/meshtastic/config.pb.h>
#include <stdint.h>

/// C++ v17+ clamp function, limits a given value to a range defined by lo and hi
template <class T> constexpr const T &clamp(const T &v, const T &lo, const T &hi)
{
    return (v < lo) ? lo : (hi < v) ? hi : v;
}

#if HAS_SCREEN
#define IF_SCREEN(X)                                                                                                             \
    if (screen) {                                                                                                                \
        X;                                                                                                                       \
    }
#else
#define IF_SCREEN(...)
#endif

#if (defined(ARCH_PORTDUINO) && !defined(STRNSTR))
#define STRNSTR
#include <string.h>
char *strnstr(const char *s, const char *find, size_t slen);
#endif

void printBytes(const char *label, const uint8_t *p, size_t numbytes);

// is the memory region filled with a single character?
bool memfll(const uint8_t *mem, uint8_t find, size_t numbytes);

bool isOneOf(int item, int count, ...);

const std::string vformat(const char *const zcFormat, ...);

// Get actual string length for nanopb char array fields.
size_t pb_string_length(const char *str, size_t max_len);

/// Calculate 2^n without calling pow() - used for spreading factor and other calculations
inline uint32_t pow_of_2(uint32_t n)
{
    return 1 << n;
}

#define IS_ONE_OF(item, ...) isOneOf(item, sizeof((int[]){__VA_ARGS__}) / sizeof(int), __VA_ARGS__)

inline bool isRouterRole(meshtastic_Config_DeviceConfig_Role role)
{
    return role == meshtastic_Config_DeviceConfig_Role_ROUTER || role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE;
}

inline bool isRouterLikeRole(meshtastic_Config_DeviceConfig_Role role)
{
    return role == meshtastic_Config_DeviceConfig_Role_ROUTER || role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE ||
           role == meshtastic_Config_DeviceConfig_Role_CLIENT_BASE;
}

inline bool isTrackerRole(meshtastic_Config_DeviceConfig_Role role)
{
    return role == meshtastic_Config_DeviceConfig_Role_TRACKER || role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER;
}

inline bool isSensorRole(meshtastic_Config_DeviceConfig_Role role)
{
    return role == meshtastic_Config_DeviceConfig_Role_SENSOR;
}

inline bool isTrackerOrSensorRole(meshtastic_Config_DeviceConfig_Role role)
{
    return isTrackerRole(role) || isSensorRole(role);
}

inline bool isTakLikeRole(meshtastic_Config_DeviceConfig_Role role)
{
    return role == meshtastic_Config_DeviceConfig_Role_TAK || role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER;
}

inline bool isSensorOrRouterRole(meshtastic_Config_DeviceConfig_Role role)
{
    return isSensorRole(role) || isRouterRole(role);
}

inline bool isClientMuteRole(meshtastic_Config_DeviceConfig_Role role)
{
    return role == meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE;
}

inline bool isClientHiddenRole(meshtastic_Config_DeviceConfig_Role role)
{
    return role == meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN;
}

inline bool isClientBaseRole(meshtastic_Config_DeviceConfig_Role role)
{
    return role == meshtastic_Config_DeviceConfig_Role_CLIENT_BASE;
}

inline bool isLostAndFoundRole(meshtastic_Config_DeviceConfig_Role role)
{
    return role == meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND;
}

inline bool isClientRole(meshtastic_Config_DeviceConfig_Role role)
{
    return role == meshtastic_Config_DeviceConfig_Role_CLIENT || role == meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE ||
           role == meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN || role == meshtastic_Config_DeviceConfig_Role_CLIENT_BASE;
}
