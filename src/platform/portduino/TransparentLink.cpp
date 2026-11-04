#include "TransparentLink.h"

#if HAS_TRANSPARENT_RADIO

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

namespace
{

constexpr int DEFAULT_API_PORT = 4403;
constexpr int CONNECT_TIMEOUT_MS = 1000; // keep a dead host from stalling meshtasticd's main loop
constexpr int WRITE_STALL_MS = 200;      // a full kernel buffer that doesn't drain in this long is a dead link

bool wouldBlock()
{
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}

bool waitWritable(int fd)
{
    struct pollfd pfd = {fd, POLLOUT, 0};
    return ::poll(&pfd, 1, WRITE_STALL_MS) > 0 && (pfd.revents & POLLOUT);
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
        while (fd >= 0 && len > 0) {
            ssize_t n = sendBytes(buf, len);
            if (n > 0) {
                buf += n;
                len -= (size_t)n;
            } else if (n < 0 && wouldBlock() && waitWritable(fd)) {
                continue;
            } else {
                return false;
            }
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
        if (n >= 0) // a raw tty with VMIN 0 reads 0 when nothing is waiting, not at EOF
            return (int)n;
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

    bool open() override
    {
        close();
        struct addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res)
            return false;
        for (struct addrinfo *rp = res; rp != nullptr && fd < 0; rp = rp->ai_next) {
            int f = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (f < 0)
                continue;
            ::fcntl(f, F_SETFL, O_NONBLOCK);
            if (connectWithTimeout(f, rp->ai_addr, rp->ai_addrlen)) {
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
    static bool connectWithTimeout(int f, const struct sockaddr *addr, socklen_t addrlen)
    {
        if (::connect(f, addr, addrlen) == 0)
            return true;
        if (errno != EINPROGRESS)
            return false;
        struct pollfd pfd = {f, POLLOUT, 0};
        if (::poll(&pfd, 1, CONNECT_TIMEOUT_MS) <= 0)
            return false;
        int err = 0;
        socklen_t errLen = sizeof(err);
        return ::getsockopt(f, SOL_SOCKET, SO_ERROR, &err, &errLen) == 0 && err == 0;
    }

    std::string host;
    std::string port;
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
            if (endpoint[bracket + 1] != ':')
                return nullptr;
            portText = endpoint.substr(bracket + 2);
        }
    } else if (endpoint.find(':') == endpoint.rfind(':') && endpoint.find(':') != std::string::npos) {
        host = endpoint.substr(0, endpoint.find(':'));
        portText = endpoint.substr(endpoint.find(':') + 1);
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
