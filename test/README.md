# Native Unit Tests - Authoring Guide

This directory contains C++ unit tests that run on the host machine via PlatformIO's native environment. Tests use the [Unity](http://www.throwtheswitch.org/unity) framework.

## Running Tests

**Preferred: use `bin/run-tests.sh`** - it runs the `coverage` env (ASan/LSan sanitizers), cross-checks the number of suites that actually ran, and emits an unambiguous RED/AMBER/GREEN verdict:

```bash
./bin/run-tests.sh                          # all suites
./bin/run-tests.sh -f test_traffic_management  # single suite
./bin/run-tests.sh -f test_traffic_management > /tmp/test_out.txt 2>&1; tail -5 /tmp/test_out.txt
```

Exit codes: 0 = GREEN, 1 = RED, 2 = AMBER.

> **Copilot interface note:** When running tests via the Copilot chat interface, edits made through the chat may not be reflected in the on-disk files that the test binary reads. If tests pass in chat but fail locally (or vice versa), verify the files on disk match what you expect before trusting the result. Always confirm with a local terminal run.

**Raw `pio test` (no sanitizers, no verdict logic)** - use when you need to override the env or inspect verbose Unity output:

```bash
# All test suites
pio test -e native

# Single suite
pio test -e native -f test_your_module

# Verbose (shows build errors in detail)
pio test -e native -f test_your_module -vvv
```

**Never pipe through `| tail -N` to shorten output.** PlatformIO prints build errors at the top of output and test results at the bottom; `tail` will show stale cached results from a prior successful build while hiding the compile error that caused the current run to fail.

**Preferred pattern for raw pio - redirect to file, then grep:**

```bash
# Redirect all output to a file; grep for errors and results after it exits
pio test -e native -f test_your_module > /tmp/test_out.txt 2>&1
echo "exit: $?"
grep -E 'error:|PASS|FAIL|succeeded|failed' /tmp/test_out.txt
tail -15 /tmp/test_out.txt
```

Why: piping through `| grep` line-buffers the output and suppresses all progress until the process exits, making it look hung. The redirect approach lets the build stream normally while still giving you filtered results afterwards.

**Viewing verbose test output without truncation (e.g. `TEST_MESSAGE` group headers):**

```bash
/tmp/meshtastic-pio-venv/bin/python -m platformio test -e coverage --filter test_mesh_beacon -vv 2>&1 | grep -v "[[:space:]]SKIPPED$"
```

The `-vv` flag makes Unity emit `INFO:` lines from `TEST_MESSAGE` calls; piping through `grep -v SKIPPED` removes the noise from platform feature gates while keeping all PASS/FAIL/INFO lines visible.

**`externally-managed-environment` error on Ubuntu/Debian:**

If `pio test` fails immediately with `error: externally-managed-environment`, the system `pio` binary is using the OS Python which newer distros lock down. Use PlatformIO's own venv instead:

```bash
~/.platformio/penv/bin/python -m platformio test -e native -f test_your_module > /tmp/test_out.txt 2>&1
grep -E 'error:|PASS|FAIL|succeeded|failed' /tmp/test_out.txt
tail -15 /tmp/test_out.txt
```

### Helper Scripts (Useful Shortcuts)

These wrappers are handy when local host dependencies are missing or when you want repeatable commands.

```bash
# Run native tests in Docker (recommended on macOS / non-Linux hosts)
./bin/test-native-docker.sh

# Pass normal PlatformIO test args through to Dockerized test run
./bin/test-native-docker.sh -f test_your_module

# Force Docker image rebuild (after dependency changes)
./bin/test-native-docker.sh --rebuild

# Run simulator integration check (build native first)
pio run -e native && ./bin/test-simulator.sh

# Build and run meshtasticd natively
./bin/native-run.sh

# Build and run under gdbserver on localhost:2345
./bin/native-gdbserver.sh

# Build native release artifact into ./release/
./bin/build-native.sh native
```

Notes:

- The repository script name is `./bin/test-simulator.sh` (there is no `test-native-simulator.sh`).
- `./bin/test-native-docker.sh` is the closest match to CI behavior for native tests and avoids host package setup.

### System Dependencies (Ubuntu/Debian)

The native build requires several system libraries. Install them all at once:

```bash
sudo apt-get install -y \
  libbluetooth-dev libgpiod-dev libyaml-cpp-dev libjsoncpp-dev openssl libssl-dev \
  libulfius-dev liborcania-dev libusb-1.0-0-dev libi2c-dev libuv1-dev
```

See `.github/actions/setup-native/action.yml` for the canonical list.

## Creating a New Test Suite

### 1. Directory Structure

```text
test/test_your_module/test_main.cpp
```

One file per suite. No per-test `platformio.ini` is needed - tests build under the `[env:native]` environment defined in the root `platformio.ini`.

### 2. File Skeleton

