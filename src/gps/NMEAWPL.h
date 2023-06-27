#pragma once

#include "main.h"
#include <Arduino.h>

uint32_t printWPL(char *buf, size_t bufsz, const meshtastic_Position &pos, const char *name, bool isCaltopoMode = false);
uint32_t printWPL(char *buf, size_t bufsz, const meshtastic_PositionLite &pos, const char *name, bool isCaltopoMode = false);
uint32_t printGGA(char *buf, size_t bufsz, const meshtastic_Position &pos);