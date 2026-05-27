#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_API)

#include "ethApiServer.h"
#include <Arduino.h>

#ifdef USE_ARDUINO_ETHERNET
#include <Ethernet.h>
#else
#include <RAK13800_W5100S.h>
#endif

static constexpr uint32_t HEADER_TIMEOUT_MS = 3000;
static constexpr size_t MAX_LINE_LEN = 256;

static EthernetServer *apiServer = nullptr;

static void send503(EthernetClient &client)
{
    static const char body[] = "ethApiServer phase 0 — handlers not yet implemented\n";
    client.print("HTTP/1.1 503 Service Unavailable\r\n");
    client.print("Content-Type: text/plain; charset=utf-8\r\n");
    client.print("Content-Length: ");
    client.print((unsigned)(sizeof(body) - 1));
    client.print("\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print(body);
    client.flush();
}

// Read up to one CRLF-terminated line; returns false on timeout or oversize line.
static bool readLine(EthernetClient &client, String &out, uint32_t deadlineMs)
{
    out = "";
    while ((int32_t)(millis() - deadlineMs) < 0) {
        if (!client.connected())
            return false;
        if (!client.available()) {
            delay(1);
            continue;
        }
        int c = client.read();
        if (c < 0)
            continue;
        if (c == '\n')
            return true;
        if (c == '\r')
            continue;
        if (out.length() >= MAX_LINE_LEN)
            return false;
        out += (char)c;
    }
    return false;
}

static void handleClient(EthernetClient &client)
{
    const uint32_t deadline = millis() + HEADER_TIMEOUT_MS;

    String reqLine;
    if (!readLine(client, reqLine, deadline) || reqLine.length() == 0) {
        LOG_DEBUG("ETH API: empty/timeout request from %s", client.remoteIP().toString().c_str());
        return;
    }

    // Drain remaining headers until blank line (or deadline). We don't parse
    // anything yet — phase 0 just logs and replies 503.
    String hdr;
    while (readLine(client, hdr, deadline)) {
        if (hdr.length() == 0)
            break;
    }

    LOG_INFO("ETH API: %s (from %s)", reqLine.c_str(), client.remoteIP().toString().c_str());
    send503(client);
}

void initEthApiServer()
{
    if (apiServer)
        return;
    apiServer = new EthernetServer(ETH_API_PORT);
    apiServer->begin();
    LOG_INFO("ETH API: server listening on TCP port %d (phase 0 — 503 only)", ETH_API_PORT);
}

void ethApiServerLoop()
{
    if (!apiServer)
        return;
    EthernetClient client = apiServer->accept();
    if (client) {
        handleClient(client);
        client.stop();
    }
}

#endif // HAS_ETHERNET && HAS_ETHERNET_API
