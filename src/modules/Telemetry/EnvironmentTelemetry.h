#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#pragma once

#include "BaseTelemetryModule.h"

#ifndef ENVIRONMENTAL_TELEMETRY_MODULE_ENABLE
#define ENVIRONMENTAL_TELEMETRY_MODULE_ENABLE 0
#endif

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "NodeDB.h"
#include "ProtobufModule.h"
#include "detect/ScanI2CConsumer.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

class EnvironmentTelemetryModule;
extern EnvironmentTelemetryModule *environmentTelemetryModule;

class EnvironmentTelemetryModule : private concurrency::OSThread,
                                   public ScanI2CConsumer,
                                   public BaseTelemetryModule,
                                   public ProtobufModule<meshtastic_Telemetry>
{
    CallbackObserver<EnvironmentTelemetryModule, const meshtastic::Status *> nodeStatusObserver =
        CallbackObserver<EnvironmentTelemetryModule, const meshtastic::Status *>(this,
                                                                                 &EnvironmentTelemetryModule::handleStatusUpdate);

  public:
    enum class DisplaySource : uint8_t {
        LocalSensor = 0,
        Mesh = 1,
        FavoriteNodesOnly = 2,
    };

    EnvironmentTelemetryModule()
        : concurrency::OSThread("EnvironmentTelemetry"), ScanI2CConsumer(),
          ProtobufModule("EnvironmentTelemetry", meshtastic_PortNum_TELEMETRY_APP, &meshtastic_Telemetry_msg)
    {
        environmentTelemetryModule = this;
        (void)getDisplaySource();
        lastMeasurementPacket = nullptr;
        nodeStatusObserver.observe(&nodeStatus->onNewStatus);
        setIntervalFromNow(10 * 1000);
    }
    virtual bool wantUIFrame() override;
    static DisplaySource getDisplaySource();
    static int8_t getSelectedLocalSourceIndex();
    static const char *getDisplaySourceName(DisplaySource source);
    static void setDisplaySource(DisplaySource source, int8_t localSourceIndex = -1);
    const char *getLocalSourceLabel() const;
    const char *getLocalSourceLabel(uint8_t index) const;
    uint8_t getLocalSourceCount() const;
    bool ownsFrame(const MeshModule *module) const { return module == this; }
#if !HAS_SCREEN
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#else
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
#endif

  protected:
    /** Called to handle a particular incoming message
    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *p) override;
    virtual int32_t runOnce() override;
    /** Called to get current Environment telemetry data
    @return true if it contains valid data
    */
    bool getEnvironmentTelemetry(meshtastic_Telemetry *m);
    virtual meshtastic_MeshPacket *allocReply() override;
    /**
     * Send our Telemetry into the mesh
     */
    bool sendTelemetry(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

    virtual AdminMessageHandleResult handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                 meshtastic_AdminMessage *request,
                                                                 meshtastic_AdminMessage *response) override;

    void i2cScanFinished(ScanI2C *i2cScanner);

  private:
    void clearMeasurementPacket();
    bool refreshLocalMeasurementPacket();
    void refreshDisplayedMeasurement();
    bool getEnvironmentTelemetryForLocalSource(meshtastic_Telemetry *m, uint8_t index) const;
    bool shouldDisplayLocalMeasurement() const;
    bool shouldKeepCurrentRemoteDisplay() const;
    bool shouldDisplayRemoteNode(NodeNum nodeNum) const;

    bool firstTime = 1;
    meshtastic_MeshPacket *lastMeasurementPacket;
    uint32_t sendToPhoneIntervalMs = SECONDS_IN_MINUTE * 1000; // Send to phone every minute
    uint32_t lastSentToPhone = 0;
};

#endif
