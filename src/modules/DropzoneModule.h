#pragma once
#include "SinglePortModule.h"
#include "telemetry/Sensor/DFRobotLarkSensor.h"

/**
 * A simple example module that just replies with "Message received" to any message it receives.
 */
class DropzoneModule : public SinglePortModule
{
  DFRobotLarkSensor sensor;

public:
  /** Constructor
   * name is for debugging output
   */
  DropzoneModule() : SinglePortModule("dropzone", meshtastic_PortNum_TEXT_MESSAGE_APP)
  {
  }

protected:
  /** Called to handle a particular incoming message
   */
  virtual void alterReceived(meshtastic_MeshPacket &mp);

  /** Called to handle a particular incoming message
   */
  virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  /** For reply module we do all of our processing in the (normally optional)
   * want_replies handling
   */
  virtual meshtastic_MeshPacket *allocReply() override;

private:
  meshtastic_MeshPacket *sendConditions();
};
