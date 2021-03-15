#pragma once
#include "ProtobufPlugin.h"
#include "../mesh/generated/environmental_measurement.pb.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>
#include <DHT.h>

class EnvironmentalMeasurementPlugin : private concurrency::OSThread, public ProtobufPlugin<EnvironmentalMeasurement>
{
  public:
    EnvironmentalMeasurementPlugin(): concurrency::OSThread("EnvironmentalMeasurementPlugin"), ProtobufPlugin("EnvironmentalMeasurement", PortNum_ENVIRONMENTAL_MEASUREMENT_APP, &EnvironmentalMeasurement_msg) { 
      lastMeasurementPacket = nullptr;
    }
    virtual bool wantUIFrame();
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

  protected:
    /** Called to handle a particular incoming message
    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const MeshPacket &mp, const EnvironmentalMeasurement *p);
    virtual int32_t runOnce();
    /**
     * Send our EnvironmentalMeasurement into the mesh
     */
    bool sendOurEnvironmentalMeasurement(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);
    
  private:
    float CelsiusToFarenheit(float c);
    bool firstTime = 1;
    DHT* dht;
    const MeshPacket *lastMeasurementPacket;
    uint32_t sensor_read_error_count = 0;
};