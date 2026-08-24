# Native Unit Tests - Authoring Guide

This directory contains C++ unit tests that run on the host machine via PlatformIO's native environment. Tests use the [Unity](http://www.throwtheswitch.org/unity) framework.

## Running Tests

**Preferred: use `bin/run-tests.sh`** - it defaults to the `coverage` env, cross-checks the number of suites that actually ran, and emits an unambiguous RED/AMBER/GREEN verdict:

```bash
./bin/run-tests.sh                          # all suites
./bin/run-tests.sh -f test_traffic_management  # single suite
./bin/run-tests.sh -f test_traffic_management > /tmp/test_out.txt 2>&1; tail -5 /tmp/test_out.txt
```

Exit codes: 0 = GREEN, 1 = RED, 2 = AMBER, 3 = FILTERED.

**The harness is Linux-only, by choice.** `bin/run-tests.sh` and the per-suite isolation it drives need bash 4+ and GNU coreutils/find (`find -printf`, `md5sum`), and the script refuses to start anywhere else rather than degrade quietly - a shared-state check that silently mis-hashes a sandbox still prints a verdict, and that verdict would be worthless. The `native-macos` PlatformIO env is a **build** target for `meshtasticd`, not a test host; the isolation wrapper is registered for `env:native` and `env:coverage` only. On macOS or Windows, run the suite in a container: `./bin/test-native-docker.sh`.

**`-f` is not a gate.** A filtered run can pass while a full run fails, because filtering removes the suites that _create_ the state a later suite trips over. Iterate with `-f`; gate on a full run.

**Sanitizers are per env.** `coverage` (the default) has ASan/LSan; **`native` has none**, verified. `-e native` runs are not sanitized.

**A signal name in the output is not a crash.** `exit(UNITY_END())` returns the failure count and PlatformIO renders it as a signal number (4 -> `SIGILL`, 5 -> `SIGTRAP`), reporting the suite `[ERRORED]`. Match it against the failure count before assuming a fault.

**Suite order is randomisable, and reproducible.** `--shuffle` runs the suites in a seeded random order; `--seed <n>` replays an exact one. The seed defaults to the commit SHA - one order per commit, so a red is replayable and attributable rather than flaky - and is printed at the start of the run and on the `RESULT:` line. On failure the full order is printed, because for an order-dependent failure the order _is_ the diagnostic. **A single green seed is not evidence of order independence**; vary it.

```bash
./bin/run-tests.sh --shuffle              # seed from HEAD, printed
./bin/run-tests.sh --seed 2855893161      # replay that exact order
```

Randomisation costs one `pio` invocation per suite (about 4.7s each), because PlatformIO orders suites by its own directory walk and `-f` only selects.

> **Copilot interface note:** When running tests via the Copilot chat interface, edits made through the chat may not be reflected in the on-disk files that the test binary reads. If tests pass in chat but fail locally (or vice versa), verify the files on disk match what you expect before trusting the result. Always confirm with a local terminal run.

**Never add `--without-building` to a test run.** PlatformIO links every native test program to the single `$BUILD_DIR/$PROGNAME` path and attributes Unity output by text alone, so a run that only builds beforehand executes whichever suite was linked last under _every_ suite's name - all reporting PASSED. Build once with `--without-testing` to warm the shared src objects if you like; the run itself must still build. `bin/check-test-attribution.py` grades the JUnit reports for exactly this and is wired into both `bin/run-tests.sh` (RED) and CI.

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
    exit(UNITY_END());             // REQUIRED - a bare UNITY_END() leaves the process running
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

### 3. Terminate with `exit(UNITY_END())`, on every branch

**A bare `UNITY_END()` does not end the suite - it ends the _reporting_.** `setup()` returns, the runtime goes on calling `loop()`, and the process runs forever. PlatformIO does not notice: it reads the Unity summary off stdout, reports the suite `PASSED` and moves to the next one, so the run is green while the binary is still resident. Nothing surfaces it, and the leak is one process per suite per run.

The consequences are worse than an idle process:

- The per-suite sandbox is **deleted underneath a live process**, so its CLEAN/DIRTY verdict says what the suite had written by the time the harness stopped looking, not what it left behind.
- `.gcda` coverage data and LeakSanitizer's report are both flushed by `atexit` handlers, so a suite that never exits contributes **no coverage and gets no leak check** - silently.
- Each survivor pins its own deleted binary on disk (~94 MB), which `du` cannot see.

So: `exit(UNITY_END())` in **every** `setup()` branch, including the `#else` of a feature or architecture guard where the suite does nothing. The empty-suite branch is the easiest one to get wrong, because it looks like there is nothing to clean up.

### 4. Feature Guard

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

### 1. Persisted Filesystem State

**You are handed a clean sandbox. Declare what you write.**

Each suite runs inside its own scratch `$HOME` (`bin/pio-test-isolate.sh`), so state cannot reach the next suite. The files in play are wider than module state, and all but the last live under `~/.portduino/default/prefs/`:

| File                                                             | Written by                                                                                                                                                     |
| ---------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `nodes.proto`                                                    | any `NodeDB` save - including incidental ones from `removeNodeByNum()`, `resetNodes()`, `nodeDBSelfCare()`, and the constructor itself when the file is absent |
| `config.proto`, `module.proto`, `channels.proto`, `device.proto` | config/channel saves, admin handlers                                                                                                                           |
| `warm.dat`                                                       | `WarmNodeStore::saveIfDirty()`, on the node-DB save cadence                                                                                                    |
| `transmit_history.dat`                                           | retransmission tracking                                                                                                                                        |
| `/prefs/<module>.bin`                                            | per-module `saveState()`                                                                                                                                       |

