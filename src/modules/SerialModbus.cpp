#include "SerialModbus.h"

#if !MESHTASTIC_EXCLUDE_MODBUS

#include <math.h>

namespace modbus
{

constexpr uint8_t CAPS_S700 = CAP_THP | CAP_LIGHT | CAP_WIND | CAP_RAIN;
constexpr uint8_t CAPS_S1000 = CAPS_S700 | CAP_PM | CAP_CO2;

// SenseCAP ONE factory addresses; 1 (generic) and 69 (S600-A) get the S700 set like any unknown address.
const Profile profiles[] = {{1, CAPS_S700},   {10, CAP_THP | CAP_WIND}, {20, CAPS_S700},
                            {43, CAPS_S1000}, {44, CAP_WIND},           {46, CAP_THP | CAP_WIND | CAP_PM},
                            {60, CAPS_S700},  {61, CAPS_S1000},         {69, CAPS_S700}};
const size_t profileCount = sizeof(profiles) / sizeof(profiles[0]);

// Pseudo capabilities selecting the full base block or the two short reads that replace it.
enum : uint8_t { NEED_FULL = 0x40, NEED_SPLIT = 0x80 };

static const struct {
    uint8_t reg, count, need;
} blocks[] = {{0x00, 0x20, NEED_FULL},
              {0x00, 6, NEED_SPLIT | CAP_THP},
              {0x08, 12, NEED_SPLIT | CAP_WIND},
              {0x30, 4, CAP_PM},
              {0x40, 2, CAP_CO2}};

uint16_t crc16(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= *buf++;
        for (uint8_t i = 0; i < 8; i++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

static size_t appendCrc(uint8_t *buf, size_t len)
{
    uint16_t crc = crc16(buf, len);
    buf[len] = crc & 0xFF;
    buf[len + 1] = crc >> 8;
    return len + 2;
}

static bool crcOk(const uint8_t *buf, size_t len)
{
    return len >= 4 && crc16(buf, len - 2) == (buf[len - 2] | buf[len - 1] << 8);
}

size_t buildRead(uint8_t *out, uint8_t addr, uint8_t fc, uint16_t reg, uint16_t count)
{
    out[0] = addr;
    out[1] = fc;
    out[2] = reg >> 8;
    out[3] = reg & 0xFF;
    out[4] = count >> 8;
    out[5] = count & 0xFF;
    return appendCrc(out, 6);
}

Result checkResponse(const uint8_t *buf, size_t len, uint8_t addr, uint8_t fc, uint16_t count)
{
    if (len && buf[0] != addr)
        return RESP_INVALID;
    if (len < 3)
        return RESP_INCOMPLETE;
    size_t need;
    if (buf[1] == (fc | 0x80))
        need = 5;
    else if (buf[1] == fc && buf[2] == count * 2)
        need = 5 + buf[2];
    else
        return RESP_INVALID;
    if (len < need)
        return RESP_INCOMPLETE;
    // Trailing bytes are line noise; only the frame itself must check out.
    if (!crcOk(buf, need))
        return RESP_INVALID;
    return buf[1] == fc ? RESP_OK : RESP_EXCEPTION;
}

void storeRegisters(int32_t *raw, uint16_t reg, const uint8_t *data, uint8_t byteCount)
{
    for (uint8_t i = 0; i + 4 <= byteCount; i += 4, reg += 2) {
        if (reg / 2 < REG_SLOTS)
            raw[reg / 2] = (int32_t)((uint32_t)data[i] << 24 | (uint32_t)data[i + 1] << 16 | data[i + 2] << 8 | data[i + 3]);
    }
}

uint8_t capsForAddress(uint8_t addr)
{
    for (size_t i = 0; i < profileCount; i++) {
        if (profiles[i].addr == addr)
            return profiles[i].caps;
    }
    return CAPS_S700;
}

bool nextRead(uint8_t caps, bool split, uint8_t &step, uint16_t &reg, uint16_t &count)
{
    uint8_t have = caps | (split ? NEED_SPLIT : NEED_FULL);
    for (; step < sizeof(blocks) / sizeof(blocks[0]); step++) {
        if ((blocks[step].need & have) == blocks[step].need) {
            reg = blocks[step].reg;
            count = blocks[step].count;
            return true;
        }
    }
    return false;
}

// Register slots (register / 2) of the SenseCAP ONE input registers in use.
enum : uint8_t {
    R_TEMP = 0,
    R_HUM = 1,
    R_PRESS = 2,
    R_LUX = 3,
    R_DIR = 6,
    R_LULL = 7,
    R_GUST = 8,
    R_SPEED = 9,
    R_RAIN_ACC = 10,
    R_RAIN_1H = 12,
    R_PM25 = 24,
    R_PM10 = 25,
    R_CO2 = 32
};

// sinf/cosf/atan2f link about 4 KB of libm on STM32; wind direction needs neither their range nor their precision.

/// sin of an angle in [-90, 450) degrees, error below 2e-4.
static float sinDeg(float d)
{
    if (d >= 270)
        d -= 360;
    if (d > 90)
        d = 180 - d;
    float x = d * (float)(M_PI / 180);
    float x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42)));
}

