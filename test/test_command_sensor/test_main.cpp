// Tests for the config.yaml "Sensors:" feature on Linux (meshtasticd):
//
//   - execCommand() / exec() in src/platform/portduino/PortduinoGlue.cpp
//   - CommandSensorFields::{canonicalType, apply, parseValue} in
//     src/modules/Telemetry/Sensor/CommandSensorFields.cpp
//   - CommandSensor::getMetrics() in src/modules/Telemetry/Sensor/CommandSensor.cpp
//
// Why these contracts matter:
//
// execCommand() runs on the main loop, the same thread that services the radio. A sensor script that
// hangs - a stuck I2C read in Python, a network call with no timeout, a command that closes stdout and
// keeps running - must cost at most its timeout, and the whole pipeline it started must be killed so it
// cannot keep holding the pipe. exec() keeps its old contract for HostMetrics.UserStringCommand: it
// returns whatever the command printed, whether or not it exited 0.
//
// parseValue() is the only thing between arbitrary command output and a field broadcast over the mesh.
// It must take exactly one finite number: "21.5 C", an empty line, an error message or "nan" must be
// rejected rather than truncated to a plausible-looking value or sent as NaN.
//
// apply() writes into nanopb fields of several widths. A value outside a narrow integer field
// (soil_moisture is uint8_t) must clamp, not wrap: 300 wrapping to 44% moisture is a silent lie.
//
// CommandSensor caches readings because getMetrics() is also called for screen refreshes and phone
// requests. Without the cache every command reruns on each of those calls, multiplying the main-loop
// stall; with a cache that never expires the node broadcasts the first reading forever.
//
// Regressions guarded: an unbounded wait on a hung or stdout-closing command; a timeout that kills
// only /bin/sh and leaves the pipeline running; trailing text accepted as a number; NaN/inf sent;
// integer fields wrapping; the cache either missing or never expiring; Scale not applied.
#include "TestUtil.h"
#include "configuration.h"
#include <unity.h>

#if ARCH_PORTDUINO && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "PortduinoGlue.h"
#include "UptimeClock.h"
#include "modules/Telemetry/Sensor/CommandSensor.h"
#include "modules/Telemetry/Sensor/CommandSensorFields.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

namespace
{

long elapsedMs(std::chrono::steady_clock::time_point start)
{
    return static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

std::string scratchDir;

std::string scratchFile(const char *name)
{
    return scratchDir + "/" + name;
}

int countLines(const std::string &path)
{
    std::ifstream in(path);
    int lines = 0;
    std::string line;
    while (std::getline(in, line))
        lines++;
    return lines;
}

meshtastic_Telemetry emptyEnvironment()
{
    meshtastic_Telemetry t = meshtastic_Telemetry_init_zero;
    t.which_variant = meshtastic_Telemetry_environment_metrics_tag;
    t.variant.environment_metrics = meshtastic_EnvironmentMetrics_init_zero;
    return t;
}

} // namespace

void setUp(void)
{
    portduino_config.command_sensors.clear();
    Time::setTestMillis(1000);
}

void tearDown(void)
{
    portduino_config.command_sensors.clear();
    Time::useRealClock();
}

// --- execCommand() / exec() ---

void test_execCommand_captures_stdout_and_reports_success()
{
    std::string output;
    TEST_ASSERT_TRUE(execCommand("echo 21.5", 2000, output));
    TEST_ASSERT_EQUAL_STRING("21.5\n", output.c_str());
}

void test_execCommand_nonzero_exit_is_failure()
{
    std::string output;
    TEST_ASSERT_FALSE(execCommand("echo 3; exit 3", 2000, output));
    TEST_ASSERT_EQUAL_STRING("3\n", output.c_str());
}

void test_execCommand_stdin_is_not_inherited()
{
    // A command that reads stdin must see EOF at once, not block on meshtasticd's terminal or socket.
    std::string output;
    TEST_ASSERT_TRUE(execCommand("cat; echo done", 2000, output));
    TEST_ASSERT_EQUAL_STRING("done\n", output.c_str());
}

void test_execCommand_hung_command_is_killed_at_timeout()
{
    const auto start = std::chrono::steady_clock::now();
    std::string output;
    TEST_ASSERT_FALSE(execCommand("sleep 10", 300, output));
    TEST_ASSERT_LESS_THAN(2000, elapsedMs(start));
}

void test_execCommand_timeout_kills_the_whole_pipeline()
{
    // If only /bin/sh were killed, `sleep` would survive holding the pipe's write end. The marker file
    // proves the pipeline's last stage never ran.
    const std::string marker = scratchFile("pipeline-survived");
    const std::string cmd = "sleep 2 | cat; touch " + marker;
    std::string output;
    TEST_ASSERT_FALSE(execCommand(cmd.c_str(), 300, output));
    usleep(2500 * 1000);
    TEST_ASSERT_EQUAL(-1, access(marker.c_str(), F_OK));
}

void test_execCommand_closed_stdout_still_bounded_by_timeout()
{
    // EOF on stdout is not exit: the wait for the process must also respect the deadline.
    const auto start = std::chrono::steady_clock::now();
    std::string output;
    TEST_ASSERT_FALSE(execCommand("echo early; exec >&-; sleep 10", 300, output));
    TEST_ASSERT_LESS_THAN(2000, elapsedMs(start));
    TEST_ASSERT_EQUAL_STRING("early\n", output.c_str());
}

void test_execCommand_output_is_capped()
{
    std::string output;
    TEST_ASSERT_TRUE(execCommand("head -c 20000 /dev/zero | tr '\\0' a", 2000, output));
    TEST_ASSERT_EQUAL(4096, output.size());
}

void test_exec_returns_output_even_on_nonzero_exit()
{
    // HostMetrics.UserStringCommand has always sent whatever the command printed.
    TEST_ASSERT_EQUAL_STRING("hi\n", exec("echo hi; exit 1").c_str());
}

// --- CommandSensorFields ---

void test_parseValue_accepts_one_number_with_whitespace()
{
    float v = 0;
    TEST_ASSERT_TRUE(CommandSensorFields::parseValue("21.5\n", v));
    TEST_ASSERT_EQUAL_FLOAT(21.5f, v);
    TEST_ASSERT_TRUE(CommandSensorFields::parseValue("  -3\r\n", v));
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, v);
    TEST_ASSERT_TRUE(CommandSensorFields::parseValue("1.5e3", v));
    TEST_ASSERT_EQUAL_FLOAT(1500.0f, v);
}

