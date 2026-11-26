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
#include <algorithm>
#include <deque>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <ulfius.h>
#include <vector>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static constexpr uint32_t WS_IDLE_TIMEOUT_MS = 15 * 60 * 1000UL;
// One drain slice: a longer one starves loop() during a NodeDB dump.
static constexpr uint32_t WS_WRITE_BUDGET_MS = 100;
static constexpr int32_t WS_ATTACHED_INTERVAL_MS = 50;
static constexpr int32_t WS_DETACHED_INTERVAL_MS = 500;
static constexpr unsigned int WS_CLOSE_POLL_MS = 100;
static constexpr unsigned int WS_CLOSE_GRACE_MS = 2000;
static constexpr size_t WS_MAX_INBOUND = 32;

// Close codes, the same on the ESP32 route. 4001 tells a client it was replaced, so it can
// choose not to reconnect and take the slot straight back.
static constexpr uint16_t WS_CLOSE_NORMAL = 1000;
static constexpr uint16_t WS_CLOSE_TOO_BIG = 1009;
static constexpr uint16_t WS_CLOSE_REPLACED = 4001;

// Ulfius runs its callbacks on its own threads. They only move bytes across this lock; the
// PhoneAPI itself stays on the main loop.
static std::mutex wsLock;
static struct _websocket_manager *wsClient;
// Connections on their way out. One stays listed until its close callback, the last ulfius runs for it.
static std::vector<struct _websocket_manager *> wsDisplaced;
static std::deque<std::string> wsInbound;
// Moves whenever wsClient does, so the main loop can tell one client's session from the next.
static uint32_t wsGeneration;

static bool isDisplaced(const struct _websocket_manager *manager)
{
    return std::find(wsDisplaced.begin(), wsDisplaced.end(), manager) != wsDisplaced.end();
}

static void setClient(struct _websocket_manager *manager)
{
    wsClient = manager;
    wsInbound.clear();
    wsGeneration++;
}

/// Make manager the one attached client. A newer client takes the slot, as a new TCP client does.
static bool attachClient(struct _websocket_manager *manager)
{
    if (wsClient == manager)
        return true;
    if (isDisplaced(manager))
        return false;
    if (wsClient)
        wsDisplaced.push_back(wsClient);
    setClient(manager);
    return true;
}

/// Close with a status code. Ulfius drops the payload of a close frame sent through its API,
/// so the frame goes to its socket directly, under its own write lock.
static void sendClose(struct _websocket_manager *manager, uint16_t code)
{
    const uint8_t frame[4] = {0x88, 0x02, (uint8_t)(code >> 8), (uint8_t)(code & 0xFF)};
    if (pthread_mutex_lock(&manager->write_lock) == 0) {
        send(manager->mhd_sock, frame, sizeof(frame), MSG_NOSIGNAL);
        pthread_mutex_unlock(&manager->write_lock);
    }
    ulfius_websocket_send_close_signal(manager);
}

static void onWebSocketOpen(const struct _u_request *request, struct _websocket_manager *manager, void *userData)
{
#ifndef U_DISABLE_WS_MESSAGE_LIST
    // Ulfius otherwise keeps every message of the connection in memory.
    manager->keep_messages = U_WEBSOCKET_KEEP_NONE;
#endif
    {
        std::lock_guard<std::mutex> guard(wsLock);
        attachClient(manager);
    }
    // Returning closes the socket, so hold it until the peer leaves or a newer client takes over.
    bool replaced = false;
    while (!replaced && !manager->close_flag &&
           ulfius_websocket_wait_close(manager, WS_CLOSE_POLL_MS) == U_WEBSOCKET_STATUS_OPEN) {
        std::lock_guard<std::mutex> guard(wsLock);
        replaced = wsClient != manager;
    }
    if (replaced)
        sendClose(manager, WS_CLOSE_REPLACED);
    // Ulfius's reader waits out an unfinished frame and the close signal does not interrupt it,
    // so a peer that stops mid-frame would keep its thread and socket for good.
    if (ulfius_websocket_wait_close(manager, WS_CLOSE_GRACE_MS) == U_WEBSOCKET_STATUS_OPEN)
        manager->connected = 0;
    std::lock_guard<std::mutex> guard(wsLock);
    if (!isDisplaced(manager))
        wsDisplaced.push_back(manager);
    if (wsClient == manager)
        setClient(nullptr);
}

