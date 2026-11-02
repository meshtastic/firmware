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

using Setter = void (*)(meshtastic_EnvironmentMetrics &, float);

struct Field {
    const char *name;
    Setter set;
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

#define FLOAT_FIELD(f)                                                                                                           \
    {                                                                                                                            \
        #f, [](meshtastic_EnvironmentMetrics &m, float v) {                                                                      \
            m.f = v;                                                                                                             \
            m.has_##f = true;                                                                                                    \
        }                                                                                                                        \
    }
#define INT_FIELD(f)                                                                                                             \
    {                                                                                                                            \
        #f, [](meshtastic_EnvironmentMetrics &m, float v) {                                                                      \
            m.f = roundAndClamp<decltype(m.f)>(v);                                                                               \
            m.has_##f = true;                                                                                                    \
        }                                                                                                                        \
    }

const Field kFields[] = {
    FLOAT_FIELD(temperature),
    FLOAT_FIELD(relative_humidity),
    FLOAT_FIELD(barometric_pressure),
    FLOAT_FIELD(gas_resistance),
    FLOAT_FIELD(voltage),
    FLOAT_FIELD(current),
    INT_FIELD(iaq),
    FLOAT_FIELD(distance),
    FLOAT_FIELD(lux),
    FLOAT_FIELD(white_lux),
    FLOAT_FIELD(ir_lux),
    FLOAT_FIELD(uv_lux),
    INT_FIELD(wind_direction),
    FLOAT_FIELD(wind_speed),
    FLOAT_FIELD(weight),
    FLOAT_FIELD(wind_gust),
    FLOAT_FIELD(wind_lull),
    FLOAT_FIELD(radiation),
    FLOAT_FIELD(rainfall_1h),
    FLOAT_FIELD(rainfall_24h),
    INT_FIELD(soil_moisture),
    FLOAT_FIELD(soil_temperature),
    FLOAT_FIELD(adc_voltage_ch0),
    FLOAT_FIELD(adc_voltage_ch1),
    FLOAT_FIELD(adc_voltage_ch2),
    FLOAT_FIELD(adc_voltage_ch3),
    FLOAT_FIELD(adc_voltage_ch4),
    FLOAT_FIELD(adc_voltage_ch5),
    FLOAT_FIELD(adc_voltage_ch6),
    FLOAT_FIELD(adc_voltage_ch7),
    FLOAT_FIELD(one_wire_temperature_ch0),
    FLOAT_FIELD(one_wire_temperature_ch1),
    FLOAT_FIELD(one_wire_temperature_ch2),
    FLOAT_FIELD(one_wire_temperature_ch3),
    FLOAT_FIELD(one_wire_temperature_ch4),
    FLOAT_FIELD(one_wire_temperature_ch5),
    FLOAT_FIELD(one_wire_temperature_ch6),
    FLOAT_FIELD(one_wire_temperature_ch7),
    INT_FIELD(lightning_strike_count_1h),
    FLOAT_FIELD(lightning_distance_km),
};

#undef FLOAT_FIELD
#undef INT_FIELD

const Field *findField(const std::string &type)
{
    std::string lower = type;
    for (auto &c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto &field : kFields)
        if (lower == field.name)
            return &field;
    return nullptr;
}

} // namespace

std::string canonicalType(const std::string &type)
{
    const Field *field = findField(type);
    return field ? field->name : "";
}

std::string knownTypes()
{
    std::string out;
    for (const auto &field : kFields)
        out += (out.empty() ? "" : ", ") + std::string(field.name);
    return out;
}

bool apply(meshtastic_EnvironmentMetrics &metrics, const std::string &type, float value)
{
    const Field *field = findField(type);
    if (!field)
        return false;
    field->set(metrics, value);
    return true;
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
