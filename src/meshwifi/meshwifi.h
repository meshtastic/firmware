#pragma once

#include <Arduino.h>
#include <functional>
#include <WiFi.h>

void handleNotFound();

void reconnectWiFi();

void initWifi();

void initWebServer();

void handleWebResponse();

void notifyWebUI();

void handleJSONChatHistory();

void WiFiEvent(WiFiEvent_t event);

