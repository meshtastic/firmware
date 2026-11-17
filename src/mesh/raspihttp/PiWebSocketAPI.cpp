#ifdef ARCH_PORTDUINO
#if __has_include(<ulfius.h>)
// First, in its own block so the include sorter keeps it there: configuration.h supplies the
// variant defines mesh-pb-constants.h needs (portduino resolves MAX_NUM_NODES at runtime).
#include "configuration.h"

#include "PiWebSocketAPI.h"

#ifdef HAS_PI_WEBSOCKET_API
#include "PhoneAPI.h"
#include "Throttle.h"
#include "UptimeClock.h"
#include "concurrency/OSThread.h"
#include <deque>
#include <mutex>
#include <string>
#include <ulfius.h>

static constexpr uint32_t WS_IDLE_TIMEOUT_MS = 15 * 60 * 1000UL;
// One drain slice: a longer one starves loop() during a NodeDB dump.
static constexpr uint32_t WS_WRITE_BUDGET_MS = 100;
static constexpr int32_t WS_ATTACHED_INTERVAL_MS = 50;
static constexpr int32_t WS_DETACHED_INTERVAL_MS = 500;
static constexpr unsigned int WS_CLOSE_POLL_MS = 100;
static constexpr size_t WS_MAX_INBOUND = 32;

// Ulfius runs its callbacks on its own threads. They only move bytes across this lock; the
// PhoneAPI itself stays on the main loop.
static std::mutex wsLock;
static struct _websocket_manager *wsClient;
static struct _websocket_manager *wsRetired;
static std::deque<std::string> wsInbound;
static bool wsClientChanged;

/// Make manager the one attached client. A newer client takes the slot, as a new TCP client does.
static bool attachClient(struct _websocket_manager *manager)
{
    if (wsClient == manager)
        return true;
    if (wsRetired == manager)
        return false;
    wsRetired = wsClient;
    wsClient = manager;
    wsInbound.clear();
    wsClientChanged = true;
    return true;
}

static void onWebSocketOpen(const struct _u_request *request, struct _websocket_manager *manager, void *userData)
{
    {
        std::lock_guard<std::mutex> guard(wsLock);
        attachClient(manager);
    }
    // Returning closes the socket, so hold it until the peer leaves or a newer client takes over.
    while (ulfius_websocket_wait_close(manager, WS_CLOSE_POLL_MS) == U_WEBSOCKET_STATUS_OPEN) {
        std::lock_guard<std::mutex> guard(wsLock);
        if (wsClient != manager)
            break;
    }
    std::lock_guard<std::mutex> guard(wsLock);
    if (wsRetired == manager)
        wsRetired = nullptr;
    if (wsClient == manager) {
        wsClient = nullptr;
        wsInbound.clear();
        wsClientChanged = true;
    }
}

static void onWebSocketMessage(const struct _u_request *request, struct _websocket_manager *manager,
                               const struct _websocket_message *message, void *userData)
{
    if (message->opcode != U_WEBSOCKET_OPCODE_BINARY || message->data_len == 0 || message->data_len > MAX_TO_FROM_RADIO_SIZE)
        return;
    std::lock_guard<std::mutex> guard(wsLock);
    // The first message can beat onWebSocketOpen to the lock.
    if (attachClient(manager) && wsInbound.size() < WS_MAX_INBOUND)
        wsInbound.emplace_back(message->data, message->data_len);
}

static int handleAPIv1WebSocket(const struct _u_request *req, struct _u_response *res, void *userData)
{
    if (ulfius_set_websocket_response(res, NULL, NULL, &onWebSocketOpen, NULL, &onWebSocketMessage, NULL, NULL, NULL) != U_OK)
        ulfius_set_string_body_response(res, 400, "WebSocket upgrade failed");
    return U_CALLBACK_COMPLETE;
}

class PiWebSocketAPI : public PhoneAPI, private concurrency::OSThread
{
  public:
    PiWebSocketAPI() : concurrency::OSThread("WebSocketAPI") { api_type = TYPE_HTTP; }

    bool checkIsConnected() override { return true; }

  protected:
    void onNowHasData(uint32_t fromRadioNum) override { setIntervalFromNow(0); }

    int32_t runOnce() override;
};

int32_t PiWebSocketAPI::runOnce()
{
    std::deque<std::string> inbound;
    bool changed, attached;
    {
        std::lock_guard<std::mutex> guard(wsLock);
        inbound.swap(wsInbound);
        changed = wsClientChanged;
        wsClientChanged = false;
        attached = wsClient != nullptr;
    }
    if (changed) {
        LOG_INFO("WebSocket API client %s", attached ? "connected" : "disconnected");
        close();
        lastContactMsec = 0;
    }
    for (const std::string &message : inbound)
        handleToRadio((const uint8_t *)message.data(), message.size());
    if (!attached)
        return WS_DETACHED_INTERVAL_MS;

    const bool idle = lastContactMsec > 0 && Throttle::hasElapsed(lastContactMsec, WS_IDLE_TIMEOUT_MS);
    uint8_t buf[MAX_TO_FROM_RADIO_SIZE];
    const uint32_t started = Time::getMillis();
    std::lock_guard<std::mutex> guard(wsLock);
    if (idle && wsClient) {
        LOG_WARN("WebSocket API client idle, closing");
        ulfius_websocket_send_close_signal(wsClient);
        return WS_ATTACHED_INTERVAL_MS;
    }
    while (wsClient && Throttle::isWithinTimespanMs(started, WS_WRITE_BUDGET_MS)) {
        const size_t len = getFromRadio(buf);
        if (len == 0)
            return WS_ATTACHED_INTERVAL_MS;
        if (ulfius_websocket_send_message(wsClient, U_WEBSOCKET_OPCODE_BINARY, len, (const char *)buf) != U_OK) {
            ulfius_websocket_send_close_signal(wsClient);
            return WS_ATTACHED_INTERVAL_MS;
        }
    }
    return 1;
}

void initPiWebSocketAPI(struct _u_instance *instance)
{
    // Lives for the process: ulfius callbacks may still run while the framework stops.
    new PiWebSocketAPI();
    ulfius_add_endpoint_by_val(instance, "GET", "", "/api/v1/ws", 1, &handleAPIv1WebSocket, NULL);
}

#endif
#endif
#endif
