#pragma once
#include "ProtobufPlugin.h"
#include "../mesh/generated/environmental_measurement.pb.h"


class EnvironmentalMeasurementPlugin : private concurrency::OSThread
{
    bool firstTime = 1;

  public:
    EnvironmentalMeasurementPlugin();

  protected:
    virtual int32_t runOnce();
};

extern EnvironmentalMeasurementPlugin *environmentalMeasurementPlugin;

/**
 * EnvironmentalMeasurementPluginRadio plugin for sending/receiving environmental measurements to/from the mesh
 */
class EnvironmentalMeasurementPluginRadio : public ProtobufPlugin<EnvironmentalMeasurement>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    EnvironmentalMeasurementPluginRadio() : ProtobufPlugin("EnvironmentalMeasurement", PortNum_ENVIRONMENTAL_MEASUREMENT_APP, &EnvironmentalMeasurement_msg) {}

    /**
     * Send our EnvironmentalMeasurement into the mesh
     */
    bool sendOurEnvironmentalMeasurement(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  protected:
    
    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const MeshPacket &mp, const EnvironmentalMeasurement *p);

};

extern EnvironmentalMeasurementPluginRadio *environmentalMeasurementPluginRadio;