#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_MODBUS

#include "mesh/generated/meshtastic/telemetry.pb.h"
#include <stddef.h>
#include <stdint.h>

// Modbus-RTU protocol code for SerialModule's MODBUS mode. No Arduino dependency, so it is testable on native.
namespace modbus
{

constexpr uint8_t READ_HOLDING = 0x03;
constexpr uint8_t READ_INPUT = 0x04;

// What a SenseCAP ONE model measures; absent quantities read as 0, so this cannot come from the data.
enum : uint8_t { CAP_THP = 1, CAP_LIGHT = 2, CAP_WIND = 4, CAP_RAIN = 8, CAP_PM = 16, CAP_CO2 = 32 };

enum Result : uint8_t { RESP_INCOMPLETE, RESP_OK, RESP_EXCEPTION, RESP_INVALID };

// One int32 per register pair, indexed by register / 2, covering 0x0000..0x0041.
constexpr size_t REG_SLOTS = 0x21;

struct Profile {
    uint8_t addr;
    uint8_t caps;
};

// Factory default addresses, in scan order.
extern const Profile profiles[];
extern const size_t profileCount;

// One polled slave; an array of these can follow when a bus carries several sensors.
struct Sensor {
    uint8_t addr;
    uint8_t caps;
    uint8_t fails;
    bool split;  // use the two short base reads instead of the full block
    bool fullOk; // the full block has been answered at least once
};

uint16_t crc16(const uint8_t *buf, size_t len);

/// Writes a read-registers request (fc 0x03 or 0x04) to out; returns its length.
size_t buildRead(uint8_t *out, uint8_t addr, uint8_t fc, uint16_t reg, uint16_t count);

/// Classifies the bytes received so far as the answer to a read of count registers.
Result checkResponse(const uint8_t *buf, size_t len, uint8_t addr, uint8_t fc, uint16_t count);

/// Stores the big-endian int32 register pairs of a read response into raw[].
void storeRegisters(int32_t *raw, uint16_t reg, const uint8_t *data, uint8_t byteCount);

uint8_t capsForAddress(uint8_t addr);

/// Advances step to the next read the sensor needs in a poll cycle; false once the cycle is complete.
bool nextRead(uint8_t caps, bool split, uint8_t &step, uint16_t &reg, uint16_t &count);

/// Running means of the polled values between two telemetry sends.
class Aggregate
{
  public:
    void add(const int32_t *raw);
    void environment(meshtastic_EnvironmentMetrics &e, uint8_t caps) const;
    void airQuality(meshtastic_AirQualityMetrics &a, uint8_t caps) const;
    void reset() { s = {}; }
    void nextHour()
    {
        rainIdx = (rainIdx + 1) % 24;
        rainHour[rainIdx] = 0;
    }

  private:
    struct {
        float temp, hum, press, lux, speed, dirSin, dirCos, pm25, pm10, co2;
        int32_t gust, lull, rain1h;
        uint16_t n;
    } s = {};
    int32_t rainHour[24] = {}; // rain per hour from the accumulated counter, survives reset()
    int32_t lastRain = -1;
    uint8_t rainIdx = 0;
};

} // namespace modbus

#endif
