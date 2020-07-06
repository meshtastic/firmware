#include "timing.h"
#include "freertosinc.h"

namespace timing {

    uint32_t millis() {
        return xTaskGetTickCount();
    }

} // namespace timing
