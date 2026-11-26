#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_WEBSERVER
#include "PhoneAPI.h"
#include "Throttle.h"
#include "UptimeClock.h"
#include "mesh/http/WebServer.h"
#include "mesh/http/WebSocketAPI.h"
#include <cstdlib>
#include <new>

// See the note in WebServer.cpp: esp32_https_server needs this before its headers.
#undef str

#include <HTTPSServer.hpp>
#include <HTTPServer.hpp>
#include <WebsocketHandler.hpp>
#include <WebsocketNode.hpp>

using namespace httpsserver;

static constexpr uint32_t WS_IDLE_TIMEOUT_MS = 15 * 60 * 1000UL;
// One drain slice: a longer one starves loop() during a NodeDB dump.
static constexpr uint32_t WS_WRITE_BUDGET_MS = 100;
// A client that takes nothing for this long is dropped.
static constexpr uint32_t WS_STALL_TIMEOUT_MS = 10 * 1000UL;
// Private close code, the same on the Portduino route: tells a client it was replaced, so it
// can choose not to reconnect and take the slot straight back.
static constexpr uint16_t WS_CLOSE_REPLACED = 4001;

class WebSocketPhoneAPI : public PhoneAPI
{
  public:
    WebSocketPhoneAPI() { api_type = TYPE_HTTP; }

    bool checkIsConnected() override { return true; }

    bool idleTimedOut() const { return lastContactMsec > 0 && Throttle::hasElapsed(lastContactMsec, WS_IDLE_TIMEOUT_MS); }

  protected:
    void onNowHasData(uint32_t fromRadioNum) override
    {
        if (webServerThread)
            webServerThread->wake();
    }
};

class MeshWebsocketHandler : public WebsocketHandler
{
  public:
    MeshWebsocketHandler();
    ~MeshWebsocketHandler() override;
    MeshWebsocketHandler(const MeshWebsocketHandler &) = delete;
    MeshWebsocketHandler &operator=(const MeshWebsocketHandler &) = delete;

    void onMessage(WebsocketInputStreambuf *input) override;

    void drain();

  private:
    void releaseAPI();
    /// Writes to the socket block, so a close frame only goes to a peer that is still reading.
    void shut(uint16_t status);

    WebSocketPhoneAPI *api = nullptr;
    uint32_t lastWritableMs = Time::getMillis();
};

// The one attached client. A newer client takes the slot, as a new TCP client does.
static MeshWebsocketHandler *activeHandler;

MeshWebsocketHandler::MeshWebsocketHandler()
{
    if (activeHandler) {
        LOG_INFO("WebSocket API client replaced by a new one");
        activeHandler->shut(WS_CLOSE_REPLACED);
    }
    activeHandler = this;

    // malloc, not new: a failed new reboots an ESP32, see the note on openAPI in ServerAPI.h.
    void *block = malloc(sizeof(WebSocketPhoneAPI));
    if (block)
        api = new (block) WebSocketPhoneAPI();
    else
        LOG_ERROR("No heap for WebSocket API connection (%u bytes)", (unsigned)sizeof(WebSocketPhoneAPI));
    LOG_INFO("WebSocket API client connected");
}

MeshWebsocketHandler::~MeshWebsocketHandler()
{
    releaseAPI();
    if (activeHandler == this) {
        activeHandler = nullptr;
        LOG_INFO("WebSocket API client disconnected");
    }
}

void MeshWebsocketHandler::releaseAPI()
{
    if (api) {
        api->~WebSocketPhoneAPI();
        free(api);
        api = nullptr;
    }
}

void MeshWebsocketHandler::shut(uint16_t status)
{
    releaseAPI();
    if (canSend())
        close(status);
    else
        abort();
}

void MeshWebsocketHandler::onMessage(WebsocketInputStreambuf *input)
{
    uint8_t buf[MAX_TO_FROM_RADIO_SIZE];
    const std::streamsize len = input->sgetn((char *)buf, sizeof(buf));

    if (!api || len <= 0)
        return;
    if (input->sgetc() != std::char_traits<char>::eof()) {
        LOG_WARN("WebSocket API message over %u bytes, closing", (unsigned)sizeof(buf));
        shut(CLOSE_TOO_BIG);
        return;
    }
    if (webServerThread)
        webServerThread->markActivity();
    api->handleToRadio(buf, (size_t)len);
}

void MeshWebsocketHandler::drain()
{
    if (closed())
        return;
    if (!api) {
        shut(CLOSE_TRY_AGAIN_LATER);
        return;
    }
    if (api->idleTimedOut()) {
        LOG_WARN("WebSocket API client idle, closing");
        shut(CLOSE_NORMAL_CLOSURE);
        return;
    }
    if (canSend()) {
        lastWritableMs = Time::getMillis();
    } else if (Throttle::hasElapsed(lastWritableMs, WS_STALL_TIMEOUT_MS)) {
        LOG_WARN("WebSocket API client stopped reading, dropping it");
        shut(CLOSE_GOING_AWAY);
        return;
    }

    uint8_t buf[MAX_TO_FROM_RADIO_SIZE];
    const uint32_t started = Time::getMillis();
    bool sent = false;
    // canSend() before every frame: a write the socket cannot take would block the main loop.
    while (canSend() && Throttle::isWithinTimespanMs(started, WS_WRITE_BUDGET_MS)) {
        const size_t len = api->getFromRadio(buf);
        if (len == 0)
            break;
        if (!send(buf, (uint16_t)len, SEND_TYPE_BINARY)) {
            LOG_WARN("WebSocket API write failed, dropping the client");
            releaseAPI();
            abort();
            return;
        }
        sent = true;
    }
    if (sent && webServerThread)
        webServerThread->markActivity();
}

static WebsocketHandler *createWebSocketAPIHandler()
{
    return new MeshWebsocketHandler();
}

void registerWebSocketAPI(HTTPServer *insecureServer, HTTPSServer *secureServer)
{
    WebsocketNode *node = new WebsocketNode(WEBSOCKET_API_PATH, &createWebSocketAPIHandler);
    if (secureServer)
        secureServer->registerNode(node);
    insecureServer->registerNode(node);
}

bool serviceWebSocketAPI()
{
    if (!activeHandler)
        return false;
    activeHandler->drain();
    return true;
}
#endif
