#pragma once

#include "MeshModule.h"
#include "Router.h"
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

// Is this serial config one we will accept? Outside the architecture guard below because it touches
// no serial hardware, and AdminModule must run it on every platform - including those where
// SerialModule itself does not exist. Logs and notifies the client on rejection.
bool serialConfigIsValid(const meshtastic_ModuleConfig_SerialConfig &config);

#if (defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_RP2040) || defined(ARCH_STM32WL)) &&                             \
    !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)

#if !MESHTASTIC_EXCLUDE_MODBUS
#include "SerialModbus.h"
#endif

class SerialModule : public StreamAPI, private concurrency::OSThread
{
    bool firstTime = 1;
    unsigned long lastNmeaTime = millis();
    char outbuf[90] = "";
    uint32_t telemetryStartAt = 0, telemetryStartDelay = 0;

#if !MESHTASTIC_EXCLUDE_MODBUS
    modbus::Sensor mbSensor = {};
    modbus::Aggregate mbAgg;
    int32_t mbRaw[modbus::REG_SLOTS] = {};
    uint32_t mbSentAt = 0, mbCycleAt = 0, mbCycleMs = 0, mbRainHourAt = 0;
    size_t mbRxLen = 0;
    uint16_t mbReg = 0, mbCount = 0; // request in flight
    uint8_t mbAddr = 0, mbStep = 0;
    bool mbWaiting = false, mbBusy = false;
#ifdef MODBUS_API_ADDR
    modbus::Tunnel mbTunnel{MODBUS_API_ADDR};
    uint32_t mbRxAt = 0, mbTunnelAt = 0;
#endif
#endif

  public:
    SerialModule();

  protected:
    virtual int32_t runOnce() override;

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override;

#if !MESHTASTIC_EXCLUDE_MODBUS && defined(MODBUS_API_ADDR)
    // In MODBUS mode API output is retained for the host's tunnel reads instead of being written to the UART.
    virtual bool writeFrame(uint8_t *buf, size_t len, bool bestEffort) override;
    virtual bool finishPendingFrame() override;
#endif

  private:
    uint32_t getBaudRate();
    void sendTelemetry(meshtastic_Telemetry m);
    bool telemetryDue();
    void processWXSerial();
#if !MESHTASTIC_EXCLUDE_MODBUS
    int32_t runModbus();
    bool modbusNextRequest();
    void modbusPollDone(modbus::Result r);
    void modbusCycleDone();
    void modbusSend(const uint8_t *buf, size_t len);
#ifdef MODBUS_API_ADDR
    void modbusTunnel(size_t len);
#endif
#endif
};

extern SerialModule *serialModule;

/*
 * Radio interface for SerialModule
 *
 */
class SerialModuleRadio : public SinglePortModule
{
    uint32_t lastRxID = 0;
    char outbuf[90] = "";

  public:
    SerialModuleRadio();

    using SinglePortModule::setStartDelay;

    /**
     * Send our payload into the mesh
     */
    void sendPayload(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  protected:
    /** Called to handle a particular incoming message

    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for
    it
    */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};

extern SerialModuleRadio *serialModuleRadio;

#endif
