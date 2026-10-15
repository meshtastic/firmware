#include "LinuxPower.h"

#if HAS_HOST_POWEROFF

#include "SdbusCompat.h"
#include <sdbus-c++/sdbus-c++.h>

bool hostPowerOffRequested = false;

static constexpr const char *kLogindService = "org.freedesktop.login1";
static constexpr const char *kLogindPath = "/org/freedesktop/login1";
static constexpr const char *kLogindManager = "org.freedesktop.login1.Manager";

bool linuxPowerOffHost()
{
    try {
        // A private connection rather than the BLE backend's: this runs during shutdown, after
        // that one may already have been torn down, and it is a single synchronous call.
        auto connection = sdbus::createSystemBusConnection();
        auto logind = sdbuscompat::makeProxy(*connection, kLogindService, kLogindPath);

        // interactive=false. The daemon has no session and no way to answer a polkit prompt, so
        // asking for one would just hang the call until it timed out; a flat refusal is what we
        // want to see and log.
        logind->callMethod("PowerOff").onInterface(kLogindManager).withArguments(false);

        LOG_INFO("Host power off requested via logind");
        return true;
    } catch (const sdbus::Error &e) {
        // The expected failure is org.freedesktop.PolicyKit1.Error.NotAuthorized when the polkit
        // rule is missing. Name it, because "permission denied" from a background daemon is
        // otherwise a long afternoon.
        LOG_ERROR("Host power off refused by logind (%s): %s. Is bin/polkit-1/meshtasticd.rules installed?", e.getName().c_str(),
                  e.getMessage().c_str());
        return false;
    } catch (const std::exception &e) {
        LOG_ERROR("Host power off failed: %s", e.what());
        return false;
    }
}

#endif