void test_parseValue_rejects_anything_else()
{
    float v = 42;
    TEST_ASSERT_FALSE(CommandSensorFields::parseValue("", v));
    TEST_ASSERT_FALSE(CommandSensorFields::parseValue("\n", v));
    TEST_ASSERT_FALSE(CommandSensorFields::parseValue("error: no device", v));
    TEST_ASSERT_FALSE(CommandSensorFields::parseValue("21.5 C", v));
    TEST_ASSERT_FALSE(CommandSensorFields::parseValue("21.5\n22.0\n", v));
    TEST_ASSERT_FALSE(CommandSensorFields::parseValue("nan", v));
    TEST_ASSERT_FALSE(CommandSensorFields::parseValue("inf", v));
    TEST_ASSERT_FALSE(CommandSensorFields::parseValue("1e999", v));
    TEST_ASSERT_EQUAL_FLOAT(42.0f, v); // untouched on failure
}

void test_canonicalType_is_case_insensitive_and_rejects_unknown()
{
    TEST_ASSERT_EQUAL_STRING("temperature", CommandSensorFields::canonicalType("Temperature").c_str());
    TEST_ASSERT_EQUAL_STRING("relative_humidity", CommandSensorFields::canonicalType("RELATIVE_HUMIDITY").c_str());
    TEST_ASSERT_EQUAL_STRING("", CommandSensorFields::canonicalType("humidity").c_str());
    TEST_ASSERT_EQUAL_STRING("", CommandSensorFields::canonicalType("").c_str());
}

void test_apply_sets_value_and_has_flag()
{
    meshtastic_EnvironmentMetrics m = meshtastic_EnvironmentMetrics_init_zero;
    TEST_ASSERT_TRUE(CommandSensorFields::apply(m, "barometric_pressure", 1013.25f));
    TEST_ASSERT_TRUE(m.has_barometric_pressure);
    TEST_ASSERT_EQUAL_FLOAT(1013.25f, m.barometric_pressure);
    TEST_ASSERT_FALSE(m.has_temperature);
}

void test_apply_rounds_and_clamps_integer_fields()
{
    meshtastic_EnvironmentMetrics m = meshtastic_EnvironmentMetrics_init_zero;
    TEST_ASSERT_TRUE(CommandSensorFields::apply(m, "soil_moisture", 42.6f));
    TEST_ASSERT_EQUAL_UINT8(43, m.soil_moisture);
    TEST_ASSERT_TRUE(CommandSensorFields::apply(m, "soil_moisture", 300.0f));
    TEST_ASSERT_EQUAL_UINT8(255, m.soil_moisture);
    TEST_ASSERT_TRUE(CommandSensorFields::apply(m, "soil_moisture", -5.0f));
    TEST_ASSERT_EQUAL_UINT8(0, m.soil_moisture);
    TEST_ASSERT_TRUE(CommandSensorFields::apply(m, "wind_direction", 359.6f));
    TEST_ASSERT_EQUAL_UINT16(360, m.wind_direction);
    TEST_ASSERT_TRUE(m.has_wind_direction);
}