`NodeDB`'s constructor calls `loadFromDisk()`, so **any** suite that constructs one inherits whatever is there.

**What you have to do:**

- Nothing, if your suite is self-contained. That is the default and what almost every suite wants.
- If your suite mutates persisted state on purpose, add a line to **`test/state-manifest.tsv`** with a reason:

  ```text
  test_nodedb_blocked	state=per-suite writes=nodes.proto,warm.dat	saturates the DB to test the protected-node cap
  ```

  An undeclared write is reported as **DIRTY** and grades the run AMBER. A declared write that never happens is reported as **MISSING** - a warning, and a useful one: it catches persistence that silently stopped working.

- Use `state=per-suite` only if a test genuinely needs to observe the previous test's write (persistence round-trips, migration ladders). It relaxes per-test checking to the suite boundary, so make it a deliberate choice rather than an accident of `setUp()`.

Deleting your own state in `setUp()` is still fine and still a good habit for intra-suite isolation - it is just no longer what stands between you and the next suite:

```cpp
void setUp(void) {
    // ...
#ifdef FSCom
    FSCom.remove("/prefs/your_module.bin");
#endif
}
```

### 2. A Shared Fixture Is Not a Fixture

If your suite touches globals the code under test writes - `nodeDB`, `config`, `owner`, `devicestate`, `channelFile` - build and restore them in `setUp`/`tearDown` for **every** test, not just the ones that seem to need it. An opt-in fixture that only some tests arm leaves the rest sharing one never-reset object, and "the other tests set their own state and are unaffected" is a claim that quietly stops being true as tests are added.

`test/test_admin_radio/test_main.cpp` is the worked example:

```cpp
void setUp(void) {
    // ...
    replaceAdminRadioGlobals();   // saves the globals, installs a fresh NodeDB
}
void tearDown(void) {
    restoreAdminRadioGlobals();   // restores them, deletes the NodeDB, re-runs initRegion()
    // ...
}
```

A fresh `NodeDB` per test costs real time (`loadFromDisk()` plus, when the region is set, key generation) - in that suite roughly 7% of a ~7½-minute run. Pay it. If a test genuinely needs to observe the previous test's state, that is what `state=per-suite` in `test/state-manifest.tsv` is for; say so there rather than achieving it by omission.

### 3. File-Scope Mutable Globals Persist Across Tests

Variables like `static uint8_t someDenominator = 8;` in the module `.cpp` file retain mutations from previous tests. This is distinct from member variables - it affects all instances.

**Fix:** Add a `static void resetGlobal()` method to the module and call it in `setUp()`.

### 4. Randomness Breaks Determinism

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

### 5. Time-Dependent Logic Produces Zeros

Rolling averages weighted by `elapsedMs / ONE_HOUR_MS` collapse to zero when tests complete in microseconds. Sample windows, EMA alphas, and interval-based accumulators all suffer from this.

**Fix:** Expose the timestamp via friend access and simulate realistic elapsed time:

```cpp
// In test shim:
void setWindowStartMs(uint32_t ms) { windowStartMs = ms; }

// In test:
shim.setWindowStartMs(millis() - 3600000UL);  // pretend 1 hour elapsed
```

### 6. Capacity Limits Cause Cascading Failures

Fixed-size data structures (hash sets, ring buffers) overflow when tests inject more data than fits. This triggers early flushes with near-zero time fractions, compounding the time-dependent-zeros problem.

**Fix:** Simulate multiple realistic time windows rather than one massive burst. Let adaptive mechanisms (if any) self-tune over several rolls.

### 7. Granting test access to private/protected members

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
- [ ] Delete your own persisted state files (`FSCom.remove(...)`) for intra-suite isolation - cross-suite isolation is already guaranteed, see Pitfall 1
- [ ] Declare deliberate writes to shared state in `test/state-manifest.tsv`, with a reason
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

## Not a Unity suite: `bin/test-config-check.sh`

Portduino YAML validation is tested by driving a built `meshtasticd` rather than by a
Unity suite, because what it asserts - the exit status and printed report of
`meshtasticd --check`, and the fact that a normal run still refuses a bad config - are
properties of the process, not of a linkable function. Fixtures live in
`test/fixtures/portduino-config/` (see the README there); CI runs it in
`test_native.yml`. It is not a `test_*` directory, so it sits outside the suite count the
harness derives from `test/`.

```bash
pio run -e native && ./bin/test-config-check.sh
```

## Existing Test Suites

**This table is a description, not an inventory.** The canonical suite total is the number of
`test_*` directories under `test/`, detected on the fly by `bin/run-tests.sh` on every full run
and cross-checked against the suites that actually ran. That derived count is the only number
that should be trusted or quoted. Entries below carry per-suite descriptions the count cannot;
do not infer completeness from the row count.

| Suite                        | Module Under Test             |
| ---------------------------- | ----------------------------- |
| `test_admin_radio`           | Admin + LoRa region config    |
| `test_fscommon_getfiles`     | Bounded file-manifest walk    |
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
| `test_module_config`         | AdminModule module config     |
| `test_tak_config`            | TAK (ATAK) team/role values   |
| `test_traffic_management`    | Traffic management            |
| `test_transmit_history`      | Retransmission tracking       |
| `test_type_conversions`      | NodeDB v25 type conversions   |
| `test_utf8`                  | UTF-8 utilities               |
