#pragma once
#ifdef ARCH_RASPBERRY_PI
#include <map>

extern std::map<int, int> settingsMap;

enum { use_sx1262, cs, irq, busy, reset, dio2_as_rf_switch, use_rf95, user };
int initGPIOPin(int pinNum);

#endif