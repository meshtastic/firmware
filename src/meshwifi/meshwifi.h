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
bool isWifiAvailable();

void handleDNSResponse();

void reconnectWiFi();
