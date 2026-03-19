#pragma once

#include "concurrency/Periodic.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
#include <WiFi.h>
#endif

#if HAS_ETHERNET
#if defined(ESP32) && defined(ETH_PHY_TYPE)
#include <ETH.h>
#elif defined(USE_WS5500)
#include <ETHClass2.h>
#define ETH ETH2
#endif
#endif

extern bool needReconnect;
extern concurrency::Periodic *wifiReconnect;

/// @return true if Wi-Fi is now in use
bool initWifi();

void deinitWifi();

bool isWifiAvailable();

uint8_t getWifiDisconnectReason();

#if HAS_ETHERNET_ON_WIFI_STACK
bool initEthernet();
#endif