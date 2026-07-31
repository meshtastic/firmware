#pragma once
// Portduino webserver is built whenever the ulfius headers are reachable,
// not only on Linux. macOS users can `brew install ulfius` to enable it;
// without ulfius the entire body is skipped and main.cpp's matching
// __has_include guard avoids referencing the type.
#ifdef ARCH_PORTDUINO
#if __has_include(<ulfius.h>)
#include "PhoneAPI.h"
#include "ulfius-cfg.h"
#include "ulfius.h"
#include <Arduino.h>
#include <array>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

#define STATIC_FILE_CHUNK 256

void initWebServer();
void createSSLCert();
int callback_static_file(const struct _u_request *request, struct _u_response *response, void *user_data);
const char *get_filename_ext(const char *path);

struct _file_config {
    char *files_path;
    char *url_prefix;
    struct _u_map mime_types;
    struct _u_map map_header;
    char *redirect_on_404;
    char *rootPath;
};

class HttpAPI : public PhoneAPI
{

  public:
    HttpAPI() { api_type = TYPE_HTTP; }

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override { return true; } // FIXME, be smarter about this

    bool submitToRadio(const uint8_t *data, size_t length);
    bool submitFromRadio(uint8_t *data, size_t &length);
    bool hasPendingRequests();
    size_t pendingRequestCount();
    void processPendingRequests();
    void stopAcceptingRequests();

  private:
    static constexpr size_t REQUEST_QUEUE_SIZE = 8;
    static constexpr uint32_t REQUEST_TIMEOUT_MS = 5000;
    enum class RequestType : uint8_t { TO_RADIO, FROM_RADIO };
    struct PendingRequest {
        RequestType type;
        std::array<uint8_t, MAX_TO_FROM_RADIO_SIZE> data{};
        size_t length = 0;
        std::mutex completionMutex;
        std::condition_variable completion;
        bool completed = false;
        bool cancelled = false;
    };

    bool submit(const std::shared_ptr<PendingRequest> &request);
    std::mutex requestMutex;
    std::condition_variable requestDrain;
    std::deque<std::shared_ptr<PendingRequest>> requests;
    std::deque<std::shared_ptr<PendingRequest>> inFlightRequests;
    bool acceptingRequests = true;
};

class PiWebServerThread
{
  private:
    char *key_pem = NULL;
    char *cert_pem = NULL;
    // struct _u_map mime_types;
    std::string webrootpath;
    HttpAPI webAPI;

  public:
    PiWebServerThread();
    ~PiWebServerThread();
    void processPendingRequests();
    int CreateSSLCertificate();
    int CheckSSLandLoad();
    uint32_t requestRestart = 0;
    struct _u_instance instanceWeb;
};

extern PiWebServerThread *piwebServerThread;

#endif
#endif
