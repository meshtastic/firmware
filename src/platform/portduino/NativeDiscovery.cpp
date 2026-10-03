#include "NativeDiscovery.h"

#if (defined(__linux__) && defined(MESHTASTIC_USE_AVAHI)) || defined(__APPLE__)

#include "DebugConfiguration.h"
#include "NodeDB.h"
#include "configuration.h"
#include "main.h"
#include "meshUtils.h"

#include <atomic>
#include <cerrno>
#include <chrono>
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
#include <dns_sd.h>
#include <poll.h>
#endif

namespace
{

class NativeDiscovery
{
  public:
    NativeDiscovery(int port) : port(port), shortName(owner.short_name), nodeId(nodeDB->getNodeId()), pioEnv(optstr(APP_ENV))
    {
        worker = std::thread(&NativeDiscovery::run, this);
    }

    ~NativeDiscovery()
    {
        stopping = true;
        worker.join();
    }

  private:
    const int port;
    const std::string shortName;
    const std::string nodeId;
    const std::string pioEnv;
    std::atomic<bool> stopping{false};
    std::thread worker;

    void retryDelay()
    {
        for (int i = 0; i < 5 && !stopping; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
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
            LOG_WARN("Avahi entry group failed: %s", avahi_strerror(avahi_client_errno(client)));
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
            LOG_WARN("Avahi service registration failed: %s", avahi_strerror(result));
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
            LOG_INFO("Avahi service %s registered on port %d", self->serviceName.c_str(), self->port);
        else if (state == AVAHI_ENTRY_GROUP_COLLISION) {
            self->renameService();
            if (!self->reconnect) {
                avahi_entry_group_reset(entryGroup);
                self->publish(avahi_entry_group_get_client(entryGroup));
            }
        } else if (state == AVAHI_ENTRY_GROUP_FAILURE) {
            LOG_WARN("Avahi service failed: %s", avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(entryGroup))));
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
            LOG_WARN("Avahi client failed: %s", avahi_strerror(avahi_client_errno(client)));
            self->reconnect = true;
        }
    }

    void run()
    {
        while (!stopping) {
            reconnect = false;
            AvahiSimplePoll *poll = avahi_simple_poll_new();
            if (!poll) {
                LOG_WARN("Avahi poll setup failed");
                retryDelay();
                continue;
            }

            int error = 0;
            AvahiClient *client = avahi_client_new(avahi_simple_poll_get(poll), AVAHI_CLIENT_NO_FAIL,
                                                   &NativeDiscovery::clientCallback, this, &error);
            if (!client)
                LOG_WARN("Avahi client setup failed: %s", avahi_strerror(error));
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
                retryDelay();
        }
    }
#elif defined(__APPLE__)
    bool registrationFailed = false;

    static void registerCallback(DNSServiceRef, DNSServiceFlags flags, DNSServiceErrorType error, const char *name, const char *,
                                 const char *, void *userdata)
    {
        auto *self = static_cast<NativeDiscovery *>(userdata);
        if (error == kDNSServiceErr_NoError && (flags & kDNSServiceFlagsAdd))
            LOG_INFO("Bonjour service %s registered on port %d", name, self->port);
        else if (error != kDNSServiceErr_NoError) {
            LOG_WARN("Bonjour service registration failed: %d", error);
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
            LOG_WARN("Bonjour TXT record setup failed: %d", error);
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
                LOG_WARN("Bonjour service setup failed: %d", error);
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
                retryDelay();
        }
        TXTRecordDeallocate(&txt);
    }
#endif
};

} // namespace

void startNativeDiscovery(int port)
{
    static NativeDiscovery discovery(port);
}

#else

void startNativeDiscovery(int) {}

#endif
