#include "TransparentLink.h"

#if HAS_TRANSPARENT_RADIO

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <future>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <system_error>
#include <termios.h>
#include <unistd.h>

namespace
{

constexpr int DEFAULT_API_PORT = 4403;
constexpr int CONNECT_TIMEOUT_MS = 1000; // keep a dead host from stalling meshtasticd's main loop
constexpr int CONNECT_BUDGET_MS = 2000;  // all resolved addresses together, so a v4 after a dead v6 still gets a try
constexpr int WRITE_DEADLINE_MS = 200;   // a frame the kernel can't take in this long means a dead link

using Deadline = std::chrono::steady_clock::time_point;

Deadline deadlineIn(int ms)
{
    return std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
}

int msLeft(Deadline deadline)
{
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
}

bool wouldBlock()
{
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}

bool waitWritable(int fd, int timeoutMs)
{
    struct pollfd pfd = {fd, POLLOUT, 0};
    return timeoutMs > 0 && ::poll(&pfd, 1, timeoutMs) > 0 && (pfd.revents & POLLOUT);
}

class FdLink : public TransparentLink
{
  public:
    explicit FdLink(std::string description) : description(std::move(description)) {}
    ~FdLink() override { close(); }

    void close() override
    {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }
    bool isOpen() const override { return fd >= 0; }
    const std::string &describe() const override { return description; }

    bool write(const uint8_t *buf, size_t len) override
    {
        // One deadline for the whole frame: runOnce() is blocked until this returns.
        const Deadline deadline = deadlineIn(WRITE_DEADLINE_MS);
        while (fd >= 0 && len > 0) {
            ssize_t n = sendBytes(buf, len);
            if (n > 0) {
                buf += n;
                len -= (size_t)n;
                continue;
            }
            if (n < 0 && wouldBlock() && waitWritable(fd, msLeft(deadline)))
                continue;
            return false;
        }
        return fd >= 0;
    }

  protected:
    virtual ssize_t sendBytes(const uint8_t *buf, size_t len) { return ::write(fd, buf, len); }

    int fd = -1;
    std::string description;
};

class SerialLink : public FdLink
{
  public:
    explicit SerialLink(const std::string &path) : FdLink(path) {}

    bool open() override
    {
        close();
        int f = ::open(description.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (f < 0)
            return false;
        struct termios tio;
        if (tcgetattr(f, &tio) != 0) {
            ::close(f);
            return false;
        }
        cfmakeraw(&tio);
        cfsetispeed(&tio, B115200);
        cfsetospeed(&tio, B115200);
        tio.c_cflag |= CLOCAL | CREAD;
        tio.c_cflag &= ~HUPCL; // dropping DTR on close resets boards with an auto-reset circuit
        tio.c_cc[VMIN] = 0;
        tio.c_cc[VTIME] = 0;
        if (tcsetattr(f, TCSANOW, &tio) != 0) {
            ::close(f);
            return false;
        }
        tcflush(f, TCIOFLUSH);
        fd = f;
        return true;
    }

    int read(uint8_t *buf, size_t len) override
    {
        if (fd < 0)
            return -1;
        ssize_t n = ::read(fd, buf, len);
        if (n > 0)
            return (int)n;
        if (n == 0) { // a raw tty with VMIN 0 reads 0 when nothing is waiting, and also once the device is unplugged
            struct pollfd pfd = {fd, POLLIN, 0};
            return (::poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))) ? -1 : 0;
        }
        return wouldBlock() ? 0 : -1;
    }
};

class TcpLink : public FdLink
{
  public:
    TcpLink(const std::string &endpoint, std::string host, int port)
        : FdLink(endpoint), host(std::move(host)), port(std::to_string(port))
    {
    }
    ~TcpLink() override
    {
        if (resolving) // the lookup can't be cancelled; wait it out so its result is freed
            if (struct addrinfo *res = pending.get())
                freeaddrinfo(res);
    }

    bool openPending() const override { return resolving; }

