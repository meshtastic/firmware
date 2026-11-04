// Tests for the config.yaml "Sensors:" feature on Linux (meshtasticd):
//
//   - execCommand() / exec() in src/platform/portduino/PortduinoGlue.cpp
//   - CommandSensorFields::{canonicalType, messageFor, apply, parseValue} in
//     src/modules/Telemetry/Sensor/CommandSensorFields.cpp
//   - CommandSensor (worker thread and getMetrics()) in src/modules/Telemetry/Sensor/CommandSensor.cpp
//
// Why these contracts matter:
//
// A sensor script that hangs - a stuck I2C read in Python, a network call with no timeout, a command
// that closes stdout and keeps running - must cost at most its timeout, and the whole pipeline it
// started must be killed so it cannot keep holding the pipe. exec() still runs on the main loop for
// HostMetrics, and keeps its old contract there: it returns whatever the command printed, whether or
// not it exited 0. execCommand() reports the outcome instead of logging, because CommandSensor calls
// it from a worker thread and portduino's log buffer is not thread-safe.
//
// parseValue() is the only thing between arbitrary command output and a field broadcast over the mesh.
// It must take exactly one finite number: "21.5 C", an empty line, an error message or "nan" must be
// rejected rather than truncated to a plausible-looking value or sent as NaN.
//
// apply() writes into nanopb fields of several widths. A value outside a narrow integer field
// (soil_moisture is uint8_t) must clamp, not wrap: 300 wrapping to 44% moisture is a silent lie.
//
// A Type belongs to exactly one telemetry message, EnvironmentMetrics or AirQualityMetrics, and each
// telemetry module owns one CommandSensor that runs only its own entries. An entry claimed by both would
// run its command twice and could land in the wrong message; one claimed by neither would be silently
// dropped after loadConfig() accepted it.
//
// CommandSensor runs commands on a worker thread and getMetrics() only reads the latest results.
// getMetrics() is called on the main loop, which also drives the radio, so it must return at once even
// while a command is hanging. On Windows, where the timeout cannot be enforced, that is the only thing
// that keeps a command that never exits from freezing the node. Each command is re-run every 30 s:
// re-running on every getMetrics() call (screen refreshes, phone requests) would multiply the work, and
// never re-running would broadcast the first reading forever.
//
// Regressions guarded: getMetrics() waiting on a command; an unbounded wait on a hung or
// stdout-closing command; a timeout that kills only /bin/sh and leaves the pipeline running; trailing
// text accepted as a number; NaN/inf sent; integer fields wrapping; a scaled value overflowing to inf
// and being sent; the refresh either missing or never happening; Scale not applied.
#include "TestUtil.h"
#include "configuration.h"
#include <unity.h>

#if ARCH_PORTDUINO && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "PortduinoGlue.h"
#include "UptimeClock.h"
#include "modules/Telemetry/Sensor/CommandSensor.h"
#include "modules/Telemetry/Sensor/CommandSensorFields.h"

#include <chrono>
#include <csignal>
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

// Waits, in real time, for the worker to finish `reads` command runs.
bool waitForReads(CommandSensor &sensor, uint32_t reads, long timeoutMs = 5000)
{
    const auto start = std::chrono::steady_clock::now();
    while (sensor.finishedReads() < reads) {
        if (elapsedMs(start) > timeoutMs)
            return false;
        usleep(10 * 1000);
    }
    return true;
}

meshtastic_Telemetry emptyAirQuality()
{
    meshtastic_Telemetry t = meshtastic_Telemetry_init_zero;
    t.which_variant = meshtastic_Telemetry_air_quality_metrics_tag;
    t.variant.air_quality_metrics = meshtastic_AirQualityMetrics_init_zero;
    return t;
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
    TEST_ASSERT_TRUE(execCommand("echo 21.5", 2000, output) == ExecResult::Ok);
    TEST_ASSERT_EQUAL_STRING("21.5\n", output.c_str());
}

void test_execCommand_nonzero_exit_is_failure()
{
    std::string output;
    TEST_ASSERT_TRUE(execCommand("echo 3; exit 3", 2000, output) == ExecResult::Failed);
    TEST_ASSERT_EQUAL_STRING("3\n", output.c_str());
}

void test_execCommand_stdin_is_not_inherited()
{
    // A command that reads stdin must see EOF at once, not block on meshtasticd's terminal or socket.
    std::string output;
    TEST_ASSERT_TRUE(execCommand("cat; echo done", 2000, output) == ExecResult::Ok);
    TEST_ASSERT_EQUAL_STRING("done\n", output.c_str());
}

