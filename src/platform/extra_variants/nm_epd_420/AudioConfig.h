#pragma once

#include "configuration.h"

#if defined(NM_EPD_420_BW)
inline void configureNmEpd420NotificationAudio(meshtastic_ModuleConfig_AudioConfig &audio, bool enabled)
{
    audio.codec2_enabled = enabled;
    audio.ptt_pin = 0;
    audio.i2s_ws = 17;
    audio.i2s_sd = 16;
    audio.i2s_din = 18;
    audio.i2s_sck = 15;
}
#else
inline void configureNmEpd420NotificationAudio(meshtastic_ModuleConfig_AudioConfig &, bool) {}
#endif