    bool open() override
    {
        close();
        // getaddrinfo() has no timeout and runOnce() is on meshtasticd's main loop, so resolve on a worker thread.
        if (!resolving) {
            try {
                pending = std::async(std::launch::async, resolve, host, port);
            } catch (const std::system_error &) { // no thread to spare: a failed open, so runOnce() backs off
                return false;
            }
            resolving = true;
        }
        if (pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return false;
        resolving = false;
        struct addrinfo *res = pending.get();
        if (!res)
            return false;
        const Deadline budget = deadlineIn(CONNECT_BUDGET_MS);
        for (struct addrinfo *rp = res; rp != nullptr && fd < 0 && msLeft(budget) > 0; rp = rp->ai_next) {
            int f = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (f < 0)
                continue;
            ::fcntl(f, F_SETFL, O_NONBLOCK);
            if (connectWithTimeout(f, rp->ai_addr, rp->ai_addrlen, std::min(CONNECT_TIMEOUT_MS, msLeft(budget)))) {
                int one = 1;
                ::setsockopt(f, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
                ::setsockopt(f, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
                fd = f;
            } else {
                ::close(f);
            }
        }
        freeaddrinfo(res);
        return fd >= 0;
    }

    int read(uint8_t *buf, size_t len) override
    {
        if (fd < 0)
            return -1;
        ssize_t n = ::recv(fd, buf, len, 0);
        if (n > 0)
            return (int)n;
        if (n < 0 && wouldBlock())
            return 0;
        return -1; // 0 is the peer closing
    }

  protected:
    ssize_t sendBytes(const uint8_t *buf, size_t len) override
    {
#ifdef MSG_NOSIGNAL
        return ::send(fd, buf, len, MSG_NOSIGNAL);
#else
        return ::send(fd, buf, len, 0);
#endif
    }

  private:
    static struct addrinfo *resolve(std::string host, std::string port)
    {
        struct addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0)
            return nullptr;
        return res;
    }

    static bool connectWithTimeout(int f, const struct sockaddr *addr, socklen_t addrlen, int timeoutMs)
    {
        if (::connect(f, addr, addrlen) == 0)
            return true;
        if (errno != EINPROGRESS)
            return false;
        struct pollfd pfd = {f, POLLOUT, 0};
        if (timeoutMs <= 0 || ::poll(&pfd, 1, timeoutMs) <= 0)
            return false;
        int err = 0;
        socklen_t errLen = sizeof(err);
        return ::getsockopt(f, SOL_SOCKET, SO_ERROR, &err, &errLen) == 0 && err == 0;
    }

    std::string host;
    std::string port;
    std::future<struct addrinfo *> pending;
    bool resolving = false;
};

} // namespace

std::unique_ptr<TransparentLink> makeTransparentLink(const std::string &endpoint)
{
    if (endpoint.empty())
        return nullptr;
    if (endpoint[0] == '/')
        return std::unique_ptr<TransparentLink>(new SerialLink(endpoint));

    // "host", "host:port", "[v6addr]" or "[v6addr]:port"; a bare v6 address has no port.
    std::string host = endpoint;
    int port = DEFAULT_API_PORT;
    std::string portText;
    if (endpoint[0] == '[') {
        const size_t bracket = endpoint.find(']');
        if (bracket == std::string::npos)
            return nullptr;
        host = endpoint.substr(1, bracket - 1);
        if (bracket + 1 < endpoint.size()) {
            if (endpoint[bracket + 1] != ':' || bracket + 2 == endpoint.size())
                return nullptr;
            portText = endpoint.substr(bracket + 2);
        }
    } else if (endpoint.find(':') == endpoint.rfind(':') && endpoint.find(':') != std::string::npos) {
        host = endpoint.substr(0, endpoint.find(':'));
        portText = endpoint.substr(endpoint.find(':') + 1);
        if (portText.empty())
            return nullptr;
    }
    if (!portText.empty()) {
        char *end = nullptr;
        const long parsed = strtol(portText.c_str(), &end, 10);
        if (*end != '\0' || parsed < 1 || parsed > 65535)
            return nullptr;
        port = (int)parsed;
    }
    if (host.empty())
        return nullptr;
    return std::unique_ptr<TransparentLink>(new TcpLink(endpoint, host, port));
}

#endif
