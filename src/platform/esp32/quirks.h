#if QUIRK_RTTTL
#define ledcSetup(ch, freq, res)                                                                                                 \
    uint32_t __freq = freq;                                                                                                      \
    uint8_t __res = res;                                                                                                         \
    do {                                                                                                                         \
    } while (0)
#define ledcAttachPin(pin, ch) ledcAttachChannel(pin, __freq, __res, ch)
#endif

#if QUIRK_LOVYAN && (CHATTER_2 || M5STACK)
#include "rom/ets_sys.h"
#include <stdbool.h>
#undef bool
#undef true
#undef false
#endif
