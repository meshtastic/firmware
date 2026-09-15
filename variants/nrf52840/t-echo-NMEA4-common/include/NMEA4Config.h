#pragma once

#if defined(T_ECHO_NMEA4_FAMILY) && defined(USE_EINK)
#define NMEA4_HAS_SATELLITES_PAGE 1
#else
#define NMEA4_HAS_SATELLITES_PAGE 0
#endif

#if defined(T_ECHO_NMEA4_FAMILY) && defined(USE_EINK) && __has_include("graphics/draw/FavoritesMapRenderer.h")
#define NMEA4_HAS_FAVORITES_MAP 1
#else
#define NMEA4_HAS_FAVORITES_MAP 0
#endif
