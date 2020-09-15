#pragma once

#include <Arduino.h>
#include <functional>
#include <WiFi.h>

void reconnectWiFi();

void initWifi();

void deinitWifi();

void WiFiEvent(WiFiEvent_t event);

