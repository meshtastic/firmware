#include "configuration.h"

#if HAS_ETHERNET && (defined(HAS_ETHERNET_API) || defined(HAS_ETHERNET_TLS_API))

#include "ethApiHandlers.h"
#include "mesh/PhoneAPI.h"
#include <Arduino.h>
#include <vector>

#ifdef ARCH_RP2040
#include <hardware/watchdog.h>
#endif

// HEADER_TIMEOUT_MS doubles as the keep-alive idle window: handleApiClient
// loops reading requests on the same connection, and bails out if no new
// request line arrives within this budget. 3 s is comfortable for browser
// pipelining without leaving the OSThread blocked too long after a client
// goes quiet.
static constexpr uint32_t HEADER_TIMEOUT_MS = 3000;
static constexpr uint32_t BODY_TIMEOUT_MS = 5000;
// A request body still missing when the session window closes gets at least this long, so a PUT
// that lands right at the end of a session is not dropped over a few milliseconds of lag.
static constexpr uint32_t BODY_MIN_WAIT_MS = 500;
// Input that keeps arriving is read for at most this long past a deadline. Reading what is already
// buffered takes milliseconds (the W5x00 holds 2 KB per socket), but without a cap a client feeding
// one byte per poll could stretch a request by MAX_HEADER_LINES * MAX_LINE_LEN polls, about 8 s.
static constexpr uint32_t CONSUME_GRACE_MS = 500;
static constexpr size_t MAX_LINE_LEN = 256;
static constexpr size_t MAX_HEADER_LINES = 32;
static constexpr const char *PROTOBUF_SCHEMA =
    "https://raw.githubusercontent.com/meshtastic/protobufs/master/meshtastic/mesh.proto";

// PhoneAPI subclass for the Ethernet HTTP transport. Mirrors mesh/http/HttpAPI
// but lives outside the MESHTASTIC_EXCLUDE_WEBSERVER gate (which is ESP32-only).
// A single instance is shared between the HTTP and HTTPS servers since they
// represent the same logical "phone" - same state machine, same packet queue.
class EthHttpAPI : public PhoneAPI
{
  public:
    EthHttpAPI() { api_type = TYPE_HTTP; }
    bool checkIsConnected() override { return true; }
};

static EthHttpAPI webAPI;

struct Request {
    String method;
    String path;
    String query; // raw "key=val&..." (without leading '?'), empty if none
    long contentLength = 0;
};

// Whichever of two millis() deadlines comes first, safe across the 32-bit wrap.
static uint32_t earlierDeadline(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0 ? a : b;
}

static bool deadlinePassed(uint32_t deadlineMs)
{
    return (int32_t)(millis() - deadlineMs) >= 0;
}

// Read up to one CRLF-terminated line; returns false on timeout or oversize.
// The deadline bounds how long we wait for more bytes; input that has already arrived is still read
// for up to CONSUME_GRACE_MS after it, so a request that is fully buffered is never dropped just
// because its window closed, and one that keeps trickling in cannot hold the session open.
static bool readLine(IStreamReadWrite &client, String &out, uint32_t deadlineMs)
{
    out = "";
    while (true) {
        if (!client.connected())
            return false;
        if (!client.available()) {
            if (deadlinePassed(deadlineMs))
                return false;
            delay(1);
            continue;
        }
        if (deadlinePassed(deadlineMs + CONSUME_GRACE_MS))
            return false;
        int c = client.read();
        if (c < 0) {
            // available() can count raw bytes that do not yet decode (a partial TLS record).
            if (deadlinePassed(deadlineMs))
                return false;
            delay(1);
            continue;
        }
        if (c == '\n')
            return true;
        if (c == '\r')
            continue;
        if (out.length() >= MAX_LINE_LEN)
            return false;
        out += (char)c;
    }
}

