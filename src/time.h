#pragma once

#include "freertos.h"

namespace time {

    uint32_t millis() {
        return xTaskGetTickCount();
    }

} // namespace time