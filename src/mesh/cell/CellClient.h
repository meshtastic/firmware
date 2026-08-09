#pragma once

#include "configuration.h"

#if HAS_CELLULAR

#include <Arduino.h>
#include <Client.h>
#include <functional>
#include <vector>

// Arduino Client over the modem's AT socket API, so PubSubClient and anything else written
// against Client works unmodified. One socket only, and IPv4 only - the AT socket API cannot carry IPv6.

// RX ring capacity. MQTT's largest expected frame plus headroom; overflowing
// drops bytes with a log line rather than truncating silently.
#ifndef CELL_RX_BUFFER_SIZE
#define CELL_RX_BUFFER_SIZE 2048
#endif

class CellClient : public Client
{
  public:
    CellClient() {}
    ~CellClient() override;

    int connect(IPAddress ip, uint16_t port) override;

    // Blocks until AT+CIPSTART settles (OK plus its outcome URC) or socketConnectTimeoutMs()
    // elapses. Rejects an IPv6 literal outright - the AT socket API is IPv4 only.
    int connect(const char *host, uint16_t port) override;

    // Whether to negotiate TLS (AT+CIPSSL); set fresh by the caller before each connect().
    void setTlsEnabled(bool enabled) { tlsRequested = enabled; }

    size_t write(uint8_t b) override;

    // Chunks against the dialect's socketSendMax(), blocking per chunk until SEND OK/FAIL.
    size_t write(const uint8_t *buf, size_t size) override;

    // Services the modem and, if the RX ring is empty, asks for more pending bytes.
    int available() override;
    int read() override;
    int read(uint8_t *buf, size_t size) override;
    int peek() override;

    // No-op: write() already blocks until the modem confirms SEND OK, so nothing is buffered here.
    void flush() override;

    // Closes the AT socket (AT+CIPCLOSE) and resets the RX ring; safe to call when not opened.
    void stop() override;
    uint8_t connected() override;
    operator bool() override { return connected(); }

    using Print::write;

  private:
    // Idempotent: registers this class's URC handlers with the modem on first use only.
    static void ensureUrcHandlers();

    // Pump the modem until done() or the timeout expires.
    static bool waitFor(std::function<bool()> done, uint32_t timeoutMs);

    // Ask the modem for up to CELL_RX_BUFFER_SIZE pending bytes.
    void fetchPending(uint32_t timeoutMs);

    bool opened = false;
    bool tlsRequested = false;
};

#endif