/// Direction of the vector (x, y) in [0, 360] degrees, error below 0.001 degrees.
static float atan2Deg(float y, float x)
{
    float ay = fabsf(y), ax = fabsf(x);
    if (ax == 0 && ay == 0)
        return 0;
    float z = ax > ay ? ay / ax : ax / ay;
    float z2 = z * z;
    float a = z * (0.9998660f + z2 * (-0.3302995f + z2 * (0.1801410f + z2 * (-0.0851330f + z2 * 0.0208351f))));
    a *= (float)(180 / M_PI);
    if (ay > ax)
        a = 90 - a;
    if (x < 0)
        a = 180 - a;
    if (y < 0)
        a = 360 - a;
    return a;
}

void Aggregate::add(const int32_t *raw)
{
    s.temp += raw[R_TEMP];
    s.hum += raw[R_HUM];
    s.press += raw[R_PRESS];
    s.lux += raw[R_LUX];
    s.speed += raw[R_SPEED];
    float dir = raw[R_DIR] / 1000.0f;
    s.dirSin += sinDeg(dir);
    s.dirCos += sinDeg(dir + 90);
    if (!s.n || raw[R_GUST] > s.gust)
        s.gust = raw[R_GUST];
    if (!s.n || raw[R_LULL] < s.lull)
        s.lull = raw[R_LULL];
    s.rain1h = raw[R_RAIN_1H];
    s.pm25 += raw[R_PM25];
    s.pm10 += raw[R_PM10];
    s.co2 += raw[R_CO2];
    s.n++;

    // A counter that went backwards was reset (sensor power cycle, or the wrap at 80000 mm): count from zero.
    int32_t acc = raw[R_RAIN_ACC];
    int32_t delta = acc - lastRain;
    if (lastRain < 0)
        delta = 0;
    else if (delta < 0)
        delta = acc;
    rainHour[rainIdx] += delta;
    lastRain = acc;
}

void Aggregate::environment(meshtastic_EnvironmentMetrics &e, uint8_t caps) const
{
    if (!s.n)
        return;
    float k = 1.0f / (1000 * s.n);
    if (caps & CAP_THP) {
        e.has_temperature = e.has_relative_humidity = e.has_barometric_pressure = true;
        e.temperature = s.temp * k;
        e.relative_humidity = s.hum * k;
        e.barometric_pressure = s.press * k / 100; // Pa -> hPa
    }
    if (caps & CAP_LIGHT) {
        e.has_lux = true;
        e.lux = s.lux * k;
    }
    if (caps & CAP_WIND) {
        e.has_wind_direction = e.has_wind_speed = e.has_wind_gust = e.has_wind_lull = true;
        e.wind_direction = (uint32_t)(atan2Deg(s.dirSin, s.dirCos) + 0.5f) % 360;
        e.wind_speed = s.speed * k;
        e.wind_gust = s.gust / 1000.0f;
        e.wind_lull = s.lull / 1000.0f;
    }
    if (caps & CAP_RAIN) {
        int32_t day = 0;
        for (int32_t h : rainHour)
            day += h;
        e.has_rainfall_1h = e.has_rainfall_24h = true;
        e.rainfall_1h = s.rain1h / 1000.0f;
        e.rainfall_24h = day / 1000.0f;
    }
}

void Aggregate::airQuality(meshtastic_AirQualityMetrics &a, uint8_t caps) const
{
    if (!s.n)
        return;
    float k = 1.0f / (1000 * s.n);
    if (caps & CAP_PM) {
        // pm10_standard is PM1.0 in the proto; PM10 goes to pm100_standard.
        a.has_pm25_standard = a.has_pm100_standard = true;
        a.pm25_standard = s.pm25 * k + 0.5f;
        a.pm100_standard = s.pm10 * k + 0.5f;
    }
    if (caps & CAP_CO2) {
        a.has_co2 = true;
        a.co2 = s.co2 * k + 0.5f;
    }
}

} // namespace modbus

#endif
