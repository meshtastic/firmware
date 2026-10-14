#pragma once

#include <unity.h>

// Initialize testing environment.
void initializeTestEnvironment();

// Portable delay for tests (Arduino or host).
void testDelay(unsigned long ms);

// Record which files under the scratch $HOME changed since the previous checkpoint, attributing
// each change to the test that made it.
//
// Enabled only when bin/pio-test-isolate.sh sets MESHTASTIC_TEST_STATE_REPORT; a bare `pio test`
// and every on-device build see a no-op. This emits facts only - whether a given write is allowed
// is decided by the harness against test/state-manifest.tsv, so the policy stays in one reviewable
// place instead of being spread across 40-odd suites.
void testStateCheckpoint(const char *testName, const char *sourceFile);

// Checked before every test, because the environment a suite starts in is not the one it keeps.
// initializeTestEnvironment() clears force_simradio once, and a test that sets it - directly, or by
// restoring a struct it saved before the clear - silently disables PKC for every test after it.
// wouldEncryptWithPKC() would then return false and the encode path would quietly fall back to
// channel crypto, which is a passing test asserting the wrong thing. Named per test so the culprit
// is the test that follows the one that broke it.
void testAssertEnvironmentIntact(const char *testName);

// Every RUN_TEST becomes a checkpoint. An unintended write has no matching assertion *by
// definition* - nobody wrote a TEST_ASSERT for the nodes.proto write that broke test_admin_radio,
// because nobody knew it happened - so attribution has to come from outside the test body.
//
// Unity's own RUN_TEST is #ifndef-guarded, but redefine unconditionally so it cannot matter whether
// a suite includes <unity.h> before or after this header. The variadic form accepts Unity's
// optional line argument; the call site's __LINE__ is what Unity reports either way.
#undef RUN_TEST
#define RUN_TEST(func, ...)                                                                                                      \
    do {                                                                                                                         \
        testAssertEnvironmentIntact(#func);                                                                                      \
        UnityDefaultTestRun(func, #func, __LINE__);                                                                              \
        testStateCheckpoint(#func, __FILE__);                                                                                    \
    } while (0)
