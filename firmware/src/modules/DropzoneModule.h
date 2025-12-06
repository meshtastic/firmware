#pragma once
#if !MESHTASTIC_EXCLUDE_DROPZONE
#include "SinglePortModule.h"
#include "modules/Telemetry/Sensor/DFRobotLarkSensor.h"

/**
 * An example module that replies to a message with the current conditions
 * and status at the dropzone when it receives a text message mentioning it's name followed by "conditions"
 */
class DropzoneModule : public SinglePortModule, private concurrency::OSThread
{
    DFRobotLarkSensor sensor;

  public:
    /** Constructor
     * name is for debugging output
     */
    DropzoneModule() : SinglePortModule("dropzone", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("Dropzone")
    {
        // Set up the analog pin for reading the dropzone status
        pinMode(PIN_A1, INPUT);
    }

    virtual int32_t runOnce() override;

  protected:
    /** Called to handle a particular incoming message
     */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  private:
    meshtastic_MeshPacket *sendConditions();
    uint32_t startSendConditions = 0;
};

extern DropzoneModule *dropzoneModule;
#endif