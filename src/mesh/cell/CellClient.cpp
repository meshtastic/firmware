#include "mesh/cell/CellClient.h"

#if HAS_CELLULAR

#include "mesh/cell/ATModem.h"
#include "mesh/cell/cellBearer.h"
#include <memory>

// Bound on a single available()/read() fetch, so the caller's loop keeps turning.
#define CELL_FETCH_TIMEOUT_MS 2000

// Consecutive AT+CIPRXGET failures before giving up on the socket rather than
// retrying forever - a caller that polls available() in a tight loop (the vendored
// PubSubClient's connect() wait, for one) would otherwise flood the shared AT queue.
#define CELL_RX_FAIL_LIMIT 3

// Single socket, so its state is file scope rather than per-instance.
static uint8_t rxRing[CELL_RX_BUFFER_SIZE];
static size_t rxHead = 0, rxTail = 0, rxCount = 0;

static bool sockConnected = false;   // the modem reports the socket up
static bool sockConnectFail = false; // the pending connect was refused
static bool rxPending = false;       // the modem is holding unread bytes
static bool socketAttached = false;
static uint8_t rxFailCount = 0; // consecutive AT+CIPRXGET failures

static void rxPush(const uint8_t *data, size_t len)
{
    size_t space = CELL_RX_BUFFER_SIZE - rxCount;
    if (len > space) {
        LOG_WARN("Cell RX buffer full, dropping %u bytes", (unsigned)(len - space));
        len = space;
    }
    for (size_t i = 0; i < len; i++) {
        rxRing[rxHead] = data[i];
        rxHead = (rxHead + 1) % CELL_RX_BUFFER_SIZE;
        rxCount++;
    }
}

static int rxPop()
{
    if (!rxCount)
        return -1;
    int c = rxRing[rxTail];
    rxTail = (rxTail + 1) % CELL_RX_BUFFER_SIZE;
    rxCount--;
    return c;
}

static void rxReset()
{
    rxHead = rxTail = rxCount = 0;
}

static void socketClosed(const char *why)
{
    if (sockConnected)
        LOG_INFO("Cell socket closed (%s)", why);
    sockConnected = false;
    rxPending = false;
    rxFailCount = 0;
}

void CellClient::ensureUrcHandlers()
{
    if (socketAttached || !cellModem)
        return;
    socketAttached = true;

    ATSocketEvents events;
    events.onData = [](const uint8_t *data, size_t n) { rxPush(data, n); };
    events.onClosed = [](const char *why) { socketClosed(why); };
    events.onConnected = [] { sockConnected = true; };
    events.onConnectFail = [] { sockConnectFail = true; };
    events.onRxPending = [](bool pending) { rxPending = pending; };
    cellModem->socketAttach(events);
}

bool CellClient::waitFor(std::function<bool()> done, uint32_t timeoutMs)
{
    uint32_t deadline = millis() + timeoutMs;
    while (!done()) {
        if ((int32_t)(millis() - deadline) >= 0)
            return done();
        cellModem->service();
        delay(1);
    }
    return true;
}

CellClient::~CellClient()
{
    if (opened)
        stop();
}

int CellClient::connect(IPAddress ip, uint16_t port)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return connect(buf, port);
}

int CellClient::connect(const char *host, uint16_t port)
{
    if (!cellModem || !isCellularAvailable()) {
        LOG_WARN("Cell connect refused: bearer is %s", getCellStateName(getCellState()));
        return 0;
    }

    // An IPv6 literal is silently read as its first four octets by the AT socket
    // API, so reject it here rather than let it fail as a wrong address.
    if (strchr(host, ':')) {
        LOG_ERROR("Cell socket is IPv4 only, refusing '%s'", host);
        return 0;
    }

    if (opened)
        stop();

    ensureUrcHandlers();
    rxReset();
    sockConnected = false;
    sockConnectFail = false;
    rxPending = false;
    rxFailCount = 0;

    // Manual receive mode, so payload bytes never arrive interleaved with
    // command responses.
    auto rxModeDone = std::make_shared<bool>(false);
    cellModem->socketRxMode([rxModeDone](ATResult, const String &) { *rxModeDone = true; });
    if (!waitFor([rxModeDone] { return *rxModeDone; }, 5000)) {
        LOG_ERROR("Cell modem did not accept manual receive mode");
        cellModem->cancelActive();
        return 0;
    }

    // Queued ahead of socketOpen() below - the AT queue is sequential, so this always
    // completes first. TLS support is assumed here; a rejection only warns, not refuses.
    cellModem->socketSetTls(tlsRequested, [](ATResult r, const String &resp) {
        if (r != ATResult::Ok)
            LOG_WARN("AT+CIPSSL rejected: %s", resp.c_str());
    });

    uint32_t connectTimeout = cellModem->socketConnectTimeoutMs();
    auto result = std::make_shared<ATResult>(ATResult::Timeout);
    auto finished = std::make_shared<bool>(false);

    cellModem->socketOpen(host, port, [result, finished](ATResult r, const String &) {
        *result = r;
        *finished = true;
    });

    bool settled = waitFor([finished] { return *finished || sockConnected || sockConnectFail; }, connectTimeout);

    // The outcome came as a URC the driver's final-URC match did not cover, or
    // not at all; either way the open is still holding the queue.
    if (!*finished)
        cellModem->cancelActive();

    bool ok = sockConnected || (*finished && *result == ATResult::Ok);
    if (!ok || sockConnectFail) {
        LOG_WARN("Cell %sconnect to %s:%u %s", tlsRequested ? "TLS " : "", host, port, settled ? "failed" : "timed out");
        sockConnectFail = false;
        sockConnected = false;
        opened = true; // so stop() closes the socket and clears any half-open state
        stop();
        return 0;
    }

    sockConnected = true;
    opened = true;
    LOG_INFO("Cell %ssocket connected to %s:%u", tlsRequested ? "TLS " : "", host, port);
    return 1;
}

