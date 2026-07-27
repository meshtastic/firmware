// Adversarial fuzzing of `meshtasticd --check`, above all DuplicateKeyFinder, whose stack balance
// rests on yaml-cpp emitting matched start/end events. The contract is crash-freedom and
// termination only (what the report *says* is asserted by bin/test-config-check.sh); inputs come
// from a seeded LCG, so a failure reproduces from the printed seed.

// configuration.h pulls in Arduino.h, whose Common.h declares setup()/loop() inside an extern "C"
// block. Must come BEFORE TestUtil.h, same as the other suites.
#include "configuration.h"

#include "TestUtil.h"
#include <unity.h>

#include "ConfigCheck.h"
#include "support/DeterministicRng.h"

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

static constexpr uint64_t BASE_SEED = 0xC0FFEE01ULL;

// Where the checked-in fixtures live, relative to the repo root that `pio test` runs from.
static const char *kFixtureDir = "test/fixtures/portduino-config";

// Thousands of full reports would bury the CI log, so stdout goes to /dev/null for the suite.
// Restored by duplicated fd: CI has no controlling terminal, so reopening /dev/tty would lose it.
class StdoutSilencer
{
  public:
    StdoutSilencer()
    {
        fflush(stdout);
        savedFd = dup(fileno(stdout));
        const int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            dup2(devNull, fileno(stdout));
            close(devNull);
        }
    }
    ~StdoutSilencer()
    {
        fflush(stdout);
        if (savedFd >= 0) {
            dup2(savedFd, fileno(stdout));
            close(savedFd);
        }
    }

  private:
    int savedFd = -1;
};

static std::string tempPath(int n)
{
    return "/tmp/meshtastic-fuzz-config-" + std::to_string(n) + ".yaml";
}

