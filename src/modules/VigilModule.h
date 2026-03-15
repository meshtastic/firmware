#pragma once

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

/*
 * VigilModule — Acoustic C-UAS detection module
 *
 * Architecture: thin event bus dispatcher (<100 lines of logic).
 * Subsystems register for events; this module dispatches.
 *
 *   EVENTS:
 *   ┌──────────────────────┬───────────────────────────────┐
 *   │ AUDIO_FRAME_READY    │ DSP pipeline consumes audio   │
 *   │ DETECTION            │ Mesh broadcast + alarm        │
 *   │ VECTOR_RECEIVED      │ Triangulation ingests vector  │
 *   │ HEARTBEAT_TIMER      │ Health payload assembled      │
 *   │ CALIBRATION_TRIGGER  │ Chirp scheduling begins       │
 *   └──────────────────────┴───────────────────────────────┘
 *
 * Port: PRIVATE_APP (256) — Vigil-specific messages only.
 * Core: DSP tasks pinned to Core 0, mesh tasks on Core 1.
 */

// Vigil uses a private app port (256-511 range)
#define VIGIL_PORTNUM ((meshtastic_PortNum)256)

// Event types dispatched by the event bus
enum class VigilEvent : uint8_t {
    AUDIO_FRAME_READY,
    DETECTION,
    VECTOR_RECEIVED,
    HEARTBEAT_TIMER,
    CALIBRATION_TRIGGER,
};

// Forward declarations for subsystems
namespace vigil {
class DspPipeline;
class ClusterTracker;
class Heading;
class Triangulation;
class PriorityQueue;
class Leader;
class Heartbeat;
class Calibration;
class WakeMonitor;
} // namespace vigil

class VigilModule : public SinglePortModule, private concurrency::OSThread {
  public:
    VigilModule();

    // Module lifecycle
    virtual void setup() override;

  protected:
    // Called periodically by OSThread
    virtual int32_t runOnce() override;

    // Called when a Vigil packet arrives from the mesh
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  private:
    bool initialized = false;
    uint32_t lastHeartbeat = 0;

    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 15 * 60 * 1000; // 15 minutes
    static constexpr uint32_t DSP_POLL_INTERVAL_MS = 100;             // 100ms when idle

    void handleDetection();
    void handleHeartbeat();
    void dispatchMeshMessage(const meshtastic_MeshPacket &mp);
};

extern VigilModule *vigilModule;
