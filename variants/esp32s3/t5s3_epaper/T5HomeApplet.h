#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

T5 Home: identity, status, messages and recently heard nodes, the mode control and the T5 nav.
One applet, two compositions picked from the live dimensions: Carry (portrait) and Console (landscape).

*/

#pragma once

#include "configuration.h"

#include "./T5Applet.h"

#include "MessageStore.h"
#include "concurrency/OSThread.h"
#include "modules/TextMessageModule.h"

namespace NicheGraphics::InkHUD
{

class T5HomeApplet : public T5Applet, public concurrency::OSThread
{
  public:
    T5HomeApplet();
    static void begin(); // Call after InkHUD::begin(), which loads saved settings over the addApplet() defaults

    void onActivate() override;
    void onDeactivate() override;
    void onRender(bool full) override;
    bool onTouchPoint(uint16_t x, uint16_t y, bool longPress) override;

  private:
    int32_t runOnce() override;

    struct HeardRow {
        std::string shortName, longName, distance, hops;
    };

    // Everything Home shows, read fresh on each render. Strings are already parse()d
    struct Status {
        std::string shortName, nodeId, clock, battery, voltage, region, preset, gps, bt;
        uint16_t nodes = 0, heardCount = 0, direct = 0, oneHop = 0, multiHop = 0, channelUtil = 0;
        std::vector<HeardRow> heard; // Heard in the last 10 minutes, most recent first, at most six

        const StoredMessage *latest = nullptr; // Most recent visible incoming message
        std::string latestKind, latestSender, latestSenderLong, latestClock, latestText;
        std::string channel0Title, channel0Subtitle, dmSubtitle;
    };

    Status readStatus();
    void renderCarry(const Status &s);
    void renderConsole(const Status &s);
    void drawConversation(int16_t left, int16_t top, uint16_t w, const std::string &title, const std::string &subtitle,
                          uint16_t unseen);
    void drawModeControl(int16_t x, int16_t y, uint16_t w, uint16_t h);
    void printClipped(int16_t left, int16_t top, uint16_t w, int16_t bottom, const std::string &text);
    std::string senderName(NodeNum num);
    const StoredMessage *latestIncoming();
    void openLatest();
    void openApplet(const std::string &name);

    int onReceiveTextMessage(const meshtastic_MeshPacket *p);
    CallbackObserver<T5HomeApplet, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<T5HomeApplet, const meshtastic_MeshPacket *>(this, &T5HomeApplet::onReceiveTextMessage);
};

} // namespace NicheGraphics::InkHUD

#endif
