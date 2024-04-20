#pragma once

#include "concurrency/Periodic.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
#include <WiFi.h>
#endif

extern bool needReconnect;
extern concurrency::Periodic *wifiReconnect;

/// @return true if wifi is now in use
bool initWifi();

void deinitWifi();

bool isWifiAvailable();

uint8_t getWifiDisconnectReason();