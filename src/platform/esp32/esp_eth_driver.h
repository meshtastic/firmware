#pragma once

#if __has_include_next("esp_eth_driver.h")
#include_next "esp_eth_driver.h"
#elif __has_include("esp_eth.h")
#include "esp_eth.h"
#endif
