/*
 *  nrf52840 internal die temperature sensor
 */

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(ARCH_NRF52)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

class NRFTempSensor
{
  public:
    NRFTempSensor();
    int32_t runOnce();
    bool getMetrics(meshtastic_Telemetry *measurement);
};

#endif