```cpp
#include "MeshTypes.h"      // Include BEFORE TestUtil.h (provides NodeNum, etc.)
#include "TestUtil.h"        // initializeTestEnvironment(), testDelay()
#include <unity.h>

#if YOUR_FEATURE_GUARD       // Same #if guard as the module under test

#include "FSCommon.h"
#include "gps/RTC.h"
#include "mesh/NodeDB.h"
#include "modules/YourModule.h"
#include <cstdio>    // required for printf() - used for blank-line group separators
#include <cstring>
#include <memory>

// --- Test output helpers ---
// printf() writes directly to stdout and appears in -vv output as a plain line (no prefix).
// Use it for blank-line group separators: printf("\n");
// TEST_MESSAGE() emits a "file:line:INFO: <text>" line - visible at -vv and above.
// Use TEST_MSG_FMT for formatted diagnostic lines inside tests.
#define MSG_BUF_LEN 200
#define TEST_MSG_FMT(fmt, ...) do { \
    char _buf[MSG_BUF_LEN]; \
    snprintf(_buf, sizeof(_buf), fmt, __VA_ARGS__); \
    TEST_MESSAGE(_buf); \
} while(0)

// --- Tests ---

void test_example()
{
    TEST_MESSAGE("=== Example test ===");
    TEST_ASSERT_TRUE(true);
}

// --- Unity lifecycle ---

void setUp(void) { /* runs before every test */ }
void tearDown(void) { /* runs after every test */ }

void setup()
{
    initializeTestEnvironment();   // MUST call - sets up RTC, OSThread, console
    UNITY_BEGIN();

    printf("\n=== Example group ===\n");           // header line to help find tests

    RUN_TEST(test_example);
    exit(UNITY_END());             // exit() required - Unity runner expects it
}

void loop() {}

#else // !YOUR_FEATURE_GUARD

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

void loop() {}

#endif
```

### 3. Feature Guard

Wrap the entire test body in the same `#if` guard the module uses (e.g. `#if HAS_VARIABLE_HOPS`, `#if !MESHTASTIC_EXCLUDE_GPS`). When the feature is disabled, the `#else` branch produces an empty passing suite.

## Common Patterns

### MockNodeDB

Most module tests need to inject nodes with controlled hop distances and ages:

```cpp
class MockNodeDB : public NodeDB
{
  public:
    void clearTestNodes()
    {
        testNodes.clear();
        numMeshNodes = 0;
    }

    void addTestNode(NodeNum num, uint8_t hopsAway, bool hasHops,
                     uint32_t ageSecs, bool viaMqtt = false)
    {
        meshtastic_NodeInfoLite node = meshtastic_NodeInfoLite_init_zero;
        node.num = num;
        node.has_hops_away = hasHops;
        node.hops_away = hopsAway;
        nodeInfoLiteSetBit(&node, NODEINFO_BITFIELD_VIA_MQTT_MASK, viaMqtt);
        node.last_heard = getTime() - ageSecs;
        testNodes.push_back(node);
        meshNodes = &testNodes;
        numMeshNodes = testNodes.size();
    }

    std::vector<meshtastic_NodeInfoLite> testNodes;
};

static MockNodeDB *mockNodeDB = nullptr;
```

Set `nodeDB = mockNodeDB;` in `setUp()`.

### Test Shim (Exposing Protected/Private Members)

Subclass the module under test to make protected methods callable and private members writable:

```cpp
class YourModuleTestShim : public YourModule
{
  public:
    // Pull protected methods into public scope via using.
    // IMPORTANT: using requires the method to be protected (or public) in the base -
    // friend alone does NOT satisfy this. See pitfall #6.
    using YourModule::runOnce;
    using YourModule::someProtectedMethod;

    // Wrap private members with setter methods (friend grants direct access here).
    void setPrivateField(int x) { privateField = x; }
};
```

For methods you want to expose via `using`, use the conditional access-specifier pattern in the header - **not** plain `friend`:

```cpp
// In YourModule.h, inside the class body:
#ifdef PIO_UNIT_TESTING
  protected:
#else
  private:
#endif
    bool someMethod();
```

For private _member variables_ that a shim setter needs to touch directly, `friend` is sufficient (no `using` involved):

```cpp
// In YourModule.h, inside the class body:
#ifdef PIO_UNIT_TESTING
    friend class YourModuleTestShim;
#endif
```

### Global Singleton Lifecycle

Most modules use a global pointer (`extern YourModule *yourModule;`). Manage it carefully:

```cpp
void setUp(void) {
    // ... setup ...
}

void tearDown(void) {
    yourModule = nullptr;   // prevent dangling pointer between tests
}

void test_something() {
    auto shim = std::unique_ptr<YourModuleTestShim>(new YourModuleTestShim());
    yourModule = shim.get();
    // ... test ...
    yourModule = nullptr;
}
```

## Pitfalls and How to Avoid Them

### 1. Persisted Filesystem State Leaks Between Tests

Modules that save state to `/prefs/*.bin` will have that state loaded by the next test's constructor via `loadState()`. This causes values from one test (e.g. rolling averages from a megamesh scenario) to bleed into unrelated tests.

