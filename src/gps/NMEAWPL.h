#pragma once

#include <Arduino.h>
#include "main.h"

uin32_t printWPL(char *buf, const Position &pos, const char *name);
uin32_t printGGA(char *buf, const Position &pos);
