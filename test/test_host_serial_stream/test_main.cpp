// Unit tests for src/platform/portduino/HostSerialStream - the host tty / PTY behind the serial
// module on meshtasticd.
//
// These drive real kernel PTYs, so they cover what a mock cannot: the link meshtasticd publishes,
// bytes crossing it both ways, an empty port reading as -1, and that opening a port from a session
// leader never makes it the daemon's controlling terminal (which would SIGHUP it on hangup).
#include "Arduino.h"
#include "TestUtil.h"
#include "mesh/StreamFrameWriter.h"
#include "platform/portduino/HostSerialStream.h"
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <unity.h>

static std::string scratchDir;

static std::string scratchPath(const char *name)
{
    return scratchDir + "/" + name;
}

void setUp(void)
{
    char tmpl[] = "/tmp/hostserialXXXXXX";
    const char *dir = mkdtemp(tmpl);
    TEST_ASSERT_NOT_NULL(dir);
    scratchDir = dir;
}

void tearDown(void)
{
    std::string cmd = "rm -rf '" + scratchDir + "'";
    (void)system(cmd.c_str());
}

// Waits up to a second for the stream to report at least `want` buffered bytes.
static bool waitAvailable(HostSerialStream &stream, int want)
{
    for (int i = 0; i < 100; i++) {
        if (stream.available() >= want)
            return true;
        usleep(10000);
    }
    return false;
}

static std::string readClient(int fd, size_t want)
{
    std::string out;
    char buf[64];
    for (int i = 0; i < 100 && out.size() < want; i++) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0)
            out.append(buf, n);
        else
            usleep(10000);
    }
    return out;
}

void test_pty_mode_links_the_slave_at_the_requested_path()
{
    HostSerialStream stream;
    std::string link = scratchPath("mesh");
    TEST_ASSERT_TRUE(stream.begin("pty:" + link, 38400));

    char target[PATH_MAX] = {0};
    TEST_ASSERT_TRUE(readlink(link.c_str(), target, sizeof(target) - 1) > 0);
    TEST_ASSERT_EQUAL_STRING(stream.clientPath().c_str(), target);
}

