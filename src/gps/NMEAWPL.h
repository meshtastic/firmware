#pragma once

#include <Arduino.h>
#include "main.h"

uint32_t printWPL(char *buf, const Position &pos, const char *name);
uint32_t printGGA(char *buf, const Position &pos);
