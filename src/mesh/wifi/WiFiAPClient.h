#pragma once

#include "concurrency/Periodic.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
#include <WiFi.h>
#endif

#if HAS_ETHERNET && defined(ARCH_ESP32)
#include <ETH.h>
#endif // HAS_ETHERNET

extern bool needReconnect;
extern concurrency::Periodic *wifiReconnect;

/// @return true if wifi is now in use
bool initWifi();

void deinitWifi();

bool isWifiAvailable();

uint8_t getWifiDisconnectReason();

#if HAS_WIFI && defined(ARCH_ESP32) && (ESP_ARDUINO_VERSION <= ESP_ARDUINO_VERSION_VAL(3, 0, 0))
IPv6Address GlobalIPv6();
#endif

#if defined(USE_WS5500) || defined(USE_CH390D)
// Startup Ethernet
bool initEthernet();
#endif