static bool parseRequest(IStreamReadWrite &client, Request &req, uint32_t deadlineMs)
{
    String line;
    if (!readLine(client, line, deadlineMs) || line.length() == 0)
        return false;

    // "METHOD PATH HTTP/1.1"
    int sp1 = line.indexOf(' ');
    int sp2 = line.indexOf(' ', sp1 + 1);
    if (sp1 <= 0 || sp2 <= sp1)
        return false;
    req.method = line.substring(0, sp1);
    String fullPath = line.substring(sp1 + 1, sp2);

    int q = fullPath.indexOf('?');
    if (q >= 0) {
        req.path = fullPath.substring(0, q);
        req.query = fullPath.substring(q + 1);
    } else {
        req.path = fullPath;
        req.query.remove(0);
    }

    // Headers until blank line. Only Content-Length is interesting for now.
    size_t hdrCount = 0;
    while (hdrCount++ < MAX_HEADER_LINES) {
        if (!readLine(client, line, deadlineMs))
            return false;
        if (line.length() == 0)
            return true; // end of headers
        if (line.length() >= 16) {
            // Case-insensitive prefix match for "Content-Length:"
            String head = line.substring(0, 15);
            head.toLowerCase();
            if (head == "content-length:") {
                String v = line.substring(15);
                v.trim();
                req.contentLength = v.toInt();
            }
        }
    }
    return false; // too many headers
}

// Trivial query-string param lookup. `out` set to value or "" if absent.
static bool getQueryParam(const String &query, const char *key, String &out)
{
    out.remove(0);
    if (query.length() == 0)
        return false;
    String needle = String(key) + "=";
    int idx = query.indexOf(needle);
    if (idx < 0)
        return false;
    if (idx > 0 && query.charAt(idx - 1) != '&')
        return false; // false positive (e.g. "all" matching "fall")
    int start = idx + needle.length();
    int end = query.indexOf('&', start);
    out = (end < 0) ? query.substring(start) : query.substring(start, end);
    return true;
}

static void writeStatusLine(IStreamReadWrite &client, int status, const char *reason)
{
    client.print("HTTP/1.1 ");
    client.print(status);
    client.print(' ');
    client.print(reason);
    client.print("\r\n");
}

static void writeCorsHeaders(IStreamReadWrite &client, const char *allowMethods)
{
    client.print("Access-Control-Allow-Origin: *\r\n");
    client.print("Access-Control-Allow-Headers: Content-Type\r\n");
    client.print("Access-Control-Allow-Methods: ");
    client.print(allowMethods);
    client.print("\r\n");
    client.print("X-Protobuf-Schema: ");
    client.print(PROTOBUF_SCHEMA);
    client.print("\r\n");
}

static void sendPreflight(IStreamReadWrite &client, const char *allowMethods)
{
    writeStatusLine(client, 204, "No Content");
    writeCorsHeaders(client, allowMethods);
    client.print("Content-Length: 0\r\n");
    client.print("Connection: keep-alive\r\n\r\n");
    client.flush();
}