void test_execCommand_hung_command_is_killed_at_timeout()
{
    const auto start = std::chrono::steady_clock::now();
    std::string output;
    TEST_ASSERT_TRUE(execCommand("sleep 10", 300, output) == ExecResult::TimedOut);
    TEST_ASSERT_LESS_THAN(2000, elapsedMs(start));
}

void test_execCommand_timeout_kills_the_whole_pipeline()
{
    // The timeout must kill the command's whole process group, not just /bin/sh: a surviving child keeps
    // running (and holding the pipe) after the sensor gave up on it. The shell records its background
    // child's PID, which must be gone once execCommand() returns. A zombie counts as gone: it has exited
    // and only waits to be reaped by whatever adopted it.
    const std::string pidFile = scratchFile("descendant-pid");
    const std::string cmd = "sleep 30 & echo $! > " + pidFile + "; wait";
    std::string output;
    TEST_ASSERT_TRUE(execCommand(cmd.c_str(), 300, output) == ExecResult::TimedOut);

    long descendant = 0;
    std::ifstream(pidFile) >> descendant;
    TEST_ASSERT_GREATER_THAN(0, descendant);
    bool gone = false;
    for (int i = 0; i < 50 && !gone; i++) {
        std::ifstream stat("/proc/" + std::to_string(descendant) + "/stat");
        std::string line;
        if (!std::getline(stat, line)) {
            gone = true;
        } else {
            const size_t close = line.rfind(')');
            gone = close != std::string::npos && close + 2 < line.size() && line[close + 2] == 'Z';
        }
        if (!gone)
            usleep(20 * 1000);
    }
    if (!gone)
        kill(static_cast<pid_t>(descendant), SIGKILL);
    TEST_ASSERT_TRUE(gone);
}

void test_execCommand_closed_stdout_still_bounded_by_timeout()
{
    // EOF on stdout is not exit: the wait for the process must also respect the deadline.
    const auto start = std::chrono::steady_clock::now();
    std::string output;
    TEST_ASSERT_TRUE(execCommand("echo early; exec >&-; sleep 10", 300, output) == ExecResult::TimedOut);
    TEST_ASSERT_LESS_THAN(2000, elapsedMs(start));
    TEST_ASSERT_EQUAL_STRING("early\n", output.c_str());
}