void test_apply_unknown_type_changes_nothing()
{
    meshtastic_EnvironmentMetrics m = meshtastic_EnvironmentMetrics_init_zero;
    TEST_ASSERT_FALSE(CommandSensorFields::apply(m, "humidity", 50.0f));
    meshtastic_EnvironmentMetrics zero = meshtastic_EnvironmentMetrics_init_zero;
    TEST_ASSERT_EQUAL_MEMORY(&zero, &m, sizeof(m));
}

// --- CommandSensor ---

void test_CommandSensor_not_added_without_config()
{
    CommandSensor sensor;
    TEST_ASSERT_FALSE(sensor.initDevice(nullptr, nullptr));
}

void test_CommandSensor_applies_scale_and_skips_bad_output()
{
    portduino_config.command_sensors.push_back({"temperature", "echo 21500", 0.001f});
    portduino_config.command_sensors.push_back({"relative_humidity", "echo 'no sensor'", 1.0f});
    portduino_config.command_sensors.push_back({"lux", "exit 1", 1.0f});
    CommandSensor sensor;
    TEST_ASSERT_TRUE(sensor.initDevice(nullptr, nullptr));

    meshtastic_Telemetry t = emptyEnvironment();
    TEST_ASSERT_TRUE(sensor.getMetrics(&t));
    TEST_ASSERT_TRUE(t.variant.environment_metrics.has_temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 21.5f, t.variant.environment_metrics.temperature);
    TEST_ASSERT_FALSE(t.variant.environment_metrics.has_relative_humidity);
    TEST_ASSERT_FALSE(t.variant.environment_metrics.has_lux);
}

void test_CommandSensor_all_failing_reports_no_metrics()
{
    portduino_config.command_sensors.push_back({"temperature", "echo oops", 1.0f});
    CommandSensor sensor;
    TEST_ASSERT_TRUE(sensor.initDevice(nullptr, nullptr));
    meshtastic_Telemetry t = emptyEnvironment();
    TEST_ASSERT_FALSE(sensor.getMetrics(&t));
    TEST_ASSERT_FALSE(t.variant.environment_metrics.has_temperature);
}

void test_CommandSensor_caches_readings_then_refreshes()
{
    const std::string runs = scratchFile("runs");
    std::remove(runs.c_str());
    const std::string cmd = "echo x >> " + runs + "; echo 7";
    portduino_config.command_sensors.push_back({"weight", cmd, 1.0f});
    CommandSensor sensor;
    TEST_ASSERT_TRUE(sensor.initDevice(nullptr, nullptr));

    meshtastic_Telemetry t = emptyEnvironment();
    TEST_ASSERT_TRUE(sensor.getMetrics(&t));
    Time::setTestMillis(1000 + 29 * 1000);
    t = emptyEnvironment();
    TEST_ASSERT_TRUE(sensor.getMetrics(&t)); // cached value is still reported
    TEST_ASSERT_TRUE(t.variant.environment_metrics.has_weight);
    TEST_ASSERT_EQUAL(1, countLines(runs));

    Time::setTestMillis(1000 + 31 * 1000);
    TEST_ASSERT_TRUE(sensor.getMetrics(&t));
    TEST_ASSERT_EQUAL(2, countLines(runs));
}

void setup()
{
    initializeTestEnvironment();
    char tmpl[] = "/tmp/meshtastic-command-sensor-XXXXXX";
    scratchDir = mkdtemp(tmpl) ? tmpl : "/tmp";

    UNITY_BEGIN();
    RUN_TEST(test_execCommand_captures_stdout_and_reports_success);
    RUN_TEST(test_execCommand_nonzero_exit_is_failure);
    RUN_TEST(test_execCommand_stdin_is_not_inherited);
    RUN_TEST(test_execCommand_hung_command_is_killed_at_timeout);
    RUN_TEST(test_execCommand_timeout_kills_the_whole_pipeline);
    RUN_TEST(test_execCommand_closed_stdout_still_bounded_by_timeout);
    RUN_TEST(test_execCommand_output_is_capped);
    RUN_TEST(test_exec_returns_output_even_on_nonzero_exit);
    RUN_TEST(test_parseValue_accepts_one_number_with_whitespace);
    RUN_TEST(test_parseValue_rejects_anything_else);
    RUN_TEST(test_canonicalType_is_case_insensitive_and_rejects_unknown);
    RUN_TEST(test_apply_sets_value_and_has_flag);
    RUN_TEST(test_apply_rounds_and_clamps_integer_fields);
    RUN_TEST(test_apply_unknown_type_changes_nothing);
    RUN_TEST(test_CommandSensor_not_added_without_config);
    RUN_TEST(test_CommandSensor_applies_scale_and_skips_bad_output);
    RUN_TEST(test_CommandSensor_all_failing_reports_no_metrics);
    RUN_TEST(test_CommandSensor_caches_readings_then_refreshes);
    const int failures = UNITY_END();

    std::remove(scratchFile("runs").c_str());
    std::remove(scratchFile("pipeline-survived").c_str());
    rmdir(scratchDir.c_str());
    exit(failures);
}

#else

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

#endif

void loop() {}
