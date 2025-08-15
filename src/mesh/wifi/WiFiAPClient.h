#pragma once

#include "concurrency/Periodic.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
#include <WiFi.h>
#endif

#if HAS_ETHERNET
# if defined(USE_WS5500)
# include <ETHClass2.h>
# define ETH ETH2
# endif
# if defined(USE_ESP32_RMIIPHY)
# include <ETH.h>
# endif
#endif // HAS_ETHERNET

extern bool needReconnect;
extern concurrency::Periodic *wifiReconnect;

/// @return true if wifi is now in use
bool initWifi();

void deinitWifi();

bool isWifiAvailable();

uint8_t getWifiDisconnectReason();

#if defined(USE_WS5500) || defined(USE_ESP32_RMIIPHY)
// Startup Ethernet
bool initEthernet();
#endif
