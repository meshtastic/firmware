#pragma once
// Release-to-pool MeshService stub shared by the native suites: handlers that raise client
// notifications must not leak them across thousands of iterations (LSan turns the run RED).
#include "mesh/MeshService.h"
#include <string.h>

class MockMeshService : public MeshService
{
  public:
    void sendClientNotification(meshtastic_ClientNotification *n) override
    {
        notificationCount++;
        // Kept so a suite can assert what the client was actually told, not just that it was told.
        strncpy(lastNotification, n->message, sizeof(lastNotification) - 1);
        lastNotification[sizeof(lastNotification) - 1] = '\0';
        releaseClientNotificationToPool(n);
    }

    uint32_t notificationCount = 0;
    char lastNotification[250] = {0};
};