void test_pty_mode_carries_bytes_both_ways()
{
    HostSerialStream stream;
    std::string link = scratchPath("mesh");
    TEST_ASSERT_TRUE(stream.begin("pty:" + link, 38400));
    int client = open(link.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    TEST_ASSERT_TRUE(client >= 0);

    TEST_ASSERT_EQUAL(5, ::write(client, "a\nb\0c", 5));
    TEST_ASSERT_TRUE(waitAvailable(stream, 5));
    const char expected[] = {'a', '\n', 'b', '\0', 'c'};
    for (char e : expected)
        TEST_ASSERT_EQUAL((uint8_t)e, stream.read());

    TEST_ASSERT_EQUAL(4, stream.write((const uint8_t *)"\r\nxy", 4));
    TEST_ASSERT_EQUAL_STRING("\r\nxy", readClient(client, 4).c_str());
    close(client);
}

void test_empty_port_reads_minus_one_not_a_nul_byte()
{
    HostSerialStream stream;
    TEST_ASSERT_TRUE(stream.begin("pty:" + scratchPath("mesh"), 38400));
    TEST_ASSERT_EQUAL(0, stream.available());
    TEST_ASSERT_EQUAL(-1, stream.read());
    TEST_ASSERT_EQUAL(-1, stream.peek());
}

void test_pty_mode_survives_the_client_closing()
{
    HostSerialStream stream;
    std::string link = scratchPath("mesh");
    TEST_ASSERT_TRUE(stream.begin("pty:" + link, 38400));
    int client = open(link.c_str(), O_RDWR | O_NOCTTY);
    TEST_ASSERT_TRUE(client >= 0);
    close(client);

    TEST_ASSERT_EQUAL(0, stream.available());
    TEST_ASSERT_EQUAL(-1, stream.read());

    client = open(link.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    TEST_ASSERT_TRUE(client >= 0);
    TEST_ASSERT_EQUAL(2, ::write(client, "hi", 2));
    TEST_ASSERT_TRUE(waitAvailable(stream, 2));
    TEST_ASSERT_EQUAL('h', stream.read());
    TEST_ASSERT_EQUAL('i', stream.read());
    close(client);
}

void test_pty_mode_replaces_a_stale_link()
{
    std::string link = scratchPath("mesh");
    TEST_ASSERT_EQUAL(0, symlink("/nonexistent", link.c_str()));
    HostSerialStream stream;
    TEST_ASSERT_TRUE(stream.begin("pty:" + link, 38400));
    char target[PATH_MAX] = {0};
    TEST_ASSERT_TRUE(readlink(link.c_str(), target, sizeof(target) - 1) > 0);
    TEST_ASSERT_EQUAL_STRING(stream.clientPath().c_str(), target);
}

void test_pty_mode_refuses_to_replace_a_regular_file()
{
    std::string path = scratchPath("mesh");
    FILE *f = fopen(path.c_str(), "w");
    TEST_ASSERT_NOT_NULL(f);
    fclose(f);
    HostSerialStream stream;
    TEST_ASSERT_FALSE(stream.begin("pty:" + path, 38400));
    struct stat st;
    TEST_ASSERT_EQUAL(0, lstat(path.c_str(), &st));
    TEST_ASSERT_TRUE(S_ISREG(st.st_mode));
}

void test_end_removes_the_link()
{
    std::string link = scratchPath("mesh");
    HostSerialStream stream;
    TEST_ASSERT_TRUE(stream.begin("pty:" + link, 38400));
    stream.end();
    struct stat st;
    TEST_ASSERT_NOT_EQUAL(0, lstat(link.c_str(), &st));
    TEST_ASSERT_FALSE(stream.isOpen());
}

void test_tty_mode_reads_a_device_path()
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    TEST_ASSERT_TRUE(master >= 0);
    TEST_ASSERT_EQUAL(0, grantpt(master));
    TEST_ASSERT_EQUAL(0, unlockpt(master));
    std::string slave = ptsname(master);

    HostSerialStream stream;
    TEST_ASSERT_TRUE(stream.begin(slave, 115200));
    TEST_ASSERT_EQUAL(3, ::write(master, "dmx", 3));
    TEST_ASSERT_TRUE(waitAvailable(stream, 3));
    TEST_ASSERT_EQUAL('d', stream.read());
    TEST_ASSERT_EQUAL('m', stream.read());
    TEST_ASSERT_EQUAL('x', stream.read());
    stream.end();
    close(master);
}

// A tty another program left with RTS/CTS on would never transmit over a three-wire cable.
void test_tty_mode_turns_off_hardware_flow_control()
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    TEST_ASSERT_TRUE(master >= 0);
    TEST_ASSERT_EQUAL(0, grantpt(master));
    TEST_ASSERT_EQUAL(0, unlockpt(master));
    std::string slave = ptsname(master);

    int other = open(slave.c_str(), O_RDWR | O_NOCTTY);
    TEST_ASSERT_TRUE(other >= 0);
    struct termios tty;
    TEST_ASSERT_EQUAL(0, tcgetattr(other, &tty));
    tty.c_cflag |= CRTSCTS;
    TEST_ASSERT_EQUAL(0, tcsetattr(other, TCSANOW, &tty));
    TEST_ASSERT_EQUAL(0, tcgetattr(other, &tty));
    TEST_ASSERT_TRUE_MESSAGE(tty.c_cflag & CRTSCTS, "this tty does not keep CRTSCTS, so the test proves nothing");

    HostSerialStream stream;
    TEST_ASSERT_TRUE(stream.begin(slave, 38400));
    TEST_ASSERT_EQUAL(0, tcgetattr(other, &tty));
    TEST_ASSERT_FALSE(tty.c_cflag & CRTSCTS);

    stream.end();
    close(other);
    close(master);
}

void test_missing_tty_fails_to_open()
{
    HostSerialStream stream;
    TEST_ASSERT_FALSE(stream.begin(scratchPath("no-such-tty"), 38400));
    TEST_ASSERT_FALSE(stream.isOpen());
}

// Writes until the kernel buffer between stream and client is full; returns the bytes it took.
// The pty moves data between its buffers in the background and frees room after the first short
// write, so it only counts as full once writes take nothing three times in a row.
static size_t fillUntilShortWrite(HostSerialStream &stream)
{
    uint8_t chunk[1024];
    memset(chunk, 'f', sizeof(chunk));
    size_t total = 0;
    int emptyWrites = 0;
    for (int i = 0; i < 10000 && emptyWrites < 3; i++) {
        size_t n = stream.write(chunk, sizeof(chunk));
        total += n;
        if (n == 0) {
            emptyWrites++;
            usleep(10000);
        } else {
            emptyWrites = 0;
        }
    }
    return total;
}

static void drainClient(int client, std::string &out, size_t want)
{
    char buf[4096];
    for (int i = 0; i < 500 && out.size() < want; i++) {
        ssize_t n = ::read(client, buf, sizeof(buf));
        if (n > 0)
            out.append(buf, n);
        else
            usleep(2000);
    }
}

