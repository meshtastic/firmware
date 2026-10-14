#pragma once

#include "sdkconfig.h"

#if defined(MESHTASTIC_INCLUDE_INKHUD_BOOKREADER) && CONFIG_IDF_TARGET_ESP32S3
#define dsps_fft2r_fc32_aes3_enabled 1
#define dsps_fft2r_sc16_aes3_enabled 1
#endif

#ifndef dsps_fft2r_fc32_aes3_enabled
#define dsps_fft2r_fc32_aes3_enabled 0
#endif

#ifndef dsps_fft2r_sc16_aes3_enabled
#define dsps_fft2r_sc16_aes3_enabled 0
#endif
