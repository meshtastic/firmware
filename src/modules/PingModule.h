#pragma once
#include "ProtobufModule.h"
#include "concurrency/OSThread.h"
#include "graphics/Screen.h"
#include "graphics/SharedUIDisplay.h"
#include "input/InputBroker.h"
#if HAS_SCREEN
#include "OLEDDisplayUi.h"
#endif
#include <vector>

/**
 * A module that traces the route to a certain destination node
 */
enum PingRunState { PING_STATE_IDLE, PING_STATE_TRACKING, PING_STATE_RESULT, PING_STATE_COOLDOWN };

class PingModule : public SinglePortModule, public Observable<const UIFrameEvent *>, private concurrency::OSThread
{
  public:
    PingModule();

    /** Send a ping to the given node. Returns false if blocked by cooldown, invalid target,
     *  or a ping already in flight. */
    bool startPing(NodeNum node);

    bool shouldDraw();
#if HAS_SCREEN
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
#endif

    virtual bool wantUIFrame() override { return shouldDraw(); }
    virtual Observable<const UIFrameEvent *> *getUIFrameObservable() override { return this; }

    /** Change the response timeout (ms). Default 10000. */
    void setTimeout(unsigned long ms) { trackingTimeoutMs = ms; }

  protected:
    /** Called for any packet arriving on PING_APP. Detects pong replies to our outstanding ping. */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    /** When we are the destination of a ping (want_response set), generate the pong reply.
     *  The framework routes it back to the original sender via setReplyTo(). */
    virtual meshtastic_MeshPacket *allocReply() override;

    virtual int32_t runOnce() override;

  private:
    const char *getNodeName(NodeNum node);

    void setResultText(const String &text);
    PingRunState runState = PING_STATE_IDLE;
    NodeNum pingTarget = 0;
    PacketId pingPacketId = 0;        // id of our outstanding request, matched against reply's request_id
    unsigned long pingSentTime = 0;   // millis() when the ping was sent
    unsigned long lastPingTime = 0;   // for cooldown enforcement
    unsigned long resultShowTime = 0;
    unsigned long cooldownMs = 30000;
    unsigned long trackingTimeoutMs = 20000;  // how long to wait for a pong before declaring it lost
    unsigned long resultDisplayMs = 10000;
    
    String bannerText;
    String resultText;
    std::vector<String> resultLines;
    bool resultLinesDirty = false;

    bool initialized = false;
};

extern PingModule *pingModule;