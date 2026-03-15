#include "VigilModule.h"
#include "MeshService.h"
#include "Router.h"
#include "configuration.h"

VigilModule *vigilModule = nullptr;

VigilModule::VigilModule()
    : SinglePortModule("vigil", VIGIL_PORTNUM), concurrency::OSThread("Vigil")
{
    isPromiscuous = false;
    loopbackOk = false;
}

void VigilModule::setup()
{
    // TODO Phase A: Initialize I2S TDM driver (Core 0 pinned)
    // TODO Phase A: Initialize DSP pipeline (HPF + FFT + SRP-PHAT)
    // TODO Phase B: Initialize magnetometer (QMC5883L via I2C)
    // TODO Phase B: Initialize heading state machine
    // TODO Phase D: Initialize wake-on-sound monitor
    // TODO Phase D: Initialize LED status indicator

    LOG_INFO("Vigil acoustic C-UAS module initialized\n");
    initialized = true;
}

int32_t VigilModule::runOnce()
{
    if (!initialized) {
        setup();
        return setStartDelay();
    }

    // --- Heartbeat timer ---
    uint32_t now = millis();
    if (now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
        handleHeartbeat();
        lastHeartbeat = now;
    }

    // TODO Phase A: Check wake monitor for audio events
    // TODO Phase A: If audio ready → run DSP pipeline → check for detections
    // TODO Phase B: If detection → apply heading offset → broadcast DoA on CH1
    // TODO Phase C: If neighbor vectors received → run triangulation
    // TODO Phase C: If triangulation converges → leader check → broadcast on CH2
    // TODO Phase D: If calibration scheduled → emit/receive chirps

    return DSP_POLL_INTERVAL_MS;
}

ProcessMessage VigilModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    dispatchMeshMessage(mp);
    return ProcessMessage::CONTINUE;
}

void VigilModule::dispatchMeshMessage(const meshtastic_MeshPacket &mp)
{
    // Decode the payload to determine message type
    // TODO Phase C: Parse vigil protobuf, dispatch to:
    //   - DoAVector    → triangulation.addVector()
    //   - DroneAlert   → leader.checkSuppression()
    //   - Heartbeat    → (log neighbor health)
    //   - CalAnnounce  → calibration.scheduleReceive()

    LOG_DEBUG("Vigil: received mesh packet from 0x%08x\n", mp.from);
}

void VigilModule::handleDetection()
{
    // TODO Phase B: Build DoAVector protobuf with:
    //   - node_id, lat, lon
    //   - azimuth, elevation (globally aligned via heading offset)
    //   - cluster_id (from multi-peak extraction)
    //   - confidence, signature_hash
    //   - timestamp_gps

    // TODO Phase C: Enqueue on priority queue (CH1)
    // TODO Phase D: Trigger piezo alarm
    // TODO Phase D: Set LED to red
}

void VigilModule::handleHeartbeat()
{
    // TODO Phase D: Assemble heartbeat payload:
    //   - battery_mv, solar_mv (ADC reads)
    //   - gps_fix, gps_sats, gps_hdop
    //   - position_locked
    //   - heading_source (MAG | ACOUSTIC), heading_cal_age
    //   - mic_health (16-bit bitmask)
    //   - wake_count_15m, detect_count_15m
    //   - noise_floor_dba
    //   - crash_counter (from RTC memory)
    //   - fw_version, uptime_hours

    LOG_DEBUG("Vigil: heartbeat tick\n");
}
