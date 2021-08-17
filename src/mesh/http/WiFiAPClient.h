#pragma once

#include "configuration.h"
#include <Arduino.h>
#include <functional>

#ifdef HAS_WIFI
#include <DNSServer.h>
#include <WiFi.h>
#endif

/// @return true if wifi is now in use
bool initWifi(bool forceSoftAP);

void deinitWifi();

bool isWifiAvailable();

void handleDNSResponse();

bool isSoftAPForced();

uint8_t getWifiDisconnectReason();

