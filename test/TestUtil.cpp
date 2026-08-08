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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <map>
#include <string>
#include <sys/stat.h>
#endif

void initializeTestEnvironment()
{
    concurrency::hasBeenSetup = true;
    consoleInit();
#if ARCH_PORTDUINO
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
        if (lstat(childPath.c_str(), &st) != 0)
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
