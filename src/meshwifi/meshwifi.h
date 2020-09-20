#pragma once

#include "configuration.h"
#include <Arduino.h>
#include <functional>

#ifdef HAS_WIFI
#include <DNSServer.h>
#include <WiFi.h>
#endif

void initWifi();
void deinitWifi();

/// Perform idle loop processing required by the wifi layer
void loopWifi();

bool isWifiAvailable();

void handleDNSResponse();

void reconnectWiFi();

uint8_t getWifiDisconnectReason();