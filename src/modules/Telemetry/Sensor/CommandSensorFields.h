#pragma once

#include "configuration.h"

#if ARCH_PORTDUINO

#include "mesh/generated/meshtastic/telemetry.pb.h"
#include <string>

// The EnvironmentMetrics and AirQualityMetrics fields a Sensors: entry in config.yaml can fill, named as in
// the protobuf. No field name appears in both messages.
namespace CommandSensorFields
{

// Which telemetry message, and so which telemetry module, a type belongs to.
enum class Message { None, Environment, AirQuality };

// Canonical (lowercase) field name for type, matched case-insensitively; empty if no field matches.
std::string canonicalType(const std::string &type);

Message messageFor(const std::string &type);

// Every accepted type, comma separated, for error messages.
std::string knownTypes();

// Stores value in the named field and sets its has_ flag. Integer fields are rounded and clamped.
bool apply(meshtastic_EnvironmentMetrics &metrics, const std::string &type, float value);
bool apply(meshtastic_AirQualityMetrics &metrics, const std::string &type, float value);

// Reads one finite number from command output; surrounding whitespace is allowed, anything else is not.
bool parseValue(const std::string &output, float &value);

} // namespace CommandSensorFields

#endif
