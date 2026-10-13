#include "NativeDiscovery.h"

#if (defined(__linux__) && defined(MESHTASTIC_USE_AVAHI)) || defined(__APPLE__)

#include "DebugConfiguration.h"
#include "NodeDB.h"
#include "configuration.h"
#include "main.h"
#include "meshUtils.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#if defined(MESHTASTIC_USE_AVAHI)
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/simple-watch.h>
#elif defined(__APPLE__)
#include <arpa/inet.h>
#include <cerrno>
#include <dns_sd.h>
#include <poll.h>
#endif

namespace
{

constexpr int kRetryInitialMs = 1000;
constexpr int kRetryMaxMs = 5 * 60 * 1000;
constexpr int kRetrySliceMs = 200;
constexpr size_t kLogTextLen = 160;

class NativeDiscovery
{
  public:
    NativeDiscovery(int port) : port(port), shortName(owner.short_name), nodeId(nodeDB->getNodeId()), pioEnv(optstr(APP_ENV)) {}

    void start() { worker = std::thread(&NativeDiscovery::run, this); }

    ~NativeDiscovery()
    {
        stopping = true;
        if (worker.joinable())
            worker.join();
    }

    // The worker must not call LOG_*. Portduino leaves RedirectablePrint's buffer unlocked.
    void drainLog()
    {
        bool warn = false;
        char text[kLogTextLen];
        {
            std::lock_guard<std::mutex> lock(logMu);
            if (!logPending)
                return;
            warn = logWarn;
            memcpy(text, logText, kLogTextLen);
            logPending = false;
        }
        if (warn)
            LOG_WARN("%s", text);
        else
            LOG_INFO("%s", text);
    }

  private:
    const int port;
    const std::string shortName;
    const std::string nodeId;
    const std::string pioEnv;
    std::atomic<bool> stopping{false};
    std::thread worker;
    int retryMs = kRetryInitialMs;

    std::mutex logMu;
    bool logPending = false;
    bool logWarn = false;
    char logText[kLogTextLen]{};
    char lastQueued[kLogTextLen]{};

    void queueFormatted(bool warn, const char *fmt, va_list ap)
    {
        char buf[kLogTextLen];
        vsnprintf(buf, sizeof(buf), fmt, ap);
        std::lock_guard<std::mutex> lock(logMu);
        if (strcmp(buf, lastQueued) == 0)
            return;
        // One slot. Leave an undrained warning in place; info must not erase it.
        if (logPending && logWarn && !warn)
            return;
        snprintf(logText, sizeof(logText), "%s", buf);
        snprintf(lastQueued, sizeof(lastQueued), "%s", buf);
        logWarn = warn;
        logPending = true;
    }

    void noteFailure(const char *fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        queueFormatted(true, fmt, ap);
        va_end(ap);
    }

    void noteInfo(const char *fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        queueFormatted(false, fmt, ap);
        va_end(ap);
    }

    // Backoff resets only once the service is registered. A connected client is not enough.
    void noteRegistered(const char *fmt, ...)
    {
        retryMs = kRetryInitialMs;
        va_list ap;
        va_start(ap, fmt);
        queueFormatted(false, fmt, ap);
        va_end(ap);
    }

    void backoffSleep()
    {
        for (int waited = 0; waited < retryMs && !stopping; waited += kRetrySliceMs)
            std::this_thread::sleep_for(std::chrono::milliseconds(kRetrySliceMs));
        if (retryMs < kRetryMaxMs) {
            const int next = retryMs * 2;
            retryMs = next > kRetryMaxMs ? kRetryMaxMs : next;
        }
    }

#if defined(MESHTASTIC_USE_AVAHI)
    AvahiEntryGroup *group = nullptr;
    std::string serviceName = "Meshtastic";
    bool reconnect = false;

    void publish(AvahiClient *client)
    {
        if (!group)
            group = avahi_entry_group_new(client, &NativeDiscovery::groupCallback, this);
        if (!group) {
            noteFailure("Avahi entry group failed: %s", avahi_strerror(avahi_client_errno(client)));
            reconnect = true;
            return;
        }
        if (!avahi_entry_group_is_empty(group))
            return;

        const std::string shortNameTxt = "shortname=" + shortName;
        const std::string nodeIdTxt = "id=" + nodeId;
        const std::string pioEnvTxt = "pio_env=" + pioEnv;
        int result = avahi_entry_group_add_service(group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, AvahiPublishFlags(0),
                                                   serviceName.c_str(), "_meshtastic._tcp", nullptr, nullptr, port,
                                                   shortNameTxt.c_str(), nodeIdTxt.c_str(), pioEnvTxt.c_str(), nullptr);
        if (result == AVAHI_ERR_COLLISION) {
            renameService();
            if (!reconnect) {
                avahi_entry_group_reset(group);
                publish(client);
            }
            return;
        }
        if (result >= 0)
            result = avahi_entry_group_commit(group);
        if (result < 0) {
            noteFailure("Avahi service registration failed: %s", avahi_strerror(result));
            reconnect = true;
        }
    }

    void renameService()
    {
        char *alternative = avahi_alternative_service_name(serviceName.c_str());
        if (alternative) {
            serviceName = alternative;
            avahi_free(alternative);
        } else {
            reconnect = true;
        }
    }