**Fix:** Delete state files at the start of `setUp()`:

```cpp
void setUp(void) {
    // ...
#ifdef FSCom
    FSCom.remove("/prefs/your_module.bin");
#endif
}
```

### 2. File-Scope Mutable Globals Persist Across Tests

Variables like `static uint8_t someDenominator = 8;` in the module `.cpp` file retain mutations from previous tests. This is distinct from member variables - it affects all instances.

**Fix:** Add a `static void resetGlobal()` method to the module and call it in `setUp()`.

### 3. Randomness Breaks Determinism

If the module uses `rand()` for jitter or similar, test results become non-reproducible.

**Fix:** Add a static enable/disable flag:

```cpp
// Module header:
static void setJitter(bool enabled) { s_jitterEnabled = enabled; }

// Test setUp:
YourModule::setJitter(false);

// Test tearDown:
YourModule::setJitter(true);
```

### 4. Time-Dependent Logic Produces Zeros

Rolling averages weighted by `elapsedMs / ONE_HOUR_MS` collapse to zero when tests complete in microseconds. Sample windows, EMA alphas, and interval-based accumulators all suffer from this.

**Fix:** Expose the timestamp via friend access and simulate realistic elapsed time:

```cpp
// In test shim:
void setWindowStartMs(uint32_t ms) { windowStartMs = ms; }

// In test:
shim.setWindowStartMs(millis() - 3600000UL);  // pretend 1 hour elapsed
```

### 5. Capacity Limits Cause Cascading Failures

Fixed-size data structures (hash sets, ring buffers) overflow when tests inject more data than fits. This triggers early flushes with near-zero time fractions, compounding the time-dependent-zeros problem.

**Fix:** Simulate multiple realistic time windows rather than one massive burst. Let adaptive mechanisms (if any) self-tune over several rolls.

### 6. Granting test access to private/protected members

PlatformIO defines `PIO_UNIT_TESTING` during `pio test` builds. Several production headers (`TransmitHistory.h`, `CryptoEngine.h`, `MQTT.h`, `RTC.h`) use this to gate test-only visibility changes. PlatformIO also defines `UNIT_TEST` in the same builds for backward compatibility, but that spelling is deprecated - always use `PIO_UNIT_TESTING` in new code. The established pattern for exposing a private method to a test shim **without widening production visibility**:

```cpp
#ifdef PIO_UNIT_TESTING
  protected:
#else
  private:
#endif
    bool myMethod();
```

**Critical C++ rule:** a `using` declaration in a derived class (e.g. `using Base::myMethod`) requires `myMethod` to be `protected` or `public` in the base - `friend` alone does **not** satisfy this. Adding `friend class TestShim` while leaving the method `private` will still fail to compile. Use the conditional access-specifier pattern above, not `friend`.

## setUp/tearDown Checklist

- [ ] Create and clear MockNodeDB (if needed)
- [ ] Zero global configs: `config`, `moduleConfig`, `myNodeInfo`
- [ ] Set `nodeDB = mockNodeDB`
- [ ] Delete persisted state files (`FSCom.remove(...)`)
- [ ] Reset file-scope mutable globals
- [ ] Reset mock clock to a safe base value (e.g. `mockTime = ONE_HOUR_MS`) - prevents unsigned subtraction underflow in time-dependent logic
- [ ] Disable randomness/jitter flags
- [ ] In `tearDown`: null the global singleton pointer, restore flags

## Test Organization

A well-structured test suite follows this pattern:

1. **Topology/scenario builders** - static helper functions that set up specific test conditions
2. **Injection helpers** - simulate realistic traffic, time, or event patterns
3. **Scenario tests** - each builds a scenario, runs the module, asserts on outcomes
4. **Lifecycle tests** - state persistence, startup from blank, restart recovery
5. **Summary test** (optional) - emits a scenario table into the log for quick CI review

## Existing Test Suites

| Suite                        | Module Under Test             |
| ---------------------------- | ----------------------------- |
| `test_admin_radio`           | Admin + LoRa region config    |
| `test_atak`                  | ATAK integration              |
| `test_crypto`                | CryptoEngine                  |
| `test_default`               | Default configuration helpers |
| `test_hop_scaling`           | Hop scaling algorithm         |
| `test_http_content_handler`  | HTTP handling                 |
| `test_mac_from_string`       | MAC address parsing           |
| `test_mesh_module`           | Module framework              |
| `test_meshpacket_serializer` | Packet serialization          |
| `test_mqtt`                  | MQTT integration              |
| `test_packet_history`        | Packet history tracking       |
| `test_position_precision`    | Position precision helpers    |
| `test_radio`                 | Radio interface               |
| `test_serial`                | Serial communication          |
| `test_traffic_management`    | Traffic management            |
| `test_transmit_history`      | Retransmission tracking       |
| `test_type_conversions`      | NodeDB v25 type conversions   |
| `test_utf8`                  | UTF-8 utilities               |
