#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_API)

#include "concurrency/OSThread.h"
#include "ethApiHandlers.h"
#include "ethApiServer.h"
#include <Arduino.h>

#ifdef USE_ARDUINO_ETHERNET
#include <Ethernet.h>
#else
#include <RAK13800_W5100S.h>
#endif

// Adaptive poll intervals (mirror mesh/http/WebServer.cpp ESP32 pattern).
static constexpr uint32_t ACTIVE_THRESHOLD_MS = 5000;
static constexpr uint32_t MEDIUM_THRESHOLD_MS = 30000;
static constexpr int32_t ACTIVE_INTERVAL_MS = 20;
static constexpr int32_t MEDIUM_INTERVAL_MS = 100;
static constexpr int32_t IDLE_INTERVAL_MS = 500;

static EthernetServer *apiServer = nullptr;

// Adapter that exposes an EthernetClient through the transport-agnostic
// IStreamReadWrite interface so the handlers in ethApiHandlers.cpp can drive
// it the same way they drive the TLS transport.
class EthernetClientStream : public IStreamReadWrite
{
  public:
    explicit EthernetClientStream(EthernetClient &c) : c_(c) {}

    size_t write(uint8_t b) override { return c_.write(b); }
    size_t write(const uint8_t *buf, size_t len) override { return c_.write(buf, len); }

    int available() override { return c_.available(); }
    int read() override { return c_.read(); }
    int read(uint8_t *buf, size_t len) override { return c_.read(buf, len); }

    bool connected() override { return c_.connected(); }
    void flush() override { c_.flush(); }
    IPAddress remoteIP() override { return c_.remoteIP(); }

  private:
    EthernetClient &c_;
};

// Dedicated OSThread so accept() runs on sub-second cadence. The Ethernet
// client periodic ticks every 5s which is fine for NTP/MQTT but cripples a
// chatty web client doing many small back-to-back requests (6.5s TTFB observed
// before; W5500 sockets get exhausted faster than the periodic can drain).
class EthApiServerThread : public concurrency::OSThread
{
  public:
    EthApiServerThread() : concurrency::OSThread("EthApiServer") { lastActivityMs = millis(); }

  protected:
    int32_t runOnce() override
    {
        if (apiServer) {
            EthernetClient client = apiServer->accept();
            if (client) {
                lastActivityMs = millis();
                EthernetClientStream stream(client);
                handleApiClient(stream);
                client.stop();
            }
        }

        uint32_t since = millis() - lastActivityMs;
        if (since < ACTIVE_THRESHOLD_MS)
            return ACTIVE_INTERVAL_MS;
        if (since < MEDIUM_THRESHOLD_MS)
            return MEDIUM_INTERVAL_MS;
        return IDLE_INTERVAL_MS;
    }

  private:
    uint32_t lastActivityMs;
};

static EthApiServerThread *apiThread = nullptr;

void initEthApiServer()
{
    // Bind the listener (idempotent - deInitEthApiServer() drops apiServer on a
    // W5500 reset, and this rebinds it on the restart path).
    if (!apiServer) {
        apiServer = new EthernetServer(ETH_API_PORT);
        apiServer->begin();
        LOG_INFO("ETH API: server listening on TCP port %d (phase 2.0, OSThread @ 20ms)", ETH_API_PORT);
    }
    // The worker is created once and kept for the lifetime of the process. It
    // idles harmlessly while apiServer is null (runOnce guards on it), so we
    // never delete it from another thread's runOnce - that would corrupt the
    // scheduler's thread list mid-iteration.
    if (!apiThread)
        apiThread = new EthApiServerThread(); // OSThread base auto-registers with the scheduler
}

void deInitEthApiServer()
{
    // A W5500 chip reset wipes the hardware socket table, so the listener is now
    // bound to a dead socket. Drop it (the worker stays alive and idles) so the
    // next initEthApiServer() from reconnectETH's restart path rebinds TCP/80.
    if (apiServer) {
        delete apiServer;
        apiServer = nullptr;
    }
}

#endif // HAS_ETHERNET && HAS_ETHERNET_API