    static void groupCallback(AvahiEntryGroup *entryGroup, AvahiEntryGroupState state, void *userdata)
    {
        auto *self = static_cast<NativeDiscovery *>(userdata);
        if (state == AVAHI_ENTRY_GROUP_ESTABLISHED)
            self->noteRegistered("Avahi service %s registered on port %d", self->serviceName.c_str(), self->port);
        else if (state == AVAHI_ENTRY_GROUP_COLLISION) {
            self->renameService();
            if (!self->reconnect) {
                avahi_entry_group_reset(entryGroup);
                self->publish(avahi_entry_group_get_client(entryGroup));
            }
        } else if (state == AVAHI_ENTRY_GROUP_FAILURE) {
            self->noteFailure("Avahi service failed: %s",
                              avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(entryGroup))));
            self->reconnect = true;
        }
    }

    static void clientCallback(AvahiClient *client, AvahiClientState state, void *userdata)
    {
        auto *self = static_cast<NativeDiscovery *>(userdata);
        if (state == AVAHI_CLIENT_S_RUNNING)
            self->publish(client);
        else if (state == AVAHI_CLIENT_S_REGISTERING || state == AVAHI_CLIENT_S_COLLISION) {
            if (self->group)
                avahi_entry_group_reset(self->group);
        } else if (state == AVAHI_CLIENT_FAILURE) {
            self->noteFailure("Avahi client failed: %s", avahi_strerror(avahi_client_errno(client)));
            self->reconnect = true;
        }
    }

    void run()
    {
        while (!stopping) {
            reconnect = false;
            AvahiSimplePoll *poll = avahi_simple_poll_new();
            if (!poll) {
                noteFailure("Avahi poll setup failed");
                backoffSleep();
                continue;
            }

            int error = 0;
            AvahiClient *client = avahi_client_new(avahi_simple_poll_get(poll), AvahiClientFlags(0),
                                                   &NativeDiscovery::clientCallback, this, &error);
            if (!client) {
                noteInfo("Avahi unavailable (%s); will retry", avahi_strerror(error));
                avahi_simple_poll_free(poll);
                backoffSleep();
                continue;
            }
            while (client && !stopping && !reconnect && avahi_simple_poll_iterate(poll, 250) >= 0) {
            }

            if (group) {
                avahi_entry_group_free(group);
                group = nullptr;
            }
            if (client)
                avahi_client_free(client);
            avahi_simple_poll_free(poll);
            if (!stopping)
                backoffSleep();
        }
    }
#elif defined(__APPLE__)
    bool registrationFailed = false;

    static void registerCallback(DNSServiceRef, DNSServiceFlags flags, DNSServiceErrorType error, const char *name, const char *,
                                 const char *, void *userdata)
    {
        auto *self = static_cast<NativeDiscovery *>(userdata);
        if (error == kDNSServiceErr_NoError && (flags & kDNSServiceFlagsAdd))
            self->noteRegistered("Bonjour service %s registered on port %d", name, self->port);
        else if (error != kDNSServiceErr_NoError) {
            self->noteFailure("Bonjour service registration failed: %d", error);
            self->registrationFailed = true;
        }
    }

    void run()
    {
        TXTRecordRef txt;
        TXTRecordCreate(&txt, 0, nullptr);
        DNSServiceErrorType error = TXTRecordSetValue(&txt, "shortname", shortName.size(), shortName.data());
        if (error == kDNSServiceErr_NoError)
            error = TXTRecordSetValue(&txt, "id", nodeId.size(), nodeId.data());
        if (error == kDNSServiceErr_NoError)
            error = TXTRecordSetValue(&txt, "pio_env", pioEnv.size(), pioEnv.data());
        if (error != kDNSServiceErr_NoError) {
            noteFailure("Bonjour TXT record setup failed: %d", error);
            TXTRecordDeallocate(&txt);
            return;
        }

        while (!stopping) {
            registrationFailed = false;
            DNSServiceRef service = nullptr;
            error = DNSServiceRegister(&service, 0, 0, "Meshtastic", "_meshtastic._tcp", nullptr, nullptr, htons(port),
                                       TXTRecordGetLength(&txt), TXTRecordGetBytesPtr(&txt), &NativeDiscovery::registerCallback,
                                       this);
            if (error != kDNSServiceErr_NoError)
                noteFailure("Bonjour service setup failed: %d", error);
            if (service) {
                int fd = DNSServiceRefSockFD(service);
                while (!stopping && !registrationFailed && fd >= 0) {
                    pollfd descriptor{fd, POLLIN, 0};
                    int ready = poll(&descriptor, 1, 250);
                    if (ready > 0 && (descriptor.revents & POLLIN)) {
                        error = DNSServiceProcessResult(service);
                        if (error != kDNSServiceErr_NoError)
                            break;
                    } else if (ready > 0 && (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)))
                        break;
                    else if (ready < 0 && errno != EINTR)
                        break;
                }
                DNSServiceRefDeallocate(service);
            }
            if (!stopping)
                backoffSleep();
        }
        TXTRecordDeallocate(&txt);
    }
#endif
};

NativeDiscovery *instance = nullptr;

} // namespace

void startNativeDiscovery(int port)
{
    static NativeDiscovery discovery(port);
    instance = &discovery;
    discovery.start();
}

void serviceNativeDiscovery()
{
    if (instance)
        instance->drainLog();
}

#else

void startNativeDiscovery(int) {}

void serviceNativeDiscovery() {}

#endif
