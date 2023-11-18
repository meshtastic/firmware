#pragma once
#ifdef ARCH_RASPBERRY_PI
#include <map>

extern std::map<int, int> settingsMap;

enum {
    use_sx1262,
    sx126x_cs,
    sx126x_dio1,
    sx126x_busy,
    sx126x_reset,
    sx126x_dio2_as_rf_switch,
    use_rf95,
    rf95_nss,
    rf95_irq,
    rf95_reset,
    rf95_dio1
};

#endif