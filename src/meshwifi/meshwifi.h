#pragma once

#include <Arduino.h>
#include <functional>
#include <WiFi.h>
#include <DNSServer.h>

void initWifi();

void deinitWifi();

void WiFiEvent(WiFiEvent_t event);

bool isWifiAvailable();

void handleDNSResponse();
