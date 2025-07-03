#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Displays a thread-view of incoming and outgoing message for a specific channel

The channel for this applet is set in the constructor,
when the applet is added to WindowManager in the setupNicheGraphics method.

Several messages are saved to flash at shutdown, to preseve applet between reboots.
This class has its own internal method for saving and loading to fs, which interacts directly with the FSCommon layer.
If the amount of flash usage is unacceptable, we could keep these in RAM only.

Multiple instances of this channel may be used. This must be done at buildtime.
Suggest a max of two channel, to minimize fs usage?

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"
#include "graphics/niche/InkHUD/MessageStore.h"

#include "modules/TextMessageModule.h"

namespace NicheGraphics::InkHUD
{

class Applet;

class ThreadedMessageApplet : public Applet, public SinglePortModule
{
  public:
    explicit ThreadedMessageApplet(uint8_t channelIndex);
    ThreadedMessageApplet() = delete;

    void onRender() override;

    void onActivate() override;
    void onDeactivate() override;
    void onShutdown() override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    bool approveNotification(Notification &n) override; // Which notifications to suppress

  protected:
    void saveMessagesToFlash();
    void loadMessagesFromFlash();

    MessageStore *store; // Messages, held in RAM for use, ready to save to flash on shutdown
    uint8_t channelIndex = 0;
};

} // namespace NicheGraphics::InkHUD

#endif