static void onWebSocketMessage(const struct _u_request *request, struct _websocket_manager *manager,
                               const struct _websocket_message *message, void *userData)
{
    if (message->opcode != U_WEBSOCKET_OPCODE_BINARY || message->data_len == 0)
        return;
    if (message->data_len > MAX_TO_FROM_RADIO_SIZE) {
        sendClose(manager, WS_CLOSE_TOO_BIG);
        return;
    }
    std::lock_guard<std::mutex> guard(wsLock);
    // The first message can beat onWebSocketOpen to the lock.
    if (attachClient(manager) && wsInbound.size() < WS_MAX_INBOUND)
        wsInbound.emplace_back(message->data, message->data_len);
}

static void onWebSocketClose(const struct _u_request *request, struct _websocket_manager *manager, void *userData)
{
    std::lock_guard<std::mutex> guard(wsLock);
    wsDisplaced.erase(std::remove(wsDisplaced.begin(), wsDisplaced.end(), manager), wsDisplaced.end());
    if (wsClient == manager)
        setClient(nullptr);
}

static int handleAPIv1WebSocket(const struct _u_request *req, struct _u_response *res, void *userData)
{
    // With no protocol named, ulfius echoes whichever one the client offers. None is spoken here.
    if (u_map_has_key_case(req->map_header, "Sec-WebSocket-Protocol")) {
        ulfius_set_string_body_response(res, 400, "No WebSocket subprotocol is supported");
        return U_CALLBACK_COMPLETE;
    }
    if (ulfius_set_websocket_response(res, NULL, NULL, &onWebSocketOpen, NULL, &onWebSocketMessage, NULL, &onWebSocketClose,
                                      NULL) != U_OK)
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

  private:
    uint32_t servedGeneration = 0;
};

int32_t PiWebSocketAPI::runOnce()
{
    std::deque<std::string> inbound;
    uint32_t generation;
    bool attached;
    {
        std::lock_guard<std::mutex> guard(wsLock);
        inbound.swap(wsInbound);
        generation = wsGeneration;
        attached = wsClient != nullptr;
    }
    if (generation != servedGeneration) {
        LOG_INFO("WebSocket API client %s", attached ? "connected" : "disconnected");
        servedGeneration = generation;
        close();
        lastContactMsec = Time::skipZero(Time::getMillis());
    }
    for (const std::string &message : inbound)
        handleToRadio((const uint8_t *)message.data(), message.size());
    if (!attached)
        return WS_DETACHED_INTERVAL_MS;

    const bool idle = Throttle::hasElapsed(lastContactMsec, WS_IDLE_TIMEOUT_MS);
    uint8_t buf[MAX_TO_FROM_RADIO_SIZE];
    const uint32_t started = Time::getMillis();
    std::lock_guard<std::mutex> guard(wsLock);
    // The state above belongs to the client seen at the snapshot. A different one gets nothing from it.
    if (wsGeneration != generation || !wsClient)
        return 1;
    if (idle) {
        LOG_WARN("WebSocket API client idle, closing");
        sendClose(wsClient, WS_CLOSE_NORMAL);
        return WS_ATTACHED_INTERVAL_MS;
    }
    while (Throttle::isWithinTimespanMs(started, WS_WRITE_BUDGET_MS)) {
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
    // One for the process, shared by every listener: ulfius callbacks may still run while a framework stops.
    static PiWebSocketAPI *api;
    if (!api)
        api = new PiWebSocketAPI();
    ulfius_add_endpoint_by_val(instance, "GET", "", "/api/v1/ws", 1, &handleAPIv1WebSocket, NULL);
}

#endif
#endif
#endif
