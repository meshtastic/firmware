#pragma once

#include <Arduino.h>
#include "main.h"

uint printWPL(char *buf, const Position &pos, const char *name);
uint printGGA(char *buf, const Position &pos);