void test_full_port_reports_no_write_space()
{
    HostSerialStream stream;
    std::string link = scratchPath("mesh");
    TEST_ASSERT_TRUE(stream.begin("pty:" + link, 38400));
    TEST_ASSERT_TRUE(stream.availableForWrite() > 0);
    fillUntilShortWrite(stream);
    TEST_ASSERT_EQUAL(0, stream.availableForWrite());
}

// What PROTO mode depends on: a frame the far end is not draining is kept, not truncated, and
// arrives whole and in order once the far end reads again.
void test_frame_writer_keeps_a_frame_until_the_far_end_drains()
{
    HostSerialStream stream;
    std::string link = scratchPath("mesh");
    TEST_ASSERT_TRUE(stream.begin("pty:" + link, 38400));
    int client = open(link.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    TEST_ASSERT_TRUE(client >= 0);

    size_t filled = fillUntilShortWrite(stream);
    static uint8_t frame[3000];
    for (size_t i = 0; i < sizeof(frame); i++)
        frame[i] = (uint8_t)(i * 7 + 3);

    StreamFrameWriter writer;
    TEST_ASSERT_FALSE(writer.writeFrame(stream, frame, sizeof(frame), false));
    TEST_ASSERT_FALSE(writer.isIdle());

    std::string got;
    bool done = false;
    for (int i = 0; i < 2000 && !done; i++) {
        drainClient(client, got, got.size() + 1);
        done = writer.finishPendingFrame(stream);
    }
    TEST_ASSERT_TRUE(done);
    TEST_ASSERT_TRUE(writer.isIdle());
    drainClient(client, got, filled + sizeof(frame));

    TEST_ASSERT_EQUAL(filled + sizeof(frame), got.size());
    TEST_ASSERT_EQUAL_MEMORY(frame, got.data() + filled, sizeof(frame));
    close(client);
}

// A session leader with no controlling terminal acquires the first tty it opens without O_NOCTTY.
// meshtasticd under systemd is one, so a hangup on that tty would SIGHUP the daemon.
static int childOpensWithoutAcquiringCtty(bool ptyMode, const std::string &arg)
{
    if (setsid() < 0)
        return 10;
    HostSerialStream stream;
    if (!stream.begin(ptyMode ? "pty:" + arg : arg, 38400))
        return 11;
    if (!ptyMode)
        return open("/dev/tty", O_RDWR) < 0 ? 0 : 12;
    int client = open(arg.c_str(), O_RDWR | O_NOCTTY);
    if (client < 0)
        return 13;
    return open("/dev/tty", O_RDWR) < 0 ? 0 : 12;
}

static void assertChildPasses(bool ptyMode, const std::string &arg)
{
    pid_t pid = fork();
    TEST_ASSERT_TRUE(pid >= 0);
    if (pid == 0)
        _exit(childOpensWithoutAcquiringCtty(ptyMode, arg));
    int status = 0;
    TEST_ASSERT_EQUAL(pid, waitpid(pid, &status, 0));
    TEST_ASSERT_TRUE_MESSAGE(WIFEXITED(status), "killed by a signal: SIGHUP from a port that became the controlling terminal");
    TEST_ASSERT_EQUAL_MESSAGE(0, WEXITSTATUS(status), "12 = the port became the controlling terminal");
}

void test_tty_mode_never_becomes_the_controlling_terminal()
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    TEST_ASSERT_TRUE(master >= 0);
    TEST_ASSERT_EQUAL(0, grantpt(master));
    TEST_ASSERT_EQUAL(0, unlockpt(master));
    assertChildPasses(false, ptsname(master));
    close(master);
}

void test_pty_mode_never_becomes_the_controlling_terminal()
{
    assertChildPasses(true, scratchPath("mesh"));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_pty_mode_links_the_slave_at_the_requested_path);
    RUN_TEST(test_pty_mode_carries_bytes_both_ways);
    RUN_TEST(test_empty_port_reads_minus_one_not_a_nul_byte);
    RUN_TEST(test_pty_mode_survives_the_client_closing);
    RUN_TEST(test_pty_mode_replaces_a_stale_link);
    RUN_TEST(test_pty_mode_refuses_to_replace_a_regular_file);
    RUN_TEST(test_end_removes_the_link);
    RUN_TEST(test_tty_mode_reads_a_device_path);
    RUN_TEST(test_tty_mode_turns_off_hardware_flow_control);
    RUN_TEST(test_missing_tty_fails_to_open);
    RUN_TEST(test_full_port_reports_no_write_space);
    RUN_TEST(test_frame_writer_keeps_a_frame_until_the_far_end_drains);
    RUN_TEST(test_tty_mode_never_becomes_the_controlling_terminal);
    RUN_TEST(test_pty_mode_never_becomes_the_controlling_terminal);
    exit(UNITY_END());
}

void loop() {}
