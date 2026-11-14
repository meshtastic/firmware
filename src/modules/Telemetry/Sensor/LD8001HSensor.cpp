#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(LD8001H_RX)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "LD8001HSensor.h"
#include "TelemetrySensor.h"
#include "mesh/Throttle.h"
#include "meshUtils.h"
#include <ctype.h>
#include <string.h>

#ifndef LD8001H_SERIAL
#define LD8001H_SERIAL Serial2
#endif
#ifndef LD8001H_TX
#define LD8001H_TX -1 // the sensor only transmits
#endif
#define LD8001H_BAUD 115200
#define LD8001H_READ_TIMEOUT_MS 1000 // the sensor streams ~10 lines/s

LD8001HSensor::LD8001HSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "LD8001H") {}

bool LD8001HSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s on UART rx=%d tx=%d", sensorName, (int)LD8001H_RX, (int)LD8001H_TX);
#if defined(ARCH_ESP32)
    LD8001H_SERIAL.begin(LD8001H_BAUD, SERIAL_8N1, LD8001H_RX, LD8001H_TX);
#elif defined(ARCH_NRF52)
    static_assert((int)(LD8001H_TX) >= 0, "nRF52 UART needs a real LD8001H_TX pin");
    LD8001H_SERIAL.setPins(LD8001H_RX, LD8001H_TX);
    LD8001H_SERIAL.begin(LD8001H_BAUD);
#else
#error "LD8001H: UART setup is not implemented for this architecture"
#endif
    status = 1;
    initialized = true;
    return true;
}

// "R=001.937 R=001.936" -> raw 1.937 m, filtered 1.936 m.
// Not sscanf("%f"): nRF52 links newlib-nano without _scanf_float, so %f never converts there.
static bool parseLine(const char *line, float *rawM, float *filteredM)
{
    const char *r1 = strstr(line, "R=");
    const char *r2 = r1 ? strstr(r1 + 2, "R=") : nullptr;
    if (!r2 || !isdigit((unsigned char)r1[2]) || !isdigit((unsigned char)r2[2]))
        return false;
    *rawM = parseDecimalFloat(r1 + 2);
    *filteredM = parseDecimalFloat(r2 + 2);
    return true;
}

bool LD8001HSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    // Drop stale bytes so we report a fresh reading
    while (LD8001H_SERIAL.available())
        LD8001H_SERIAL.read();

    char line[48];
    size_t len = 0;
    float raw = 0, filtered = 0;
    bool got = false;
    uint32_t start = millis();
    while (!got && Throttle::isWithinTimespanMs(start, LD8001H_READ_TIMEOUT_MS)) {
        if (!LD8001H_SERIAL.available()) {
            delay(5);
            continue;
        }
        char c = LD8001H_SERIAL.read();
        if (c == '\n' || c == '\r') {
            line[len] = 0;
            got = len > 0 && parseLine(line, &raw, &filtered);
            len = 0;
        } else if (len < sizeof(line) - 1) {
            line[len++] = c;
        } else {
            len = 0; // garbage, resync on next newline
        }
    }

    if (!got) {
        LOG_WARN("%s: no valid reading", sensorName);
        return false;
    }

    // Report the sensor's filtered value; the raw one fluctuates more
    LOG_DEBUG("%s: raw=%.3fm filtered=%.3fm", sensorName, raw, filtered);
    measurement->variant.environment_metrics.has_distance = true;
    measurement->variant.environment_metrics.distance = filtered * 1000.0f; // field is in mm
    return true;
}

#endif
