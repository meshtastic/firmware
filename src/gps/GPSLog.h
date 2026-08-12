#pragma once

#include "DebugConfiguration.h"

// GPS_DEBUG=1 enables verbose GNSS diagnostics (probe/ACK byte dumps, pin states, NMEA ages).
// Costs no flash when off. Genuine LOG_WARN anomalies stay unconditional.
#ifndef GPS_DEBUG
#define GPS_DEBUG 0
#endif
#if GPS_DEBUG
#define LOG_DEBUG_GPS(...) LOG_DEBUG(__VA_ARGS__)
#else
#define LOG_DEBUG_GPS(...) ((void)0)
#endif
