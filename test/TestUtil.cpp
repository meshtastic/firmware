// First, in its own block so the include sorter keeps it there: configuration.h supplies the
// variant defines mesh-pb-constants.h needs (portduino resolves MAX_NUM_NODES at runtime).
#include "configuration.h"

#include "SerialConsole.h"
#include "concurrency/OSThread.h"
#include "gps/RTC.h"

#include "TestUtil.h"

#if defined(ARDUINO)
#include <Arduino.h>
#else
#include <chrono>
#include <thread>
#endif

// The state checkpoint needs a POSIX directory walk, and only the host builds run these suites.
// Note ARDUINO *is* defined on portduino, so it is not the right guard here.
#if ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <map>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if ARCH_PORTDUINO
// A test binary must not be reachable from the network. main.cpp's setup()/loop() are compiled out
// under PIO_UNIT_TESTING, so the phone API, MQTT and the web server are never started - but that is
// a property of today's guards, not something anything checks. A suite that pulled in a service
// which binds a port would otherwise open one on the developer's machine, silently, for the length
// of the run. Assert the absence instead of trusting it.
//
// Listening sockets only: an outbound connection is a different (and louder) problem, and gethostby*
// opens transient sockets that would make an any-socket check flap.
static void assertNoListeningSockets()
{
    // Socket fds appear as "socket:[inode]"; a listening TCP row in /proc/self/net carries st 0A.
    std::set<std::string> ours;
    if (DIR *fds = opendir("/proc/self/fd")) {
        while (struct dirent *e = readdir(fds)) {
            char path[64], target[128];
            snprintf(path, sizeof(path), "/proc/self/fd/%s", e->d_name);
            ssize_t n = readlink(path, target, sizeof(target) - 1);
            if (n <= 0)
                continue;
            target[n] = '\0';
            unsigned long inode = 0;
            if (sscanf(target, "socket:[%lu]", &inode) == 1)
                ours.insert(std::to_string(inode));
        }
        closedir(fds);
    }
    if (ours.empty())
        return;

    std::string offenders;
    for (const char *table : {"/proc/self/net/tcp", "/proc/self/net/tcp6"}) {
        FILE *f = fopen(table, "r");
        if (!f)
            continue;
        char line[512];
        bool header = true;
        while (fgets(line, sizeof(line), f)) {
            if (header) {
                header = false;
                continue;
            }
            // sl local_address rem_address st tx:rx tr:when retrnsmt uid timeout inode
            char local[128] = {0};
            unsigned st = 0, uid = 0;
            unsigned long inode = 0;
            if (sscanf(line, "%*d: %127s %*s %x %*s %*s %*s %u %*d %lu", local, &st, &uid, &inode) != 4)
                continue;
            if (st != 0x0A) // TCP_LISTEN
                continue;
            if (ours.count(std::to_string(inode)) == 0)
                continue;
            offenders += " ";
            offenders += local;
        }
        fclose(f);
    }
    if (offenders.empty())
        return;

    // Before UNITY_BEGIN(), so there is no Unity failure to record - and a test binary that has
    // opened a port is not a result worth collecting. Fail the suite outright and say why.
    fprintf(stderr,
            "FATAL: test binary is listening on%s\n"
            "A unit-test run must not be reachable. Something started a network service - check what\n"
            "the suite constructs, and whether it belongs behind main.cpp's PIO_UNIT_TESTING guard.\n",
            offenders.c_str());
    fflush(stderr);
    exit(EXIT_FAILURE);
}
#endif

#if ARCH_PORTDUINO
static bool environmentBaselined = false;

// -s is how the harness keeps a test run off the host's radio: it makes portduinoSetup() skip the
// /etc/meshtasticd/config.yaml search and return before GPIO/SPI init. That job is done by the time
// any of this runs, and the flag's only remaining readers are behaviour we do want under test -
// wouldEncryptWithPKC() disables PKC while it is set. Clear it so suites exercise the production
// encode path; the radio choice is already made and is not revisited.
static void baselineEnvironment()
{
    portduino_config.force_simradio = false;
    assertNoListeningSockets();
    environmentBaselined = true;
}
#endif

void testAssertEnvironmentIntact(const char *testName)
{
#if ARCH_PORTDUINO
    // Not every suite calls initializeTestEnvironment() - test_atak does not - so the baseline
    // cannot live only there, or those suites run with PKC off and skip the socket check. Establish
    // it at the first RUN_TEST for whoever has not, and hold it from then on.
    if (!environmentBaselined) {
        baselineEnvironment();
        return;
    }

    // Per test, not once per suite: a service that binds a port is opened by the code under test,
    // not by the harness, so checking only at startup would miss every case that starts one.
    assertNoListeningSockets();

    if (!portduino_config.force_simradio)
        return;

    // Hard exit rather than TEST_FAIL: this runs between tests, outside any Unity test frame, so
    // there is no failure to longjmp into. Repairing the flag silently would be worse - it would
    // leave the suite that broke it passing.
    for (FILE *out : {stdout, stderr})
        fprintf(out,
                "FATAL: force_simradio was set back on before %s\n"
                "PKC is disabled while it is set, so the encode path under test falls back to channel\n"
                "crypto and every later case asserts the wrong thing. A test that needs simradio must\n"
                "restore the flag before it returns.\n",
                testName ? testName : "(unknown test)");
    fflush(stderr);
    exit(EXIT_FAILURE);
#else
    (void)testName;
#endif
}

