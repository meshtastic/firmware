#include "HostSerialStream.h"

#if defined(ARCH_PORTDUINO) && !defined(_WIN32)

#include "configuration.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static const char kPtyPrefix[] = "pty:";

static bool baudToSpeed(uint32_t baud, speed_t &speed)
{
    static const struct {
        uint32_t baud;
        speed_t speed;
    } table[] = {
        {110, B110},       {300, B300},     {600, B600},     {1200, B1200},   {2400, B2400},     {4800, B4800},
        {9600, B9600},     {19200, B19200}, {38400, B38400}, {57600, B57600}, {115200, B115200}, {230400, B230400},
#ifdef B460800
        {460800, B460800},
#endif
#ifdef B576000
        {576000, B576000},
#endif
#ifdef B921600
        {921600, B921600},
#endif
    };
    for (const auto &entry : table) {
        if (entry.baud == baud) {
            speed = entry.speed;
            return true;
        }
    }
    return false;
}

bool HostSerialStream::begin(const std::string &path, uint32_t baud)
{
    end();
    if (path.compare(0, sizeof(kPtyPrefix) - 1, kPtyPrefix) == 0)
        return openPty(path.substr(sizeof(kPtyPrefix) - 1));
    return openTty(path, baud);
}

bool HostSerialStream::openTty(const std::string &path, uint32_t baud)
{
    fd = open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        LOG_ERROR("Serial: cannot open %s: %s", path.c_str(), strerror(errno));
        return false;
    }
    struct termios tty;
    if (tcgetattr(fd, &tty) == 0) {
        cfmakeraw(&tty);
        tty.c_cflag |= CLOCAL | CREAD;
#ifdef CRTSCTS
        // cfmakeraw() keeps flow control another program left on; with CTS unwired the port never sends.
        tty.c_cflag &= ~CRTSCTS;
#endif
        speed_t speed;
        if (baudToSpeed(baud, speed)) {
            cfsetispeed(&tty, speed);
            cfsetospeed(&tty, speed);
        } else {
            LOG_WARN("Serial: %u baud is not a host termios rate; leaving %s at its current speed", baud, path.c_str());
        }
        tcsetattr(fd, TCSANOW, &tty);
    }
    clientName = path;
    return true;
}

bool HostSerialStream::openPty(const std::string &link)
{
    if (link.empty()) {
        LOG_ERROR("Serial: pty: needs a link path, e.g. pty:/tmp/meshserial");
        return false;
    }
    fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (fd < 0 || grantpt(fd) != 0 || unlockpt(fd) != 0) {
        LOG_ERROR("Serial: cannot create a pty: %s", strerror(errno));
        end();
        return false;
    }
    const char *slaveName = ptsname(fd);
    if (!slaveName) {
        LOG_ERROR("Serial: no pty slave name: %s", strerror(errno));
        end();
        return false;
    }
    clientName = slaveName;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

    ptySlaveFd = open(slaveName, O_RDWR | O_NOCTTY);
    if (ptySlaveFd < 0) {
        LOG_ERROR("Serial: cannot open pty slave %s: %s", slaveName, strerror(errno));
        end();
        return false;
    }
    struct termios tty;
    if (tcgetattr(ptySlaveFd, &tty) == 0) {
        cfmakeraw(&tty);
        tcsetattr(ptySlaveFd, TCSANOW, &tty);
    }

    // Replace a stale link from an earlier run, but never a real file.
    struct stat st;
    if (lstat(link.c_str(), &st) == 0) {
        if (!S_ISLNK(st.st_mode)) {
            LOG_ERROR("Serial: %s exists and is not a symlink; not replacing it", link.c_str());
            end();
            return false;
        }
        unlink(link.c_str());
    }
    if (symlink(slaveName, link.c_str()) != 0) {
        LOG_ERROR("Serial: cannot link %s -> %s: %s", link.c_str(), slaveName, strerror(errno));
        end();
        return false;
    }
    linkPath = link;
    LOG_INFO("Serial: pty %s linked at %s", slaveName, link.c_str());
    return true;
}

void HostSerialStream::end()
{
    if (!linkPath.empty()) {
        unlink(linkPath.c_str());
        linkPath.clear();
    }
    if (ptySlaveFd >= 0) {
        close(ptySlaveFd);
        ptySlaveFd = -1;
    }
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
    peeked = -1;
    clientName.clear();
}

int HostSerialStream::available()
{
    if (fd < 0)
        return 0;
    int bytes = 0;
    if (ioctl(fd, FIONREAD, &bytes) == -1)
        return peeked >= 0 ? 1 : 0;
    return bytes + (peeked >= 0 ? 1 : 0);
}

int HostSerialStream::read()
{
    if (peeked >= 0) {
        int c = peeked;
        peeked = -1;
        return c;
    }
    if (fd < 0)
        return -1;
    uint8_t c;
    return ::read(fd, &c, 1) == 1 ? c : -1;
}

int HostSerialStream::peek()
{
    if (peeked < 0)
        peeked = read();
    return peeked;
}

size_t HostSerialStream::write(uint8_t c)
{
    return write(&c, 1);
}

int HostSerialStream::availableForWrite()
{
    if (fd < 0)
        return 0;
    struct pollfd p = {fd, POLLOUT, 0};
    if (poll(&p, 1, 0) != 1 || !(p.revents & POLLOUT))
        return 0;
    int queued = 0;
    if (ioctl(fd, TIOCOUTQ, &queued) == -1 || queued < 0)
        queued = 0;
    // The kernel does not report its tty buffer size; 4096 is the Linux N_TTY default.
    return queued < 4096 ? 4096 - queued : 0;
}

// Non-blocking: with no reader on the far end the kernel buffer fills and the write comes up short.
size_t HostSerialStream::write(const uint8_t *buffer, size_t size)
{
    if (fd < 0)
        return 0;
    ssize_t n = ::write(fd, buffer, size);
    return n > 0 ? (size_t)n : 0;
}

#endif
