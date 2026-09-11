// RAK SensorHub uplink: decode IPSO payloads into EnvCache and fill meshtastic_Telemetry.
// 1-Wire framing, probe polling, and hot-plug live in RAKSensorHub.cpp.
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(HAS_RAKHUB)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include <onewire_master_api.h>

#include <cstdint>
#include <cstdio>

namespace RAKSensorHubUplink
{

struct ScalarReading {
    float value = 0.0f;
    uint32_t lastUpdateMs = 0;
    bool valid = false;
};

struct AccelReading {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint32_t lastUpdateMs = 0;
    bool valid = false;
};

struct HubPower {
    uint16_t volMv = 0;  // 0xBA
    int16_t curMa = 0;   // 0xB9
    uint8_t percent = 0; // 0xB8
};

struct EnvCache {
    // IPSO IDs from RAK-OneWireSerial (CayenneLPP-style offset from 3200).
    ScalarReading temperature;             // 0x67 TEMP_SENSOR
    ScalarReading humidity;                // 0x68 HUMIDITY_SENSOR
    ScalarReading pressure;                // 0x73 BAROMETER
    ScalarReading distance;                // 0x82 custom AIC distance (not IPSO DISTANCE)
    ScalarReading high_precision_humidity; // 0x70 HP_HUMIDITY
    ScalarReading moisture;                // 0xBC MOISTURE
    ScalarReading high_precision_ec;       // 0x7F HP_EC
    ScalarReading wind_speed;              // 0xBE WIND
    ScalarReading wind_direction;          // 0xBF WIND_DIR
    ScalarReading solar_irradiance;        // 0xC3 PYRANOMETER (W/m²; not Geiger radiation)
    ScalarReading lux;                     // 0x65 ILLUM_SENSOR
    ScalarReading ph_hp;                   // 0xC1 HP_PH (/100)
    ScalarReading ph_lp;                   // 0xC2 PH (/10)
    ScalarReading salinity;                // 0x13 SALINITY
    ScalarReading ec;                      // 0xC0 EC (also filled from 0x7F)
    ScalarReading nitrogen;                // 0x10 NITROGEN
    ScalarReading phosphorus;              // 0x11 PHOSPHORUS
    ScalarReading potassium;               // 0x12 POTASSIUM
    ScalarReading dissolved_oxygen;        // 0x14 DISS_OXYGEN
    ScalarReading orp;                     // 0x15 ORP
    ScalarReading cod;                     // 0x16 COD
    ScalarReading turbidity;               // 0x17 TURBIDITY
    ScalarReading nitrate;                 // 0x18 NO3
    ScalarReading ammonium;                // 0x19 NH4PLUS
    ScalarReading bod;                     // 0x1A BOD
    ScalarReading co2;                     // 0x7D CO2
    ScalarReading digital_input;           // 0x00 DIGITAL_INPUT
    ScalarReading digital_output;          // 0x01 DIGITAL_OUTPUT
    AccelReading accel;                    // 0x71 ACCELEROMETER
};

// Power module cache (IPSO 0xB8 capacity / 0xB9 current / 0xBA voltage).
static HubPower power;

// Environment sensor cache; add field in EnvCache and assign in switch when
// adding new IPSO
static EnvCache env;

// Per-PID IPSO signature so 0x70 is air RH (weather) or soil moisture, never both.
static constexpr int kMaxProbeSigs = 8;
static constexpr uint16_t SIG_PRESS = 1u << 0;
static constexpr uint16_t SIG_WIND = 1u << 1;
static constexpr uint16_t SIG_WIND_DIR = 1u << 2;
static constexpr uint16_t SIG_EC = 1u << 3;
static constexpr uint16_t SIG_MOISTURE = 1u << 4;
static constexpr uint16_t SIG_SOIL_GE = 1u << 5;
static constexpr uint16_t SIG_NPK = 1u << 6;

struct ProbeSig {
    uint8_t pid = 0;
    bool used = false;
    uint16_t mask = 0;
};

static ProbeSig probeSigs[kMaxProbeSigs];
static uint8_t hpHumidityPid = 0;
static bool hpHumidityPidValid = false;

static uint16_t ipsoToSigBit(uint8_t ipso)
{
    switch (ipso) {
    case RAK_IPSO_BAROMETER:
        return SIG_PRESS;
    case RAK_IPSO_WIND:
        return SIG_WIND;
    case RAK_IPSO_WIND_DIR:
        return SIG_WIND_DIR;
    case RAK_IPSO_HP_EC:
    case RAK_IPSO_EC:
        return SIG_EC;
    case RAK_IPSO_MOISTURE:
        return SIG_MOISTURE;
    case RAK_IPSO_NITROGEN:
    case RAK_IPSO_PHOSPHORUS:
    case RAK_IPSO_POTASSIUM:
    case RAK_IPSO_SALINITY:
        return SIG_NPK;
    default:
        return 0;
    }
}

static void noteProbeMask(uint8_t pid, uint16_t bits)
{
    if (bits == 0)
        return;
    for (int i = 0; i < kMaxProbeSigs; i++) {
        if (probeSigs[i].used && probeSigs[i].pid == pid) {
            probeSigs[i].mask = (uint16_t)(probeSigs[i].mask | bits);
            return;
        }
    }
    for (int i = 0; i < kMaxProbeSigs; i++) {
        if (!probeSigs[i].used) {
            probeSigs[i].used = true;
            probeSigs[i].pid = pid;
            probeSigs[i].mask = bits;
            return;
        }
    }
    probeSigs[0].used = true;
    probeSigs[0].pid = pid;
    probeSigs[0].mask = bits;
}

static uint16_t probeMask(uint8_t pid)
{
    for (int i = 0; i < kMaxProbeSigs; i++) {
        if (probeSigs[i].used && probeSigs[i].pid == pid)
            return probeSigs[i].mask;
    }
    return 0;
}

/** Write a scalar sensor reading: update value, valid flag and last update time
 * (used when parsing IPSO into env.*). */
static inline void setScalar(ScalarReading &r, float v, uint32_t nowMs)
{
    r.value = v;
    r.valid = true;
    r.lastUpdateMs = nowMs;
}

static void setHpHumidity(float v, uint32_t nowMs, uint8_t pid)
{
    setScalar(env.high_precision_humidity, v, nowMs);
    hpHumidityPid = pid;
    hpHumidityPidValid = true;
}

static bool hpHumidityIsSoil(uint8_t pid)
{
    const uint16_t m = probeMask(pid);
    const bool weather = (m & (SIG_PRESS | SIG_WIND | SIG_WIND_DIR)) != 0;
    const bool soil = (m & (SIG_EC | SIG_MOISTURE | SIG_SOIL_GE | SIG_NPK)) != 0;
    return soil && !weather;
}

static void logIpoRawHex(const char *tag, uint8_t ipso, uint8_t *msg, uint16_t len)
{
    char hex[96] = {0};
    size_t o = 0;
    const uint16_t n = len < 24 ? len : 24;
    for (uint16_t i = 0; i < n && o + 3 < sizeof(hex); i++) {
        o += (size_t)snprintf(&hex[o], sizeof(hex) - o, "%02X ", (unsigned)msg[i]);
    }
    LOG_INFO("%s IPSO[%02x] raw(len=%u): %s", tag, (unsigned)ipso, (unsigned)len, hex);
}

/** Parse DI/DO IPSO payloads (core do_di_upload: 1 data byte after ipso).
 * Returns true if handled. */
static bool parseDigitalIpso(uint8_t ipso, uint8_t *msg, uint16_t len, const char *via)
{
    if (len < 2)
        return false;
    const uint32_t now = millis();
    const uint8_t raw = msg[1];
    if (ipso == RAK_IPSO_DIGITAL_INPUT) {
        setScalar(env.digital_input, (float)raw, now);
        LOG_INFO("DI %s IPSO[00] value=%u (0=low 1=high); toggle PB13 to re-test", via, (unsigned)raw);
        return true;
    }
    if (ipso == RAK_IPSO_DIGITAL_OUTPUT) {
        setScalar(env.digital_output, (float)raw, now);
        LOG_INFO("DO %s IPSO[01] value=%u", via, (unsigned)raw);
        return true;
    }
    return false;
}

/** IPSO 0x7D: 2-byte ppm, or 4-byte LE when ProbeIO ADDPOLL dtype=6. */
static bool parseCo2Ipso(uint8_t *msg, uint16_t len, const char *via)
{
    (void)via;
    if (len < 3)
        return false;
    uint32_t raw = 0;
    if (len >= 5) {
        raw = (uint32_t)msg[1] | ((uint32_t)msg[2] << 8) | ((uint32_t)msg[3] << 16) | ((uint32_t)msg[4] << 24);
        if (raw == 0 || raw > 5000)
            raw = 0;
    }
    if (raw == 0) {
        const uint16_t le = (uint16_t)msg[1] | ((uint16_t)msg[2] << 8);
        const uint16_t be = ((uint16_t)msg[1] << 8) | (uint16_t)msg[2];
        raw = le;
        if ((le == 0 || le > 5000) && be > 0 && be <= 5000)
            raw = be;
    }
    if (raw == 0 || raw > 5000)
        return false;
    setScalar(env.co2, (float)raw, millis());
    LOG_INFO("CO2: %.0f ppm", env.co2.value);
    return true;
}

/** Return true if scalar reading is within validity window: valid, non-zero
 * timestamp, and not older than maxAgeMs. */
static inline bool scalarFresh(const ScalarReading &r, uint32_t nowMs, uint32_t maxAgeMs)
{
    return r.valid && r.lastUpdateMs != 0 && (nowMs - r.lastUpdateMs) <= maxAgeMs;
}

#if defined(RAK_SENSORHUB_EXTENDED_ENV_METRICS) && RAK_SENSORHUB_EXTENDED_ENV_METRICS

/** Optional on-air export of soil/water chemistry + solar irradiance.
 * Still targets legacy EnvironmentMetrics tags; retarget to SoilWaterMetrics after
 * firmware protobuf sync (protobufs#1071). Off by default: parsed into EnvCache only. */
static bool fillExtendedEnvironmentMetrics(meshtastic_EnvironmentMetrics *m, uint32_t now, uint32_t maxAgeMs)
{
    bool any = false;
    // Proto soil_ph/ph map IPSO 0xC1/0xC2 (high/low precision), not soil vs water.
    if (scalarFresh(env.ph_hp, now, maxAgeMs)) {
        m->has_soil_ph = true;
        m->soil_ph = env.ph_hp.value;
        any = true;
    }
    if (scalarFresh(env.ph_lp, now, maxAgeMs)) {
        m->has_ph = true;
        m->ph = env.ph_lp.value;
        any = true;
    }
    if (scalarFresh(env.high_precision_ec, now, maxAgeMs)) {
        m->has_electrical_conductivity = true;
        m->electrical_conductivity = env.high_precision_ec.value;
        any = true;
    } else if (scalarFresh(env.ec, now, maxAgeMs)) {
        m->has_electrical_conductivity = true;
        m->electrical_conductivity = env.ec.value;
        any = true;
    }
    if (scalarFresh(env.salinity, now, maxAgeMs)) {
        m->has_salinity = true;
        m->salinity = env.salinity.value;
        any = true;
    }
    if (scalarFresh(env.nitrogen, now, maxAgeMs)) {
        m->has_nitrogen = true;
        m->nitrogen = env.nitrogen.value;
        any = true;
    }
    if (scalarFresh(env.phosphorus, now, maxAgeMs)) {
        m->has_phosphorus = true;
        m->phosphorus = env.phosphorus.value;
        any = true;
    }
    if (scalarFresh(env.potassium, now, maxAgeMs)) {
        m->has_potassium = true;
        m->potassium = env.potassium.value;
        any = true;
    }
    if (scalarFresh(env.dissolved_oxygen, now, maxAgeMs)) {
        m->has_dissolved_oxygen = true;
        m->dissolved_oxygen = env.dissolved_oxygen.value;
        any = true;
    }
    if (scalarFresh(env.orp, now, maxAgeMs)) {
        m->has_orp = true;
        m->orp = env.orp.value;
        any = true;
    }
    if (scalarFresh(env.cod, now, maxAgeMs)) {
        m->has_chemical_oxygen_demand = true;
        m->chemical_oxygen_demand = env.cod.value;
        any = true;
    }
    if (scalarFresh(env.turbidity, now, maxAgeMs)) {
        m->has_turbidity = true;
        m->turbidity = env.turbidity.value;
        any = true;
    }
    if (scalarFresh(env.nitrate, now, maxAgeMs)) {
        m->has_nitrate = true;
        m->nitrate = env.nitrate.value;
        any = true;
    }
    if (scalarFresh(env.ammonium, now, maxAgeMs)) {
        m->has_ammonium = true;
        m->ammonium = env.ammonium.value;
        any = true;
    }
    if (scalarFresh(env.bod, now, maxAgeMs)) {
        m->has_biochemical_oxygen_demand = true;
        m->biochemical_oxygen_demand = env.bod.value;
        any = true;
    }
    if (scalarFresh(env.solar_irradiance, now, maxAgeMs)) {
        m->has_solar_irradiance = true;
        m->solar_irradiance = env.solar_irradiance.value;
        any = true;
    }
    // IPSO 0x71 accelerometer: needs protobuf acceleration_x/y/z before on-air export.
    return any;
}

#endif // RAK_SENSORHUB_EXTENDED_ENV_METRICS

static inline bool isAllZero(const uint8_t *p, uint16_t n)
{
    if (p == nullptr)
        return true;
    for (uint16_t i = 0; i < n; i++) {
        if (p[i] != 0)
            return false;
    }
    return true;
}

/** IPSO 0xF1 (MODBUS): RS485 Generic Engine soil/water probe payloads from ProbeIO.
 * Not a scalar IPSO - taskId selects which register block was read. Maps GE tasks to
 * EnvCache (water content → 0x70 path, temp, salinity, EC). Ignores all-zero padding
 * slots in periodic get.data() responses. sid is the IOC task id when layout B is used. */
static bool parseConfiguredModbusReading(uint8_t pid, uint8_t taskId, uint8_t *msg, uint16_t len)
{
    const uint8_t *modbus = nullptr;
    uint8_t modbusLen = 0;

    if (len < 2 || msg[0] != RAK_IPSO_MODBUS)
        return false;

    // Many ProbeIO firmwares include a fixed 64-byte IPSO[F1] slot in get.data()
    // responses even when no IOC upload data is pending. That slot is often
    // all-zero. Do not treat it as a link error.
    if (isAllZero(&msg[1], (uint16_t)(len - 1))) {
        return false;
    }

    /* Two possible payload layouts exist:
     * A) [F1][len][modbus...]
     * B) [F1][taskId][type=F1][datalen][modbus...]
     */
    if (len >= 5 && msg[2] == RAK_IPSO_MODBUS && msg[3] <= (len - 4)) {
        modbusLen = msg[3];
        modbus = &msg[4];
        taskId = msg[1];
        LOG_DEBUG("GE MODBUS raw task=%u len=%u head=%02x %02x %02x %02x %02x %02x %02x", (unsigned)taskId, (unsigned)modbusLen,
                  (unsigned)modbus[0], (unsigned)modbus[1], (unsigned)modbus[2], (unsigned)modbus[3], (unsigned)modbus[4],
                  (unsigned)modbus[5], (unsigned)modbus[6]);
    } else if (len >= 3 && msg[1] <= (len - 2)) {
        modbusLen = msg[1];
        modbus = &msg[2];
        LOG_DEBUG("GE MODBUS raw task=%u len=%u head=%02x %02x %02x %02x %02x %02x %02x", (unsigned)taskId, (unsigned)modbusLen,
                  (unsigned)modbus[0], (unsigned)modbus[1], (unsigned)modbus[2], (unsigned)modbus[3], (unsigned)modbus[4],
                  (unsigned)modbus[5], (unsigned)modbus[6]);
    } else {
        modbusLen = (uint8_t)(len - 1);
        modbus = &msg[1];
    }

    // If the "slot" is present but empty, skip quietly (avoid log spam during
    // periodic get.data polls).
    if (modbusLen == 0) {
        return false;
    }

    if (modbusLen < 7 || modbus[1] != 0x03 || modbus[2] < 2) {
        LOG_INFO("GE MODBUS task=%u invalid response len=%u", (unsigned)taskId, (unsigned)modbusLen);
        return false;
    }

    uint16_t raw = ((uint16_t)modbus[3] << 8) | modbus[4];
    uint32_t now = millis();

    switch (taskId) {
    case 0:   // Pass-through immediate read; same mapping as task 1.
    case 1: { // Water content / IPSO 0x70, scale 0.1 %
        float water = raw / 10.0f;
        if (water >= 0.0f && water <= 100.0f) {
            noteProbeMask(pid, SIG_SOIL_GE);
            setHpHumidity(water, now, pid);
            LOG_INFO("GE water content(task=%u): raw=%u, %.1f %%", (unsigned)taskId, (unsigned)raw, water);
            return true;
        }
        break;
    }
    case 2: { // Temperature, scale 0.1 C
        int16_t signedRaw = (int16_t)raw;
        float temperature = signedRaw / 10.0f;
        if (temperature >= -50.0f && temperature <= 130.0f) {
            setScalar(env.temperature, temperature, now);
            LOG_INFO("GE temperature(task=2): raw=%d, %.1f C", (int)signedRaw, temperature);
            return true;
        }
        break;
    }
    case 3: { // Salinity, scale 1 mg/L
        noteProbeMask(pid, SIG_SOIL_GE);
        setScalar(env.salinity, (float)raw, now);
        LOG_INFO("GE salinity(task=3): raw=%u, %.0f mg/L", (unsigned)raw, env.salinity.value);
        return true;
    }
    case 4: { // Conductivity, scale 0.001 mS/cm
        float ec_ms = raw / 1000.0f;
        noteProbeMask(pid, SIG_SOIL_GE);
        setScalar(env.ec, ec_ms, now);
        LOG_INFO("GE conductivity(task=4): raw=%u, %.3f mS/cm", (unsigned)raw, ec_ms);
        return true;
    }
    default:
        LOG_INFO("GE MODBUS task=%u not mapped", (unsigned)taskId);
        return false;
    }

    LOG_INFO("GE MODBUS task=%u raw=%u out of range", (unsigned)taskId, (unsigned)raw);
    return false;
}

static void logIpsoEventPrefix(const char *via, uint8_t pid, uint8_t ipso)
{
    (void)via;
    LOG_INFO("+EVT:PID[%02x],IPSO[%02x]", pid, ipso);
}

static void logIpsoRawIfNeeded(const char *via, uint8_t *msg, uint16_t len)
{
    if (msg[0] == RAK_IPSO_DIGITAL_INPUT || msg[0] == RAK_IPSO_DIGITAL_OUTPUT) {
        logIpoRawHex(via, msg[0], msg, len);
    } else if (msg[0] == 0x82 || msg[0] == RAK_IPSO_ANALOG_INPUT) {
        char hex[160] = {0};
        size_t o = 0;
        for (uint16_t i = 0; i < len && o + 3 < sizeof(hex); i++) {
            o += (size_t)snprintf(&hex[o], sizeof(hex) - o, "%02X ", (unsigned)msg[i]);
        }
        LOG_INFO("%s IPSO[%02x] raw(len=%u): %s", via, (unsigned)msg[0], (unsigned)len, hex);
    }
}

static bool parseIpsoEvent(const char *via, uint8_t pid, uint8_t sid, uint8_t *msg, uint16_t len)
{
    const uint32_t now = millis();
    switch (msg[0]) {
    case RAK_IPSO_DIGITAL_INPUT:
    case RAK_IPSO_DIGITAL_OUTPUT:
        return parseDigitalIpso(msg[0], msg, len, via);
    case RAK_IPSO_MODBUS:
        return parseConfiguredModbusReading(pid, sid, msg, len);
    case 0x82: { // Custom IPSO used by some AIC templates (e.g. WisToolBox:
                 // io_decode ... IPSO=130)
        if (len >= 5) {
            uint32_t raw = (uint32_t)msg[1] | ((uint32_t)msg[2] << 8) | ((uint32_t)msg[3] << 16) | ((uint32_t)msg[4] << 24);
            LOG_INFO("%s AIC IPSO[0x82] raw32=%lu (0x%08lx)", via, (unsigned long)raw, (unsigned long)raw);
            setScalar(env.distance, (float)raw, now);
            return true;
        } else if (len >= 3) {
            uint16_t raw16 = ((uint16_t)msg[2] << 8) | msg[1];
            LOG_INFO("%s AIC IPSO[0x82] raw16=%u (0x%04x)", via, (unsigned)raw16, (unsigned)raw16);
            setScalar(env.distance, (float)raw16, now);
            return true;
        }
        return false;
    }
    case RAK_IPSO_ANALOG_INPUT: { // 0x02 standard analog input (2 bytes,
                                  // little-endian)
        if (len < 3)
            return false;
        uint16_t raw16 = ((uint16_t)msg[2] << 8) | msg[1];
        LOG_INFO("%s analog input IPSO[0x02] raw=%u", via, (unsigned)raw16);
        return true;
    }
    case RAK_IPSO_TEMP_SENSOR: {
        if (len < 3)
            return false;
        int16_t temp_raw = (msg[2] << 8) + msg[1];
        float temperature = temp_raw / 10.0f;
        if (temperature < -50.0f || temperature > 130.0f) {
            LOG_INFO("Ignore temperature value out of range: %.2f C", temperature);
            return false;
        }
        setScalar(env.temperature, temperature, now);
        LOG_INFO("Temperature: %.2f C", temperature);
        return true;
    }
    case RAK_IPSO_HUMIDITY_SENSOR: {
        if (len < 2)
            return false;
        float humidity = (float)msg[1];
        if (humidity < 0.0f || humidity > 100.0f) {
            LOG_INFO("Ignore humidity value out of range: %.2f %%", humidity);
            return false;
        }
        setScalar(env.humidity, humidity, now);
        LOG_INFO("Humidity: %.2f %%", humidity);
        return true;
    }
    case RAK_IPSO_HP_HUMIDITY: {
        if (len < 3)
            return false;
        int16_t raw = (msg[2] << 8) + msg[1];
        float moisture = raw / 10.0f;
        if (moisture < 0.0f || moisture > 100.0f) {
            LOG_INFO("Ignore high precision humidity value out of range: %.1f %%", moisture);
            return false;
        }
        setHpHumidity(moisture, now, pid);
        LOG_INFO("High precision humidity: %.1f %%", moisture);
        return true;
    }
    case RAK_IPSO_BAROMETER: {
        if (len < 3)
            return false;
        int16_t press_raw = (msg[2] << 8) + msg[1];
        float pressure = press_raw / 10.0f;
        if (pressure < 100.0f || pressure > 1100.0f) {
            LOG_INFO("Ignore barometric pressure value out of range: %.1f hPa", pressure);
            return false;
        }
        setScalar(env.pressure, pressure, now);
        LOG_INFO("Barometric pressure: %.1f hPa", pressure);
        return true;
    }
    case RAK_IPSO_CO2:
        return parseCo2Ipso(msg, len, via);
    case RAK_IPSO_HP_EC: {
        if (len < 5)
            return false;
        uint32_t raw = (uint32_t)msg[1] | ((uint32_t)msg[2] << 8) | ((uint32_t)msg[3] << 16) | ((uint32_t)msg[4] << 24);
        if (raw == 0 && env.high_precision_ec.valid) {
            LOG_INFO("High precision EC (0x7F): raw=0 (skip overwrite)");
            return false;
        }
        float ec_ms = raw / 1000000.0f;
        setScalar(env.high_precision_ec, ec_ms, now);
        setScalar(env.ec, ec_ms, now);
        LOG_INFO("High precision EC (0x7F): raw=%lu, %.3f (mS/cm units)", (unsigned long)raw, ec_ms);
        return true;
    }
    case RAK_IPSO_WIND: {
        if (len < 3)
            return false;
        int16_t raw = (msg[2] << 8) + msg[1];
        float ws = raw / 100.0f;
        if (ws < 0.0f || ws > 60.0f) {
            LOG_INFO("Ignore wind speed value out of range: %.2f m/s", ws);
            return false;
        }
        setScalar(env.wind_speed, ws, now);
        LOG_INFO("Wind speed: %.2f m/s", ws);
        return true;
    }
    case RAK_IPSO_WIND_DIR: {
        if (len < 3)
            return false;
        uint16_t raw = (msg[2] << 8) + msg[1];
        setScalar(env.wind_direction, (float)(raw % 360), now);
        LOG_INFO("Wind direction: %u deg", (unsigned)(raw % 360));
        return true;
    }
    case RAK_IPSO_PYRANOMETER: {
        if (len < 3)
            return false;
        uint16_t raw = (uint16_t)((msg[2] << 8) + msg[1]);
        float rad = (float)raw;
        if (rad < 0.0f || rad > 2000.0f) {
            LOG_INFO("Ignore solar irradiance out of range: %.1f W/m²", rad);
            return false;
        }
        setScalar(env.solar_irradiance, rad, now);
        LOG_INFO("Solar irradiance: %.1f W/m²", env.solar_irradiance.value);
        return true;
    }
    case RAK_IPSO_ILLUM_SENSOR: {
        // IPSO 0x65 / 101: 4-byte LE lux (ProbeIO dtype=6) or 2-byte LE fallback.
        if (len < 3)
            return false;
        uint32_t raw = 0;
        if (len >= 5)
            raw = (uint32_t)msg[1] | ((uint32_t)msg[2] << 8) | ((uint32_t)msg[3] << 16) | ((uint32_t)msg[4] << 24);
        else
            raw = (uint32_t)msg[1] | ((uint32_t)msg[2] << 8);
        if (raw > 200000) {
            LOG_INFO("Ignore illuminance out of range: %lu lux", (unsigned long)raw);
            return false;
        }
        setScalar(env.lux, (float)raw, now);
        LOG_INFO("Illuminance: %.0f lux", env.lux.value);
        return true;
    }
    case RAK_IPSO_CAPACITY:
        if (len < 2)
            return false;
        power.percent = msg[1];
        if (power.percent > 100)
            power.percent = 100;
        LOG_INFO("Battery capacity: %u %%", (unsigned)power.percent);
        return true;
    case RAK_IPSO_DC_CURRENT: {
        if (len < 3)
            return false;
        uint16_t raw = (uint16_t)((msg[2] << 8) + msg[1]);
        float amps = raw * 0.01f;
        if (amps > 50.0f) {
            LOG_INFO("Ignore battery current out of range: %.3f A", amps);
            return false;
        }
        power.curMa = (int16_t)(amps * 1000.0f);
        LOG_INFO("Battery current: %.3f A", (float)power.curMa / 1000.0f);
        return true;
    }
    case RAK_IPSO_DC_VOLTAGE: {
        if (len < 3)
            return false;
        uint16_t raw = (uint16_t)((msg[2] << 8) + msg[1]);
        float volts = raw * 0.01f;
        if (volts < 0.0f || volts > 20.0f) {
            LOG_INFO("Ignore battery voltage out of range: %.2f V", volts);
            return false;
        }
        power.volMv = (uint16_t)(volts * 1000.0f);
        LOG_INFO("Battery voltage: %.2f V", volts);
        return true;
    }
    case RAK_IPSO_HP_PH: {
        if (len < 3)
            return false;
        int16_t raw = (msg[2] << 8) + msg[1];
        float ph = raw / 100.0f;
        if (ph < 0.0f || ph > 14.0f) {
            LOG_INFO("Ignore high-precision pH value out of range: %.2f", ph);
            return false;
        }
        setScalar(env.ph_hp, ph, now);
        LOG_INFO("pH (high precision, 0xC1): %.2f", ph);
        return true;
    }
    case RAK_IPSO_PH: {
        if (len < 3)
            return false;
        int16_t raw = (msg[2] << 8) + msg[1];
        if (raw == 0 && env.ph_lp.valid) {
            LOG_INFO("pH (0xC2): raw=0 (skip overwrite)");
            return false;
        }
        float ph = raw / 10.0f;
        if (ph < 0.0f || ph > 14.0f) {
            LOG_INFO("Ignore low-precision pH value out of range: %.2f", ph);
            return false;
        }
        setScalar(env.ph_lp, ph, now);
        LOG_INFO("pH (low precision, 0xC2): %.2f", ph);
        return true;
    }
    case RAK_IPSO_ACCELEROMETER: {
        if (len < 7)
            return false;
        int16_t x = (int16_t)((msg[2] << 8) + msg[1]);
        int16_t y = (int16_t)((msg[4] << 8) + msg[3]);
        int16_t z = (int16_t)((msg[6] << 8) + msg[5]);
        env.accel.x = x / 1000.0f;
        env.accel.y = y / 1000.0f;
        env.accel.z = z / 1000.0f;
        env.accel.valid = true;
        env.accel.lastUpdateMs = now;
        LOG_INFO("Accelerometer (0x71): X=%.3f Y=%.3f Z=%.3f", env.accel.x, env.accel.y, env.accel.z);
        return true;
    }
    case RAK_IPSO_SALINITY: {
        if (len < 3)
            return false;
        uint16_t raw = (uint16_t)((msg[2] << 8) + msg[1]);
        if (raw == 0 && env.salinity.valid) {
            LOG_INFO("Salinity: raw=0 (skip overwrite)");
            return false;
        }
        setScalar(env.salinity, (float)raw, now);
        LOG_INFO("Salinity: %u mg/L", (unsigned)raw);
        return true;
    }
    case RAK_IPSO_EC: {
        if (len < 3)
            return false;
        uint16_t raw = (uint16_t)((msg[2] << 8) + msg[1]);
        if (raw == 0 && env.ec.valid) {
            LOG_INFO("EC: raw=0 (skip overwrite)");
            return false;
        }
        setScalar(env.ec, raw / 1000.0f, now);
        LOG_INFO("EC: %.3f (mS/cm units)", env.ec.value);
        return true;
    }
    case RAK_IPSO_NITROGEN: {
        if (len < 3)
            return false;
        uint16_t raw = (uint16_t)((msg[2] << 8) + msg[1]);
        if (raw == 0 && env.nitrogen.valid) {
            LOG_INFO("Nitrogen: raw=0 (skip overwrite)");
            return false;
        }
        setScalar(env.nitrogen, (float)raw, now);
        LOG_INFO("Nitrogen: %.0f mg/kg", env.nitrogen.value);
        return true;
    }
    case RAK_IPSO_PHOSPHORUS: {
        if (len < 3)
            return false;
        uint16_t raw = (uint16_t)((msg[2] << 8) + msg[1]);
        if (raw == 0 && env.phosphorus.valid) {
            LOG_INFO("Phosphorus: raw=0 (skip overwrite)");
            return false;
        }
        setScalar(env.phosphorus, (float)raw, now);
        LOG_INFO("Phosphorus: %.0f mg/kg", env.phosphorus.value);
        return true;
    }
    case RAK_IPSO_POTASSIUM: {
        if (len < 3)
            return false;
        uint16_t raw = (uint16_t)((msg[2] << 8) + msg[1]);
        if (raw == 0 && env.potassium.valid) {
            LOG_INFO("Potassium: raw=0 (skip overwrite)");
            return false;
        }
        setScalar(env.potassium, (float)raw, now);
        LOG_INFO("Potassium: %.0f mg/kg", env.potassium.value);
        return true;
    }
    case RAK_IPSO_DISS_OXYGEN: {
        if (len < 3)
            return false;
        uint16_t raw = (uint16_t)((msg[2] << 8) + msg[1]);
        if (raw == 0 && env.dissolved_oxygen.valid) {
            LOG_INFO("Dissolved oxygen: raw=0 (skip overwrite)");
            return false;
        }
        float dO = raw * 0.01f;
        setScalar(env.dissolved_oxygen, dO, now);
        LOG_INFO("Dissolved oxygen: %.2f mg/L", dO);
        return true;
    }
    case RAK_IPSO_ORP: {
        if (len < 3)
            return false;
        int16_t raw = (int16_t)((msg[2] << 8) + msg[1]);
        if (raw == 0 && env.orp.valid) {
            LOG_INFO("ORP: raw=0 (skip overwrite)");
            return false;
        }
        float orp = raw * 0.1f;
        setScalar(env.orp, orp, now);
        LOG_INFO("ORP: %.1f mV", orp);
        return true;
    }
    case RAK_IPSO_COD: {
        if (len < 3)
            return false;
        uint16_t raw = (uint16_t)((msg[2] << 8) + msg[1]);
        if (raw == 0 && env.cod.valid) {
            LOG_INFO("COD: raw=0 (skip overwrite)");
            return false;
        }
        setScalar(env.cod, (float)raw, now);
        LOG_INFO("COD: %.0f mg/L", env.cod.value);
        return true;
    }
    case RAK_IPSO_TURBIDITY: {
        if (len < 3)
            return false;
        uint16_t raw = (uint16_t)((msg[2] << 8) + msg[1]);
        if (raw == 0 && env.turbidity.valid) {
            LOG_INFO("Turbidity: raw=0 (skip overwrite)");
            return false;
        }
        setScalar(env.turbidity, (float)raw, now);
        LOG_INFO("Turbidity: %.0f NTU", env.turbidity.value);
        return true;
    }
    case RAK_IPSO_NO3: {
        if (len < 3)
            return false;
        uint16_t raw = (uint16_t)((msg[2] << 8) + msg[1]);
        if (raw == 0 && env.nitrate.valid) {
            LOG_INFO("Nitrate: raw=0 (skip overwrite)");
            return false;
        }
        float nitrate = raw * 0.1f;
        setScalar(env.nitrate, nitrate, now);
        LOG_INFO("Nitrate: %.1f ppm", nitrate);
        return true;
    }
    case RAK_IPSO_NH4PLUS: {
        if (len < 3)
            return false;
        uint16_t raw = (uint16_t)((msg[2] << 8) + msg[1]);
        if (raw == 0 && env.ammonium.valid) {
            LOG_INFO("Ammonium: raw=0 (skip overwrite)");
            return false;
        }
        float ammonium = raw * 0.01f;
        setScalar(env.ammonium, ammonium, now);
        LOG_INFO("Ammonium: %.2f ppm", ammonium);
        return true;
    }
    case RAK_IPSO_BOD: {
        if (len < 3)
            return false;
        uint16_t raw = (uint16_t)((msg[2] << 8) + msg[1]);
        if (raw == 0 && env.bod.valid) {
            LOG_INFO("BOD: raw=0 (skip overwrite)");
            return false;
        }
        setScalar(env.bod, (float)raw, now);
        LOG_INFO("BOD: %.0f mg/L", env.bod.value);
        return true;
    }
    case RAK_IPSO_MOISTURE: {
        if (len < 3)
            return false;
        uint16_t raw = (uint16_t)((msg[2] << 8) + msg[1]);
        if (raw == 0 && env.moisture.valid) {
            LOG_INFO("Soil moisture: raw=0 (skip overwrite)");
            return false;
        }
        float moisture = raw * 0.1f;
        if (moisture < 0.0f || moisture > 100.0f) {
            LOG_INFO("Ignore soil moisture out of range: %.1f %%", moisture);
            return false;
        }
        setScalar(env.moisture, moisture, now);
        LOG_INFO("Soil moisture: %.1f %%", moisture);
        return true;
    }
    default:
        if (len >= 2)
            logIpoRawHex(via, msg[0], msg, len);
        return false;
    }
}

bool handleIpsoEvent(const char *via, uint8_t pid, uint8_t sid, uint8_t *msg, uint16_t len)
{
    logIpsoEventPrefix(via, pid, msg[0]);
    logIpsoRawIfNeeded(via, msg, len);
    const bool ok = parseIpsoEvent(via, pid, sid, msg, len);
    if (ok)
        noteProbeMask(pid, ipsoToSigBit(msg[0]));
    return ok;
}

/** Fill measurement variant from EnvCache. air_quality_metrics: CO2 only (IPSO 0x7D).
 * environment_metrics: standard fields always; chemistry/irradiance only when
 * RAK_SENSORHUB_EXTENDED_ENV_METRICS is on (retarget to soil_water_metrics later). */
bool getMetrics(meshtastic_Telemetry *measurement)
{
    bool any = false;
    const uint32_t now = millis();
    // Allow cached readings to be reused for a while because 1-Wire frames can be
    // missed under BLE/app load. Five minutes keeps outdoor use stable.
    const uint32_t maxAgeMs = 5 * 60 * 1000;

    // CO2 is reported in AirQualityMetrics, not EnvironmentMetrics
    if (measurement->which_variant == meshtastic_Telemetry_air_quality_metrics_tag) {
        if (scalarFresh(env.co2, now, maxAgeMs)) {
            measurement->variant.air_quality_metrics.has_co2 = true;
            measurement->variant.air_quality_metrics.co2 =
                (uint32_t)(env.co2.value <= 0 ? 0 : (env.co2.value > 5000 ? 5000 : env.co2.value));
            return true;
        }
        return false;
    }

    if (power.volMv > 0) {
        measurement->variant.environment_metrics.has_voltage = true; // Voltage in V (IPSO 0xBA).
        measurement->variant.environment_metrics.has_current = true; // Current in A (IPSO 0xB9).
        measurement->variant.environment_metrics.voltage = (float)power.volMv / 1000;
        measurement->variant.environment_metrics.current = (float)power.curMa / 1000;
        any = true;
    }

    if (scalarFresh(env.temperature, now, maxAgeMs)) {
        measurement->variant.environment_metrics.has_temperature = true;
        measurement->variant.environment_metrics.temperature = env.temperature.value; // Temperature in °C from RAK environmental
                                                                                      // sensor (IPSO 0x67 TEMPERATURE).
        any = true;
    }
    if (scalarFresh(env.humidity, now, maxAgeMs)) {
        measurement->variant.environment_metrics.has_relative_humidity = true;
        measurement->variant.environment_metrics.relative_humidity = env.humidity.value; // Humidity in % from RAK environmental
                                                                                         // sensor (IPSO 0x68 RELATIVE_HUMIDITY).
        any = true;
    }
    if (scalarFresh(env.pressure, now, maxAgeMs)) {
        measurement->variant.environment_metrics.has_barometric_pressure = true;
        measurement->variant.environment_metrics.barometric_pressure =
            env.pressure.value; // Pressure in hPa from RAK environmental sensor
                                // (IPSO 0x73 BAROMETRIC_PRESSURE).
        any = true;
    }
    if (scalarFresh(env.distance, now, maxAgeMs)) {
        measurement->variant.environment_metrics.has_distance = true;
        measurement->variant.environment_metrics.distance =
            env.distance.value; // Distance in mm (used for water level detection).
        any = true;
    }
    if (scalarFresh(env.wind_speed, now, maxAgeMs)) {
        measurement->variant.environment_metrics.has_wind_speed = true;
        measurement->variant.environment_metrics.wind_speed = env.wind_speed.value; // Wind speed in m/s from RAK environmental
                                                                                    // sensor (IPSO 0xBE WIND_SPEED).
        any = true;
    }
    if (scalarFresh(env.wind_direction, now, maxAgeMs)) {
        measurement->variant.environment_metrics.has_wind_direction = true;
        measurement->variant.environment_metrics.wind_direction =
            (uint16_t)(env.wind_direction.value <= 360 ? env.wind_direction.value
                                                       : 0); // Wind direction in degrees from RAK environmental
                                                             // sensor (IPSO 0xBF WIND_DIRECTION).
        any = true;
    }
    if (scalarFresh(env.moisture, now, maxAgeMs)) {
        measurement->variant.environment_metrics.has_soil_moisture = true;
        measurement->variant.environment_metrics.soil_moisture =
            (uint8_t)(env.moisture.value < 0 ? 0 : (env.moisture.value > 100 ? 100 : (uint32_t)env.moisture.value));
        any = true;
    }
    if (scalarFresh(env.high_precision_humidity, now, maxAgeMs)) {
        // IPSO 0x70 is air RH on weather probes and water content on soil probes.
        float v = env.high_precision_humidity.value;
        const uint8_t pid = hpHumidityPidValid ? hpHumidityPid : 0;
        if (hpHumidityIsSoil(pid)) {
            if (!measurement->variant.environment_metrics.has_soil_moisture) {
                measurement->variant.environment_metrics.has_soil_moisture = true;
                measurement->variant.environment_metrics.soil_moisture = (uint8_t)(v < 0 ? 0 : (v > 100 ? 100 : (uint32_t)v));
            }
        } else if (!measurement->variant.environment_metrics.has_relative_humidity) {
            measurement->variant.environment_metrics.has_relative_humidity = true;
            measurement->variant.environment_metrics.relative_humidity = v;
        }
        any = true;
    }
    if (scalarFresh(env.lux, now, maxAgeMs)) {
        measurement->variant.environment_metrics.has_lux = true;
        measurement->variant.environment_metrics.lux = env.lux.value; // Illuminance in lux (IPSO 0x65).
        any = true;
    }

#if defined(RAK_SENSORHUB_EXTENDED_ENV_METRICS) && RAK_SENSORHUB_EXTENDED_ENV_METRICS
    if (fillExtendedEnvironmentMetrics(&measurement->variant.environment_metrics, now, maxAgeMs))
        any = true;
#endif

    return any;
}

/** Bus voltage in mV from RAK power module (IPSO 0xBA DC_VOLTAGE). */
uint16_t getBusVoltageMv()
{
    return power.volMv;
}

/** Bus current in mA from RAK power module (IPSO 0xB9 DC_CURRENT). */
int16_t getCurrentMa()
{
    return power.curMa;
}

/** Battery capacity 0..100 % from RAK power module (IPSO 0xB8 CAPACITY). */
int getBusBatteryPercent()
{
    return (int)power.percent;
}

/** True if current > 0 (charging). */
bool isCharging()
{
    return (power.curMa > 0) ? true : false;
}

} // namespace RAKSensorHubUplink

#endif // HAS_RAKHUB
