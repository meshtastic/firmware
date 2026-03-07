#include "configuration.h"

#if HAS_WIFI && defined(ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32C6)

#include "modules/AppModule/AppHttp.h"
#include "mesh/wifi/WiFiAPClient.h"
#include <Esp.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Root CA certificate bundle embedded in the ESP-IDF SDK
extern const uint8_t x509_crt_bundle[] asm("_binary_x509_crt_bundle_start");

// Maximum response size an app is allowed to receive (16 KB)
static const size_t MAX_RESPONSE_SIZE = 16 * 1024;

// Stack size for the HTTP task — WiFiClientSecure + mbedTLS handshake needs plenty of stack.
// Task stacks must live in internal RAM (PSRAM stacks require CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY).
// The stack is temporary and freed when the request completes.
// Response data (std::string) naturally goes to PSRAM via heap_caps_malloc_extmem_enable(256).
static const size_t HTTP_TASK_STACK = 8192;

// Async request state shared between the caller and the worker task
struct HttpAsyncState {
    std::string url;    // copied so the caller's string can go out of scope
    std::string result; // populated by the worker
    bool finished;      // set true when worker is done
};

static HttpAsyncState *pendingRequest = nullptr;

// Runs on a dedicated FreeRTOS task with enough stack for TLS
static void httpGetTask(void *param)
{
    HttpAsyncState *state = static_cast<HttpAsyncState *>(param);

    LOG_DEBUG("[AppHttp] Task started, free heap: %u, stack HWM: %u",
             ESP.getFreeHeap(), uxTaskGetStackHighWaterMark(NULL));

    WiFiClientSecure client;
    client.setCACertBundle(x509_crt_bundle);
    client.setTimeout(10);

    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(10000);

    LOG_DEBUG("[AppHttp] Connecting to: %s", state->url.c_str());

    if (!http.begin(client, state->url.c_str())) {
        LOG_WARN("[AppHttp] http.begin failed");
    } else {
        int httpCode = http.GET();
        LOG_DEBUG("[AppHttp] GET complete, code=%d, stack HWM: %u",
                 httpCode, uxTaskGetStackHighWaterMark(NULL));

        if (httpCode == HTTP_CODE_OK) {
            int len = http.getSize();
            if (len > (int)MAX_RESPONSE_SIZE) {
                LOG_WARN("[AppHttp] Response too large (%d bytes), skipping", len);
            } else {
                String payload = http.getString();
                state->result.assign(payload.c_str(), payload.length());
                LOG_INFO("[AppHttp] Got %u bytes response", (unsigned)state->result.size());
            }
        } else {
            LOG_WARN("[AppHttp] GET failed, code=%d (%s)", httpCode, http.errorToString(httpCode).c_str());
            char errBuf[128];
            int errCode = client.lastError(errBuf, sizeof(errBuf));
            LOG_WARN("[AppHttp] TLS last error: %d - %s", errCode, errBuf);
        }
        http.end();
    }

    LOG_DEBUG("[AppHttp] Task finishing");
    state->finished = true;
    vTaskDelete(NULL);
}

bool app_http_request(const char *url)
{
    if (!isWifiAvailable())
        return false;

    // Only one request at a time — reject if one is already in flight
    if (pendingRequest && !pendingRequest->finished)
        return false;

    // Clean up any previous completed request
    if (pendingRequest) {
        delete pendingRequest;
        pendingRequest = nullptr;
    }

    HttpAsyncState *state = new (std::nothrow) HttpAsyncState();
    if (!state)
        return false;

    state->url = url;
    state->finished = false;

    LOG_DEBUG("[AppHttp] Creating task, free heap: %u", ESP.getFreeHeap());

    BaseType_t created = xTaskCreate(httpGetTask, "appHttp", HTTP_TASK_STACK, state, 5, NULL);
    if (created != pdPASS) {
        LOG_ERROR("[AppHttp] Failed to create HTTP task");
        delete state;
        return false;
    }

    pendingRequest = state;
    return true;
}

bool app_http_response(std::string &result)
{
    if (!pendingRequest)
        return false;

    if (!pendingRequest->finished)
        return false;

    result = std::move(pendingRequest->result);

    delete pendingRequest;
    pendingRequest = nullptr;

    return true;
}

void app_http_cleanup()
{
    if (!pendingRequest)
        return;

    if (!pendingRequest->finished) {
        // Task is still running — it will set finished and vTaskDelete itself.
        // We can't safely delete the state while the task uses it, so just
        // wait briefly. If it doesn't finish, we leak the small state struct
        // rather than risk a crash.
        for (int i = 0; i < 50 && !pendingRequest->finished; i++)
            vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (pendingRequest->finished) {
        delete pendingRequest;
        pendingRequest = nullptr;
    }
}

bool app_http_is_connected()
{
    return isWifiAvailable();
}

#endif // HAS_WIFI && ARCH_ESP32 && !ESP32C6