static void sendError(IStreamReadWrite &client, int status, const char *reason, const char *body)
{
    writeStatusLine(client, status, reason);
    client.print("Content-Type: text/plain; charset=utf-8\r\n");
    client.print("Access-Control-Allow-Origin: *\r\n");
    client.print("Content-Length: ");
    client.print((unsigned)strlen(body));
    client.print("\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print(body);
    client.flush();
}

// Returns true if the connection may stay open (keep-alive), false if the
// handler emitted an error response with Connection: close framing.
static bool handleFromRadio(IStreamReadWrite &client, const Request &req)
{
    if (req.method == "OPTIONS") {
        sendPreflight(client, "GET, OPTIONS");
        return true;
    }
    if (req.method != "GET") {
        sendError(client, 405, "Method Not Allowed", "method must be GET or OPTIONS");
        return false;
    }

    String allParam;
    bool drainAll = getQueryParam(req.query, "all", allParam) && allParam == "true";

    // Buffer all packets first so we can emit an accurate Content-Length and
    // keep the connection alive. Phase 2 used Connection: close framing, which
    // forced clients to redo the TLS handshake (~625 ms) for every single
    // /fromradio poll - client.meshtastic.org needs dozens of those during
    // initial sync, so the user-visible load time was 15-30 s of pure
    // handshakes. With Content-Length + keep-alive a whole sync rides one
    // handshake. Buffer is dynamic (std::vector) so the common 1-packet case
    // only allocates ~256 B; the ?all=true 64-packet cap stays at ~16 KB.
    std::vector<uint8_t> body;
    uint8_t txBuf[MAX_TO_FROM_RADIO_SIZE];
    int packets = 0;
    do {
        size_t len = webAPI.getFromRadio(txBuf);
        if (len == 0)
            break;
        body.insert(body.end(), txBuf, txBuf + len);
        packets++;
    } while (drainAll && packets < 64); // safety cap so a misbehaving client can't loop forever

    writeStatusLine(client, 200, "OK");
    client.print("Content-Type: application/x-protobuf\r\n");
    writeCorsHeaders(client, "GET, OPTIONS");
    client.print("Content-Length: ");
    client.print((unsigned)body.size());
    client.print("\r\n");
    client.print("Connection: keep-alive\r\n\r\n");
    if (!body.empty())
        client.write(body.data(), body.size());
    client.flush();
    LOG_DEBUG("ETH API: fromradio sent %d packet(s), %u bytes (all=%d)", packets, (unsigned)body.size(), (int)drainAll);
    return true;
}

// Returns true if the connection may stay open (keep-alive), false if the
// handler emitted an error response with Connection: close framing.
static bool handleToRadio(IStreamReadWrite &client, const Request &req, uint32_t sessionDeadlineMs)
{
    if (req.method == "OPTIONS") {
        sendPreflight(client, "PUT, OPTIONS");
        return true;
    }
    if (req.method != "PUT") {
        sendError(client, 405, "Method Not Allowed", "method must be PUT or OPTIONS");
        return false;
    }

    if (req.contentLength <= 0 || (size_t)req.contentLength > MAX_TO_FROM_RADIO_SIZE) {
        sendError(client, 400, "Bad Request", "missing or oversized Content-Length");
        return false;
    }

    uint8_t buf[MAX_TO_FROM_RADIO_SIZE];
    size_t got = 0;
    const uint32_t now = millis();
    // Stay inside the session window (see handleApiClient), but never wait less than BODY_MIN_WAIT_MS.
    uint32_t deadline = sessionDeadlineMs;
    if ((int32_t)(deadline - (now + BODY_MIN_WAIT_MS)) < 0)
        deadline = now + BODY_MIN_WAIT_MS;
    deadline = earlierDeadline(deadline, now + BODY_TIMEOUT_MS);
    while (got < (size_t)req.contentLength) {
        if (!client.connected())
            break;
        if (!client.available()) {
            if (deadlinePassed(deadline))
                break;
            delay(1);
            continue;
        }
        if (deadlinePassed(deadline + CONSUME_GRACE_MS))
            break;
        int n = client.read(buf + got, (size_t)req.contentLength - got);
        if (n > 0) {
            got += n;
        } else {
            if (deadlinePassed(deadline))
                break;
            delay(1);
        }
    }
    if (got != (size_t)req.contentLength) {
        LOG_WARN("ETH API: toradio short read (%u/%ld)", (unsigned)got, req.contentLength);
        sendError(client, 408, "Request Timeout", "body read incomplete");
        return false;
    }

    webAPI.handleToRadio(buf, got);

    // Echo the bytes back, matching mesh/http ESP32 semantics.
    writeStatusLine(client, 200, "OK");
    client.print("Content-Type: application/x-protobuf\r\n");
    writeCorsHeaders(client, "PUT, OPTIONS");
    client.print("Content-Length: ");
    client.print((unsigned)got);
    client.print("\r\n");
    client.print("Connection: keep-alive\r\n\r\n");
    client.write(buf, got);
    client.flush();
    LOG_DEBUG("ETH API: toradio handled %u bytes", (unsigned)got);
    return true;
}

void handleApiClient(IStreamReadWrite &client)
{
    // HTTP/1.1 keep-alive loop. parseRequest returns false on idle timeout
    // (3 s) or peer close, which is also our exit signal. Errors close the
    // connection eagerly via sendError; normal responses use Content-Length
    // framing + Connection: keep-alive so the loop can read the next request
    // on the same TLS session.
    //
    // MAX_REQUESTS_PER_SESSION and MAX_SESSION_MS bound the loop because, while we are inside it,
    // the parent OSThread does not return to mainController. The watchdog is fed below after every
    // request, so the board stays up, but nothing else on core0 runs: MQTT, the radio, the router.
    // (yield() does not help: on RP2350 it is a bare taskYIELD() and does not run the OSThreads.)
    // A client.meshtastic.org sync produces ~80 back-to-back requests on one session, and a client
    // that keeps polling /api/v1/fromradio can hold a session for minutes. On a pico2_w5500_e22 that
    // was long enough for the 15 s MQTT keep-alive to lapse and for the broker to drop the node.
    // MAX_SESSION_MS bounds the time rather than the request count, and every wait below (next
    // request, request body) ends at the session deadline, so control is back in loop() within a
    // few seconds. After either cap the client just re-handshakes once and carries on.
    static constexpr int MAX_REQUESTS_PER_SESSION = 64;
    static constexpr uint32_t MAX_SESSION_MS = 3000;

    const uint32_t sessionDeadline = millis() + MAX_SESSION_MS;
    int requestsServed = 0;
    while (client.connected() && requestsServed < MAX_REQUESTS_PER_SESSION && !deadlinePassed(sessionDeadline)) {
        const uint32_t deadline = earlierDeadline(millis() + HEADER_TIMEOUT_MS, sessionDeadline);
        Request req;
        if (!parseRequest(client, req, deadline)) {
            if (requestsServed == 0)
                LOG_DEBUG("ETH API: bad/timeout request from %s", client.remoteIP().toString().c_str());
            return;
        }

        LOG_INFO("ETH API: %s %s%s%s (from %s)", req.method.c_str(), req.path.c_str(), req.query.length() ? "?" : "",
                 req.query.c_str(), client.remoteIP().toString().c_str());

        bool keepAlive = true;
        if (req.path == "/api/v1/fromradio") {
            keepAlive = handleFromRadio(client, req);
        } else if (req.path == "/api/v1/toradio") {
            keepAlive = handleToRadio(client, req, sessionDeadline);
        } else {
            sendError(client, 404, "Not Found", "unknown endpoint");
            return; // errors are terminal - Connection: close framing
        }
        // A handler that emitted an error advertised Connection: close. Stop the
        // keep-alive loop so any unread/leftover body bytes (e.g. after a 408
        // short read or a 400 oversized PUT) aren't misparsed as the next request.
        if (!keepAlive)
            return;
        requestsServed++;
#ifdef ARCH_RP2040
        // yield() ONLY cedes to the OSThread scheduler; it does not call
        // rp2040Loop() where watchdog_update() lives. While we sit inside
        // serveClient() looping over keep-alive requests, main loop() is
        // not running and the 8 s hardware watchdog WILL fire. Pet it
        // explicitly here.
        watchdog_update();
#endif
        yield();
    }

    if (requestsServed >= MAX_REQUESTS_PER_SESSION)
        LOG_DEBUG("ETH API: session capped at %d requests (will redo handshake on next batch)", MAX_REQUESTS_PER_SESSION);
}

#endif // HAS_ETHERNET && (HAS_ETHERNET_API || HAS_ETHERNET_TLS_API)
