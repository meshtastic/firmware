#pragma once

#include <Arduino.h>
#include <functional>
#include <WiFi.h>

void initWebServer();

void handleNotFound();

void handleWebResponse();

void handleJSONChatHistory();

void notifyWebUI();

void handleHotspot();