void test_execCommand_output_is_capped()
{
    std::string output;
    TEST_ASSERT_TRUE(execCommand("head -c 20000 /dev/zero | tr '\\0' a", 2000, output) == ExecResult::Ok);
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

void test_messageFor_routes_each_type_to_one_message()
{
    using CommandSensorFields::Message;
    TEST_ASSERT_TRUE(CommandSensorFields::messageFor("temperature") == Message::Environment);
    TEST_ASSERT_TRUE(CommandSensorFields::messageFor("iaq") == Message::Environment);
    TEST_ASSERT_TRUE(CommandSensorFields::messageFor("PM25_Standard") == Message::AirQuality);
    TEST_ASSERT_TRUE(CommandSensorFields::messageFor("co2") == Message::AirQuality);
    TEST_ASSERT_TRUE(CommandSensorFields::messageFor("humidity") == Message::None);
    // A status bitmask, not a measurement: deliberately not a Type.
    TEST_ASSERT_TRUE(CommandSensorFields::messageFor("pm_status_flags") == Message::None);
    TEST_ASSERT_EQUAL_STRING("pm25_standard", CommandSensorFields::canonicalType("PM25_Standard").c_str());
}

void test_apply_air_quality_fields()
{
    meshtastic_AirQualityMetrics m = meshtastic_AirQualityMetrics_init_zero;
    TEST_ASSERT_TRUE(CommandSensorFields::apply(m, "co2", 412.6f));
    TEST_ASSERT_TRUE(m.has_co2);
    TEST_ASSERT_EQUAL_UINT32(413, m.co2);
    TEST_ASSERT_TRUE(CommandSensorFields::apply(m, "pm25_standard", -3.0f));
    TEST_ASSERT_EQUAL_UINT32(0, m.pm25_standard);
    TEST_ASSERT_TRUE(CommandSensorFields::apply(m, "pm_voc_idx", 101.5f));
    TEST_ASSERT_EQUAL_FLOAT(101.5f, m.pm_voc_idx);
    // An environment type is not an air-quality field.
    TEST_ASSERT_FALSE(CommandSensorFields::apply(m, "temperature", 20.0f));
}

// --- CommandSensor ---

void test_CommandSensor_not_added_without_config()
{
    CommandSensor sensor(CommandSensorFields::Message::Environment);
    TEST_ASSERT_FALSE(sensor.initDevice(nullptr, nullptr));
}

void test_CommandSensor_applies_scale_and_skips_bad_output()
{
    portduino_config.command_sensors.push_back({"temperature", "echo 21500", 0.001f});
    portduino_config.command_sensors.push_back({"relative_humidity", "echo 'no sensor'", 1.0f});
    portduino_config.command_sensors.push_back({"lux", "exit 1", 1.0f});
    CommandSensor sensor(CommandSensorFields::Message::Environment);
    TEST_ASSERT_TRUE(sensor.initDevice(nullptr, nullptr));
    TEST_ASSERT_TRUE(waitForReads(sensor, 3));

    meshtastic_Telemetry t = emptyEnvironment();
    TEST_ASSERT_TRUE(sensor.getMetrics(&t));
    TEST_ASSERT_TRUE(t.variant.environment_metrics.has_temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 21.5f, t.variant.environment_metrics.temperature);
    TEST_ASSERT_FALSE(t.variant.environment_metrics.has_relative_humidity);
    TEST_ASSERT_FALSE(t.variant.environment_metrics.has_lux);
}

void test_CommandSensor_rejects_scale_overflow()
{
    // Both factors are finite but the product is not; it must not be sent, nor reach an integer cast.
    portduino_config.command_sensors.push_back({"temperature", "echo 3e38", 10.0f});
    portduino_config.command_sensors.push_back({"soil_moisture", "echo 3e38", 10.0f});
    CommandSensor sensor(CommandSensorFields::Message::Environment);
    TEST_ASSERT_TRUE(sensor.initDevice(nullptr, nullptr));
    TEST_ASSERT_TRUE(waitForReads(sensor, 2));
    meshtastic_Telemetry t = emptyEnvironment();
    TEST_ASSERT_FALSE(sensor.getMetrics(&t));
    TEST_ASSERT_FALSE(t.variant.environment_metrics.has_temperature);
    TEST_ASSERT_FALSE(t.variant.environment_metrics.has_soil_moisture);
}

void test_CommandSensor_all_failing_reports_no_metrics()
{
    portduino_config.command_sensors.push_back({"temperature", "echo oops", 1.0f});
    CommandSensor sensor(CommandSensorFields::Message::Environment);
    TEST_ASSERT_TRUE(sensor.initDevice(nullptr, nullptr));
    TEST_ASSERT_TRUE(waitForReads(sensor, 1));
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
    CommandSensor sensor(CommandSensorFields::Message::Environment);
    TEST_ASSERT_TRUE(sensor.initDevice(nullptr, nullptr));
    TEST_ASSERT_TRUE(waitForReads(sensor, 1));

    // Inside the refresh interval: several worker polls go by (it wakes every 100ms) without a re-run,
    // and the last value is still reported.
    Time::setTestMillis(1000 + 29 * 1000);
    usleep(500 * 1000);
    meshtastic_Telemetry t = emptyEnvironment();
    TEST_ASSERT_TRUE(sensor.getMetrics(&t));
    TEST_ASSERT_TRUE(t.variant.environment_metrics.has_weight);
    TEST_ASSERT_EQUAL(1, sensor.finishedReads());
    TEST_ASSERT_EQUAL(1, countLines(runs));

    Time::setTestMillis(1000 + 31 * 1000);
    TEST_ASSERT_TRUE(waitForReads(sensor, 2));
    TEST_ASSERT_EQUAL(2, countLines(runs));
}

void test_CommandSensor_getMetrics_does_not_wait_for_a_hung_command()
{
    // The worker is stuck in this command for its full 2 s timeout; getMetrics() must not be.
    portduino_config.command_sensors.push_back({"lux", "sleep 10", 1.0f});
    CommandSensor sensor(CommandSensorFields::Message::Environment);
    TEST_ASSERT_TRUE(sensor.initDevice(nullptr, nullptr));
    usleep(200 * 1000);

    const auto start = std::chrono::steady_clock::now();
    meshtastic_Telemetry t = emptyEnvironment();
    TEST_ASSERT_FALSE(sensor.getMetrics(&t));
    TEST_ASSERT_LESS_THAN(100, elapsedMs(start));
    TEST_ASSERT_EQUAL(0, sensor.finishedReads());

    // ...and the hung command is still bounded: the worker gets it back at the timeout.
    TEST_ASSERT_TRUE(waitForReads(sensor, 1));
    TEST_ASSERT_FALSE(sensor.getMetrics(&t));
}

void test_CommandSensor_each_module_runs_only_its_own_types()
{
    portduino_config.command_sensors.push_back({"temperature", "echo 21", 1.0f});
    portduino_config.command_sensors.push_back({"co2", "echo 415", 1.0f});
    CommandSensor environment(CommandSensorFields::Message::Environment);
    CommandSensor airQuality(CommandSensorFields::Message::AirQuality);
    TEST_ASSERT_TRUE(environment.initDevice(nullptr, nullptr));
    TEST_ASSERT_TRUE(airQuality.initDevice(nullptr, nullptr));
    TEST_ASSERT_TRUE(waitForReads(environment, 1));
    TEST_ASSERT_TRUE(waitForReads(airQuality, 1));
    usleep(300 * 1000);
    // One command each: neither instance runs the other's entry.
    TEST_ASSERT_EQUAL(1, environment.finishedReads());
    TEST_ASSERT_EQUAL(1, airQuality.finishedReads());

    meshtastic_Telemetry env = emptyEnvironment();
    TEST_ASSERT_TRUE(environment.getMetrics(&env));
    TEST_ASSERT_TRUE(env.variant.environment_metrics.has_temperature);

    meshtastic_Telemetry aq = emptyAirQuality();
    TEST_ASSERT_TRUE(airQuality.getMetrics(&aq));
    TEST_ASSERT_TRUE(aq.variant.air_quality_metrics.has_co2);
    TEST_ASSERT_EQUAL_UINT32(415, aq.variant.air_quality_metrics.co2);

    // Handed the other module's message, an instance writes nothing into it.
    meshtastic_Telemetry wrong = emptyAirQuality();
    TEST_ASSERT_FALSE(environment.getMetrics(&wrong));
    meshtastic_AirQualityMetrics zero = meshtastic_AirQualityMetrics_init_zero;
    TEST_ASSERT_EQUAL_MEMORY(&zero, &wrong.variant.air_quality_metrics, sizeof(zero));
}

void test_CommandSensor_not_added_without_its_module_types()
{
    portduino_config.command_sensors.push_back({"temperature", "echo 21", 1.0f});
    CommandSensor airQuality(CommandSensorFields::Message::AirQuality);
    TEST_ASSERT_FALSE(airQuality.initDevice(nullptr, nullptr));
}

void test_CommandSensor_destructor_stops_the_worker()
{
    // Destroying the sensor must join its worker rather than leave a thread running a destroyed object.
    const std::string runs = scratchFile("runs-after-destroy");
    std::remove(runs.c_str());
    const std::string cmd = "echo x >> " + runs + "; echo 1";
    portduino_config.command_sensors.push_back({"weight", cmd, 1.0f});
    {
        CommandSensor sensor(CommandSensorFields::Message::Environment);
        TEST_ASSERT_TRUE(sensor.initDevice(nullptr, nullptr));
        TEST_ASSERT_TRUE(waitForReads(sensor, 1));
    }
    Time::setTestMillis(1000 + 60 * 1000);
    usleep(300 * 1000);
    TEST_ASSERT_EQUAL(1, countLines(runs));
    std::remove(runs.c_str());
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
    RUN_TEST(test_messageFor_routes_each_type_to_one_message);
    RUN_TEST(test_apply_air_quality_fields);
    RUN_TEST(test_CommandSensor_not_added_without_config);
    RUN_TEST(test_CommandSensor_applies_scale_and_skips_bad_output);
    RUN_TEST(test_CommandSensor_rejects_scale_overflow);
    RUN_TEST(test_CommandSensor_all_failing_reports_no_metrics);
    RUN_TEST(test_CommandSensor_caches_readings_then_refreshes);
    RUN_TEST(test_CommandSensor_getMetrics_does_not_wait_for_a_hung_command);
    RUN_TEST(test_CommandSensor_each_module_runs_only_its_own_types);
    RUN_TEST(test_CommandSensor_not_added_without_its_module_types);
    RUN_TEST(test_CommandSensor_destructor_stops_the_worker);
    const int failures = UNITY_END();

    std::remove(scratchFile("runs").c_str());
    std::remove(scratchFile("descendant-pid").c_str());
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
