# Native Unit Tests — Authoring Guide

This directory contains C++ unit tests that run on the host machine via PlatformIO's native environment. Tests use the [Unity](http://www.throwtheswitch.org/unity) framework.

## Running Tests

```bash
# All test suites
pio test -e native

# Single suite
pio test -e native -f test_your_module

# Verbose (shows build errors in detail)
pio test -e native -f test_your_module -vvv
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
  libbluetooth-dev libgpiod-dev libyaml-cpp-dev openssl libssl-dev \
  libulfius-dev liborcania-dev libusb-1.0-0-dev libi2c-dev libuv1-dev
```

See `.github/actions/setup-native/action.yml` for the canonical list.

## Creating a New Test Suite

### 1. Directory Structure

```text
test/test_your_module/test_main.cpp
```

One file per suite. No per-test `platformio.ini` is needed — tests build under the `[env:native]` environment defined in the root `platformio.ini`.

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
#include <cstdio>
#include <cstring>
#include <memory>

// --- Test output helpers ---
// Unity swallows printf/stdout. Only TEST_MESSAGE() output appears in results.
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
    initializeTestEnvironment();   // MUST call — sets up RTC, OSThread, console
    UNITY_BEGIN();
    RUN_TEST(test_example);
    exit(UNITY_END());             // exit() required — Unity runner expects it
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
        node.via_mqtt = viaMqtt;
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
    // Expose protected methods
    using YourModule::runOnce;
    using YourModule::someProtectedMethod;

    // Access private members via friend (see below)
    void setPrivateField(int x) { privateField = x; }
};
```

In the module header, grant friend access under the `UNIT_TEST` define (set automatically by PlatformIO's test framework):

```cpp
// In YourModule.h, inside the class body:
#ifdef UNIT_TEST
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

Variables like `static uint8_t someDenominator = 8;` in the module `.cpp` file retain mutations from previous tests. This is distinct from member variables — it affects all instances.

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

## setUp/tearDown Checklist

- [ ] Create and clear MockNodeDB (if needed)
- [ ] Zero global configs: `config`, `moduleConfig`, `myNodeInfo`
- [ ] Set `nodeDB = mockNodeDB`
- [ ] Delete persisted state files (`FSCom.remove(...)`)
- [ ] Reset file-scope mutable globals
- [ ] Disable randomness/jitter flags
- [ ] In `tearDown`: null the global singleton pointer, restore flags

## Test Organization

A well-structured test suite follows this pattern:

1. **Topology/scenario builders** — static helper functions that set up specific test conditions
2. **Injection helpers** — simulate realistic traffic, time, or event patterns
3. **Scenario tests** — each builds a scenario, runs the module, asserts on outcomes
4. **Lifecycle tests** — state persistence, startup from blank, restart recovery
5. **Summary test** (optional) — emits a scenario table into the log for quick CI review

## Existing Test Suites

| Suite                        | Module Under Test             |
| ---------------------------- | ----------------------------- |
| `test_crypto`                | CryptoEngine                  |
| `test_mqtt`                  | MQTT integration              |
| `test_radio`                 | Radio interface               |
| `test_mesh_module`           | Module framework              |
| `test_meshpacket_serializer` | Packet serialization          |
| `test_transmit_history`      | Retransmission tracking       |
| `test_atak`                  | ATAK integration              |
| `test_default`               | Default configuration helpers |
| `test_http_content_handler`  | HTTP handling                 |
| `test_serial`                | Serial communication          |
| `test_hop_scaling`           | Hop scaling algorithm         |
| `test_traffic_management`    | Traffic management            |