void initializeTestEnvironment()
{
    concurrency::hasBeenSetup = true;
    consoleInit();
#if ARCH_PORTDUINO
    baselineEnvironment();

    struct timeval tv;
    tv.tv_sec = time(NULL);
    tv.tv_usec = 0;
    perhapsSetRTC(RTCQualityNTP, &tv);
#endif
    concurrency::OSThread::setup();

    // Baseline the sandbox before the first RUN_TEST, so writes made during suite setup are not
    // charged to whichever test happens to run first.
    testStateCheckpoint(nullptr, nullptr);
}

void testDelay(unsigned long ms)
{
#if defined(ARDUINO)
    ::delay(ms);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}

#if !ARCH_PORTDUINO

void testStateCheckpoint(const char *, const char *) {}

#else

namespace
{

/// MinGW-w64 has no lstat(): Windows has no POSIX symlink stat, and nothing in a test sandbox
/// creates a symlink, so stat() sees the same thing for every entry walk() can reach.
#ifdef _WIN32
inline int lstatCompat(const char *path, struct stat *st)
{
    return stat(path, st);
}
#else
inline int lstatCompat(const char *path, struct stat *st)
{
    return lstat(path, st);
}
#endif

/// Content fingerprint, used only to answer "did this file change?". FNV-1a rather than a real
/// digest because the answer is a boolean and the files are a few KB of protobuf; nothing here
/// records a hash as an expected value, which is what would make this a snapshot test.
uint64_t fileFingerprint(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return 0;
    uint64_t h = 1469598103934665603ULL;
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            h ^= buf[i];
            h *= 1099511628211ULL;
        }
    }
    fclose(f);
    return h;
}

void walk(const std::string &root, const std::string &rel, std::map<std::string, uint64_t> &out)
{
    const std::string dirPath = rel.empty() ? root : root + "/" + rel;
    DIR *d = opendir(dirPath.c_str());
    if (!d)
        return;
    while (struct dirent *e = readdir(d)) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        const std::string childRel = rel.empty() ? std::string(e->d_name) : rel + "/" + e->d_name;
        const std::string childPath = root + "/" + childRel;
        struct stat st;
        if (lstatCompat(childPath.c_str(), &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            walk(root, childRel, out);
        else if (S_ISREG(st.st_mode))
            out[childRel] = fileFingerprint(childPath);
    }
    closedir(d);
}

/// "test/test_admin_radio/test_main.cpp" -> "test_admin_radio". The suite name is not otherwise
/// available to a test program - PlatformIO passes it to the *build*, not to the run.
std::string suiteFromPath(const char *sourceFile)
{
    if (!sourceFile)
        return "";
    std::string p(sourceFile);
    const size_t lastSlash = p.find_last_of('/');
    if (lastSlash == std::string::npos)
        return "";
    p.erase(lastSlash);
    const size_t prevSlash = p.find_last_of('/');
    return prevSlash == std::string::npos ? p : p.substr(prevSlash + 1);
}

struct StateWatch {
    bool resolved = false;
    bool active = false;
    std::string root;
    std::string report;
    std::map<std::string, uint64_t> previous;
};

StateWatch &watch()
{
    static StateWatch w;
    return w;
}

} // namespace

void testStateCheckpoint(const char *testName, const char *sourceFile)
{
    StateWatch &w = watch();

    if (!w.resolved) {
        w.resolved = true;
        const char *report = getenv("MESHTASTIC_TEST_STATE_REPORT");
        const char *home = getenv("HOME");
        // No report path means nobody asked: a bare `pio test` behaves exactly as before.
        w.active = report && *report && home && *home;
        if (w.active) {
            w.report = report;
            w.root = home;
        }
    }
    if (!w.active)
        return;

    std::map<std::string, uint64_t> current;
    walk(w.root, "", current);

    // The priming call from initializeTestEnvironment() has no test to attribute to.
    if (testName) {
        const std::string suite = suiteFromPath(sourceFile);
        FILE *out = fopen(w.report.c_str(), "a");
        if (out) {
            for (const auto &entry : current) {
                auto prior = w.previous.find(entry.first);
                if (prior == w.previous.end())
                    fprintf(out, "%s\t%s\tadded\t%s\n", suite.c_str(), testName, entry.first.c_str());
                else if (prior->second != entry.second)
                    fprintf(out, "%s\t%s\tmodified\t%s\n", suite.c_str(), testName, entry.first.c_str());
            }
            for (const auto &entry : w.previous) {
                if (current.find(entry.first) == current.end())
                    fprintf(out, "%s\t%s\tremoved\t%s\n", suite.c_str(), testName, entry.first.c_str());
            }
            fclose(out);
        }
    }

    w.previous.swap(current);
}

#endif
