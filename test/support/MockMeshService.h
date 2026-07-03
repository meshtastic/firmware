#pragma once
// Release-to-pool MeshService stub shared by the native suites: handlers that raise client
// notifications must not leak them across thousands of iterations (LSan turns the run RED).
#include "mesh/MeshService.h"

class MockMeshService : public MeshService
{
  public:
    void sendClientNotification(meshtastic_ClientNotification *n) override { releaseClientNotificationToPool(n); }
};