size_t CellClient::write(uint8_t b)
{
    return write(&b, 1);
}

size_t CellClient::write(const uint8_t *buf, size_t size)
{
    if (!cellModem || !connected())
        return 0;

    size_t sendMax = cellModem->socketSendMax();
    size_t sent = 0;
    while (sent < size) {
        size_t chunk = size - sent;
        if (chunk > sendMax)
            chunk = sendMax;

        auto result = std::make_shared<ATResult>(ATResult::Timeout);
        auto finished = std::make_shared<bool>(false);

        cellModem->socketSend(buf + sent, chunk, [result, finished](ATResult r, const String &) {
            *result = r;
            *finished = true;
        });

        waitFor([finished] { return *finished; }, 20000);
        if (!*finished || *result != ATResult::Ok) {
            LOG_WARN("Cell send of %u bytes failed after %u sent", (unsigned)chunk, (unsigned)sent);
            if (!*finished)
                cellModem->cancelActive();
            socketClosed("send failed");
            return sent;
        }
        sent += chunk;
    }
    return sent;
}

void CellClient::fetchPending(uint32_t timeoutMs)
{
    if (!rxPending || !cellModem)
        return;

    size_t want = CELL_RX_BUFFER_SIZE - rxCount;
    if (!want)
        return;
    size_t recvMax = cellModem->socketRecvMax();
    if (want > recvMax)
        want = recvMax;

    auto result = std::make_shared<ATResult>(ATResult::Timeout);
    auto finished = std::make_shared<bool>(false);
    cellModem->socketRecv(want, timeoutMs, [result, finished](ATResult r, const String &) {
        *result = r;
        *finished = true;
    });
    waitFor([finished] { return *finished; }, timeoutMs);

    if (*result == ATResult::Ok) {
        rxFailCount = 0;
        return;
    }

    // No well-formed +CIPRXGET line came back to clear rxPending, so without this
    // the next available() would retry immediately, forever - give up on the socket
    // instead of flooding the shared AT queue with a request that keeps failing.
    if (++rxFailCount >= CELL_RX_FAIL_LIMIT)
        socketClosed("rx stalled");
}

int CellClient::available()
{
    if (cellModem)
        cellModem->service();

    if (!rxCount)
        fetchPending(CELL_FETCH_TIMEOUT_MS);

    return (int)rxCount;
}

int CellClient::read()
{
    if (!rxCount && !available())
        return -1;
    return rxPop();
}

int CellClient::read(uint8_t *buf, size_t size)
{
    if (!rxCount && !available())
        return -1;

    size_t n = 0;
    while (n < size && rxCount)
        buf[n++] = (uint8_t)rxPop();
    return (int)n;
}

int CellClient::peek()
{
    if (!rxCount && !available())
        return -1;
    return rxRing[rxTail];
}

void CellClient::flush()
{
    // write() only returns once the modem has answered SEND OK, so there is
    // nothing buffered on this side to push.
}

void CellClient::stop()
{
    if (!opened) {
        sockConnected = false;
        return;
    }
    opened = false;

    if (cellModem) {
        auto finished = std::make_shared<bool>(false);
        cellModem->socketClose([finished](ATResult, const String &) { *finished = true; });
        waitFor([finished] { return *finished; }, 10000);
    }

    sockConnected = false;
    rxPending = false;
    rxReset();
}

uint8_t CellClient::connected()
{
    if (cellModem)
        cellModem->service();

    // Bytes already buffered stay readable after the peer closes, matching
    // WiFiClient and what PubSubClient's read loop expects.
    return (opened && sockConnected) || rxCount > 0;
}

#endif
