#pragma once

#include "configuration.h"
#include <Arduino.h>
#include <functional>

#ifdef HAS_WIFI
#include <WiFi.h>
#include <DNSServer.h>
#endif

void initWifi();
void deinitWifi();
bool isWifiAvailable();

void WiFiEvent(WiFiEvent_t event);

void handleDNSResponse();
