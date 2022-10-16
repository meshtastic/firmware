#pragma once

#include <Arduino.h>
#include "main.h"

uint printWPL(char *buf, Position &pos, const char *name);
uint printGGA(char *buf, Position &pos);