/// Write `bytes` to a scratch file and hand it to the checker. Returns nothing: the assertion is
/// that we get here at all, without a crash, a hang, or an escaped exception.
static void runOn(const std::string &bytes, int slot)
{
    const std::string path = tempPath(slot);
    {
        std::ofstream out(path, std::ios::binary);
        out.write(bytes.data(), (std::streamsize)bytes.size());
    }
    try {
        runConfigCheck({path});
    } catch (const std::exception &) {
        // An escaped exception is a defect in its own right: portduinoSetup() calls this on the way
        // to exit() with nothing above it to catch.
        TEST_FAIL_MESSAGE("runConfigCheck() let an exception escape");
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

static std::vector<std::string> loadSeedCorpus()
{
    std::vector<std::string> corpus;
    std::error_code ec;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(kFixtureDir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".yaml")
            continue;
        std::ifstream in(entry.path(), std::ios::binary);
        std::ostringstream buf;
        buf << in.rdbuf();
        corpus.push_back(buf.str());
    }
    return corpus;
}

// ---------------------------------------------------------------------------
// C1 - the seed corpus itself
// ---------------------------------------------------------------------------

void test_seed_corpus_survives(void)
{
    StdoutSilencer quiet;
    const auto corpus = loadSeedCorpus();
    // A corpus that failed to load would make every later group vacuous, so prove it is there.
    TEST_ASSERT_TRUE_MESSAGE(corpus.size() >= 20, "fixture corpus not found - is the test running from the repo root?");
    int slot = 0;
    for (const auto &seed : corpus)
        runOn(seed, slot++);
}

// ---------------------------------------------------------------------------
// C2 - byte mutation of the corpus
// ---------------------------------------------------------------------------

void test_mutated_corpus(void)
{
    StdoutSilencer quiet;
    const auto corpus = loadSeedCorpus();
    TEST_ASSERT_TRUE(corpus.size() >= 20);
    rngSeed(BASE_SEED + 1);

    for (int i = 0; i < 3000; i++) {
        std::string s = corpus[rngRange((uint32_t)corpus.size())];
        if (s.empty())
            continue;

        switch (rngRange(5)) {
        case 0: // flip a byte
            s[rngRange((uint32_t)s.size())] = (char)rngByte();
            break;
        case 1: // truncate
            s.resize(rngRange((uint32_t)s.size()));
            break;
        case 2: // insert a run of one byte
            s.insert(rngRange((uint32_t)s.size()), std::string(1 + rngRange(64), (char)rngByte()));
            break;
        case 3: // splice two fixtures together
            s += corpus[rngRange((uint32_t)corpus.size())];
            break;
        default: // delete a span
            if (s.size() > 1) {
                const uint32_t at = rngRange((uint32_t)s.size() - 1);
                s.erase(at, 1 + rngRange((uint32_t)(s.size() - at)));
            }
            break;
        }
        runOn(s, i % 8);
    }
}

// ---------------------------------------------------------------------------
// C3 - structural torture
// ---------------------------------------------------------------------------
// Shapes chosen to stress the event-stream walker rather than the parser.

void test_structural_torture(void)
{
    StdoutSilencer quiet;
    rngSeed(BASE_SEED + 2);
    int slot = 0;

    // Deep nesting, both flow and block style.
    for (int depth : {1, 2, 8, 64, 512, 4096}) {
        runOn("Lora:\n  rfswitch_table: " + std::string(depth, '[') + std::string(depth, ']'), slot++);
        std::string block = "Lora:\n";
        for (int i = 0; i < depth; i++)
            block += std::string(2 + i * 2, ' ') + "k:\n";
        runOn(block, slot++);
    }

    // Duplicate keys at every depth, which is what DuplicateKeyFinder's stack is for.
    for (int repeat : {2, 16, 256}) {
        std::string dup = "Lora:\n";
        for (int i = 0; i < repeat; i++)
            dup += "  CS: " + std::to_string(i) + "\n";
        runOn(dup, slot++);
        std::string nested = "Lora:\n  rfswitch_table:\n";
        for (int i = 0; i < repeat; i++)
            nested += "    MODE_RX: [HIGH]\n";
        runOn(nested, slot++);
    }

    // Anchors and aliases: one node reachable from many paths, including a merge key.
    runOn("Lora: &a\n  Module: sx1262\nDisplay: *a\n", slot++);
    runOn("a: &x [1, 2]\nLora:\n  Enable_Pins: *x\n  rfswitch_table: *x\n", slot++);
    runOn("base: &b {CS: 1}\nLora:\n  <<: *b\n  IRQ: 2\n", slot++);

    // Collections where a scalar is expected and vice versa, across every known section.
    for (const char *section : {"Lora", "General", "Display", "Logging", "Webserver", "Meta", "GPIO"}) {
        runOn(std::string(section) + ": [1, 2, 3]\n", slot++);
        runOn(std::string(section) + ": ~\n", slot++);
        runOn(std::string(section) + ":\n  ? [complex, key]\n  : value\n", slot++);
    }

    // Very long keys and scalars.
    runOn("Lora:\n  " + std::string(64 * 1024, 'k') + ": 1\n", slot++);
    runOn("Lora:\n  Module: " + std::string(256 * 1024, 'v') + "\n", slot++);

    // Multiple documents in one file, which HandleNextDocument() loops over.
    runOn("Lora:\n  CS: 1\n---\nLora:\n  CS: 2\n---\n[]\n", slot++);
}

// ---------------------------------------------------------------------------
// C4 - random bytes (DISABLED)
// ---------------------------------------------------------------------------
// Off for CI cost: half the inputs and half the runtime for the least return, since uniform noise
// is almost always rejected on the first token. Flip to 1 to restore it.
#define FUZZ_CONFIG_RANDOM_BYTES 0

#if FUZZ_CONFIG_RANDOM_BYTES
void test_random_bytes(void)
{
    StdoutSilencer quiet;
    rngSeed(BASE_SEED + 3);

    for (int i = 0; i < 1500; i++) {
        std::string s;
        s.resize(rngRange(2048));
        for (auto &c : s)
            c = (char)rngByte();
        runOn(s, i % 8);
    }

    // Bytes drawn only from YAML's structural alphabet get much deeper into the parser than
    // uniform random noise, which is usually rejected on the first token.
    static const char kYamlChars[] = " \t\n-:[]{}#&*!|>'\"%@`,?abcLora0123";
    for (int i = 0; i < 1500; i++) {
        std::string s;
        s.resize(rngRange(2048));
        for (auto &c : s)
            c = kYamlChars[rngRange(sizeof(kYamlChars) - 1)];
        runOn(s, i % 8);
    }
}
#endif // FUZZ_CONFIG_RANDOM_BYTES

void setUp(void) {}
void tearDown(void) {}

// The portduino framework supplies main() and calls setup()/loop(), so the suite's entry point is
// setup() rather than main() -- defining main() here collides with the framework's.
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    printf("\n=== Group C1: seed corpus ===\n");
    RUN_TEST(test_seed_corpus_survives);

    printf("\n=== Group C2: mutated corpus ===\n");
    RUN_TEST(test_mutated_corpus);

    printf("\n=== Group C3: structural torture ===\n");
    RUN_TEST(test_structural_torture);

#if FUZZ_CONFIG_RANDOM_BYTES
    printf("\n=== Group C4: random bytes ===\n");
    RUN_TEST(test_random_bytes);
#endif

    exit(UNITY_END());
}

void loop() {}
