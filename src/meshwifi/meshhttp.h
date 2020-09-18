#pragma once

#include <Arduino.h>
#include <functional>

void initWebServer();

void handleNotFound();

void handleWebResponse();

void handleJSONChatHistory();

void notifyWebUI();

