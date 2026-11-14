#include "CommandSensorFields.h"

#if ARCH_PORTDUINO

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace CommandSensorFields
{
namespace
{

template <typename Metrics> struct Field {
    const char *name;
    void (*set)(Metrics &, float);
};

template <typename T> T roundAndClamp(float value)
{
    const double rounded = std::round(static_cast<double>(value));
    if (rounded <= static_cast<double>(std::numeric_limits<T>::min()))
        return std::numeric_limits<T>::min();
    if (rounded >= static_cast<double>(std::numeric_limits<T>::max()))
        return std::numeric_limits<T>::max();
    return static_cast<T>(rounded);
}

#define FLOAT_FIELD(M, f)                                                                                                        \
    {#f, [](M &m, float v) {                                                                                                     \
         m.f = v;                                                                                                                \
         m.has_##f = true;                                                                                                       \
     }}
#define INT_FIELD(M, f)                                                                                                          \
    {#f, [](M &m, float v) {                                                                                                     \
         m.f = roundAndClamp<decltype(m.f)>(v);                                                                                  \
         m.has_##f = true;                                                                                                       \
     }}

using Env = meshtastic_EnvironmentMetrics;
const Field<Env> kEnvironmentFields[] = {
    FLOAT_FIELD(Env, temperature),
    FLOAT_FIELD(Env, relative_humidity),
    FLOAT_FIELD(Env, barometric_pressure),
    FLOAT_FIELD(Env, gas_resistance),
    FLOAT_FIELD(Env, voltage),
    FLOAT_FIELD(Env, current),
    INT_FIELD(Env, iaq),
    FLOAT_FIELD(Env, distance),
    FLOAT_FIELD(Env, lux),
    FLOAT_FIELD(Env, white_lux),
    FLOAT_FIELD(Env, ir_lux),
    FLOAT_FIELD(Env, uv_lux),
    INT_FIELD(Env, wind_direction),
    FLOAT_FIELD(Env, wind_speed),
    FLOAT_FIELD(Env, weight),
    FLOAT_FIELD(Env, wind_gust),
    FLOAT_FIELD(Env, wind_lull),
    FLOAT_FIELD(Env, radiation),
    FLOAT_FIELD(Env, rainfall_1h),
    FLOAT_FIELD(Env, rainfall_24h),
    INT_FIELD(Env, soil_moisture),
    FLOAT_FIELD(Env, soil_temperature),
    FLOAT_FIELD(Env, adc_voltage_ch0),
    FLOAT_FIELD(Env, adc_voltage_ch1),
    FLOAT_FIELD(Env, adc_voltage_ch2),
    FLOAT_FIELD(Env, adc_voltage_ch3),
    FLOAT_FIELD(Env, adc_voltage_ch4),
    FLOAT_FIELD(Env, adc_voltage_ch5),
    FLOAT_FIELD(Env, adc_voltage_ch6),
    FLOAT_FIELD(Env, adc_voltage_ch7),
    FLOAT_FIELD(Env, one_wire_temperature_ch0),
    FLOAT_FIELD(Env, one_wire_temperature_ch1),
    FLOAT_FIELD(Env, one_wire_temperature_ch2),
    FLOAT_FIELD(Env, one_wire_temperature_ch3),
    FLOAT_FIELD(Env, one_wire_temperature_ch4),
    FLOAT_FIELD(Env, one_wire_temperature_ch5),
    FLOAT_FIELD(Env, one_wire_temperature_ch6),
    FLOAT_FIELD(Env, one_wire_temperature_ch7),
    INT_FIELD(Env, lightning_strike_count_1h),
    FLOAT_FIELD(Env, lightning_distance_km),
};

// pm_status_flags is a sensor status bitmask, not a measurement, so it is not offered.
using Aq = meshtastic_AirQualityMetrics;
const Field<Aq> kAirQualityFields[] = {
    INT_FIELD(Aq, pm10_standard),       INT_FIELD(Aq, pm25_standard),
    INT_FIELD(Aq, pm40_standard),       INT_FIELD(Aq, pm100_standard),
    INT_FIELD(Aq, pm10_environmental),  INT_FIELD(Aq, pm25_environmental),
    INT_FIELD(Aq, pm100_environmental), INT_FIELD(Aq, particles_03um),
    INT_FIELD(Aq, particles_05um),      INT_FIELD(Aq, particles_10um),
    INT_FIELD(Aq, particles_25um),      INT_FIELD(Aq, particles_40um),
    INT_FIELD(Aq, particles_50um),      INT_FIELD(Aq, particles_100um),
    FLOAT_FIELD(Aq, particles_tps),     INT_FIELD(Aq, co2),
    FLOAT_FIELD(Aq, co2_temperature),   FLOAT_FIELD(Aq, co2_humidity),
    FLOAT_FIELD(Aq, form_formaldehyde), FLOAT_FIELD(Aq, form_humidity),
    FLOAT_FIELD(Aq, form_temperature),  FLOAT_FIELD(Aq, pm_temperature),
    FLOAT_FIELD(Aq, pm_humidity),       FLOAT_FIELD(Aq, pm_voc_idx),
    FLOAT_FIELD(Aq, pm_nox_idx),
};

#undef FLOAT_FIELD
#undef INT_FIELD

std::string lowercase(const std::string &type)
{
    std::string lower = type;
    for (auto &c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower;
}

template <typename Metrics, size_t N> const Field<Metrics> *findIn(const Field<Metrics> (&fields)[N], const std::string &type)
{
    const std::string lower = lowercase(type);
    for (const auto &field : fields)
        if (lower == field.name)
            return &field;
    return nullptr;
}

template <typename Metrics, size_t N>
bool applyIn(const Field<Metrics> (&fields)[N], Metrics &metrics, const std::string &type, float value)
{
    const Field<Metrics> *field = findIn(fields, type);
    if (!field)
        return false;
    field->set(metrics, value);
    return true;
}

} // namespace

std::string canonicalType(const std::string &type)
{
    if (const auto *field = findIn(kEnvironmentFields, type))
        return field->name;
    if (const auto *field = findIn(kAirQualityFields, type))
        return field->name;
    return "";
}

Message messageFor(const std::string &type)
{
    if (findIn(kEnvironmentFields, type))
        return Message::Environment;
    if (findIn(kAirQualityFields, type))
        return Message::AirQuality;
    return Message::None;
}

std::string knownTypes()
{
    std::string out;
    for (const auto &field : kEnvironmentFields)
        out += (out.empty() ? "" : ", ") + std::string(field.name);
    for (const auto &field : kAirQualityFields)
        out += ", " + std::string(field.name);
    return out;
}

bool apply(meshtastic_EnvironmentMetrics &metrics, const std::string &type, float value)
{
    return applyIn(kEnvironmentFields, metrics, type, value);
}

bool apply(meshtastic_AirQualityMetrics &metrics, const std::string &type, float value)
{
    return applyIn(kAirQualityFields, metrics, type, value);
}

bool parseValue(const std::string &output, float &value)
{
    const char *start = output.c_str();
    while (*start && std::isspace(static_cast<unsigned char>(*start)))
        start++;
    if (!*start)
        return false;

    char *end = nullptr;
    errno = 0;
    const float parsed = std::strtof(start, &end);
    if (end == start || errno == ERANGE || !std::isfinite(parsed))
        return false;
    while (*end && std::isspace(static_cast<unsigned char>(*end)))
        end++;
    if (*end)
        return false;

    value = parsed;
    return true;
}

} // namespace CommandSensorFields

#endif
