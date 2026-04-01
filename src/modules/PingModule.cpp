#include "PingModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"

extern graphics::Screen *screen;

PingModule *pingModule;

PingModule::PingModule() : SinglePortModule("ping", meshtastic_PortNum_PING_APP), OSThread("Ping") {}

const char *PingModule::getNodeName(NodeNum node)
{
    meshtastic_NodeInfoLite *info = nodeDB->getMeshNode(node);
    if (info && info->has_user) {
        if (strlen(info->user.short_name) > 0) {
            return info->user.short_name;
        }
        if (strlen(info->user.long_name) > 0) {
            return info->user.long_name;
        }
    }

    static char fallback[12];
    snprintf(fallback, sizeof(fallback), "0x%08x", node);
    return fallback;
}

void PingModule::setResultText(const String &text)
{
    resultText = text;
}

bool PingModule::startPing(NodeNum node)
{
    LOG_INFO("=== Ping startPing CALLED: node=0x%08x ===", node);
    unsigned long now = millis();

    if (node == 0 || node == NODENUM_BROADCAST) {
        LOG_ERROR("Invalid node number for ping: 0x%08x", node);
        runState = PING_STATE_RESULT;
        setResultText("Invalid node");
        resultShowTime = millis();
        pingTarget = 0;

        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        return false;
    }
    if (node == nodeDB->getNodeNum()) {
        LOG_ERROR("Cannot ping self: 0x%08x", node);
        runState = PING_STATE_RESULT;
        setResultText("Cannot ping self");
        resultShowTime = millis();
        pingTarget = 0;

        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        return false;
    }
     if (!initialized) {
        lastPingTime = 0;
        initialized = true;
        LOG_INFO("Ping initialized for first time");
    }

    if (runState == PING_STATE_TRACKING) {
        LOG_INFO("Ping already in progress");
        return false;
    }
    if (initialized && lastPingTime > 0 && now - lastPingTime < cooldownMs) {
        // Cooldown
        unsigned long wait = (cooldownMs - (now - lastPingTime)) / 1000;
        bannerText = String("Wait for ") + String(wait) + String("s");
        runState = PING_STATE_COOLDOWN;
        resultText = "";

        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        LOG_INFO("Cooldown active, please wait %lu seconds before starting a new ping.", wait);
        return false;
    }

    pingTarget = node;
    lastPingTime = now;
    runState = PING_STATE_TRACKING;
    resultText = "";
    bannerText = String("Pinging ") + getNodeName(node);

    LOG_INFO("Ping UI: Starting ping to node 0x%08x, requesting focus", node);

    requestFocus();
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);

    setIntervalFromNow(1000);

    LOG_INFO("Creating Ping packet...");
    meshtastic_MeshPacket *p = allocDataPacket();
    if (p) {
        pingPacketId = p->id; 
        pingSentTime = millis(); 

        p->to = node;
        p->decoded.portnum = meshtastic_PortNum_PING_APP;
        p->decoded.want_response = true;
        p->want_ack = true;
        p->decoded.payload.size = strlen("");
        memcpy(p->decoded.payload.bytes, "", p->decoded.payload.size);
        
        LOG_INFO("Packet allocated successfully: to=0x%08x, portnum=%d, want_response=%d, payload_size=%d", p->to,
                 p->decoded.portnum, p->decoded.want_response, p->decoded.payload.size);
        LOG_INFO("About to call service->sendToMesh...");

        if (service) {
            LOG_INFO("MeshService is available, sending packet...");
            service->sendToMesh(p, RX_SRC_USER);
            LOG_INFO("sendToMesh called successfully for ping to node 0x%08x", node);
        } else {
            LOG_ERROR("MeshService is NULL!");
            runState = PING_STATE_RESULT;
            setResultText("Service unavailable");
            resultShowTime = millis();
            pingTarget = 0;

            requestFocus();
            UIFrameEvent e2;
            e2.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e2);
            return false;
        }

    } else {
        LOG_ERROR("Failed to allocate Ping packet from router");
        runState = PING_STATE_RESULT;
        setResultText("Failed to send.");
        resultShowTime = millis();
        pingTarget = 0;

        requestFocus();
        UIFrameEvent e2;
        e2.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e2);
        return false;
    }
    return true;
}

meshtastic_MeshPacket *PingModule::allocReply()
{
// We are the destination of a ping request; reply with a pong. The framework's
    // setReplyTo() will route it back to the original sender through the mesh.
    assert(currentRequest);
    LOG_INFO("Ping: received request from=0x%08x id=0x%08x, sending pong", currentRequest->from, currentRequest->id);
    LOG_INFO("Creating Ping packet...");
    meshtastic_MeshPacket *p = allocDataPacket();
    if (p) {
        p->to = currentRequest->from;
        p->decoded.portnum = meshtastic_PortNum_PING_APP;
        p->decoded.want_response = false;
        p->want_ack = false;
        p->decoded.payload.size = strlen("");
        memcpy(p->decoded.payload.bytes, "", p->decoded.payload.size);
       
        LOG_INFO("Packet allocated successfully: to=0x%08x, portnum=%d, want_response=%d, payload_size=%d", p->to,
                 p->decoded.portnum, p->decoded.want_response, p->decoded.payload.size);
    } else {
        LOG_ERROR("Failed to allocate Ping packet from router");
        runState = PING_STATE_RESULT;
        setResultText("Failed to send.");
        resultShowTime = millis();
        pingTarget = 0;

        requestFocus();
        UIFrameEvent e2;
        e2.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e2);
    }
    return p;
}

ProcessMessage PingModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // A reply to our ping arrives with request_id set to the id of the packet we sent.
    if (runState == PING_STATE_TRACKING && mp.decoded.request_id == pingPacketId && getFrom(&mp) == pingTarget) {
        unsigned long rtt = mp.rx_time - pingSentTime;
        LOG_INFO("Ping: pong received from=0x%08x rtt=%lums", getFrom(&mp), rtt);

        String r = String("Pong Received: ");
        if (rtt >= 1000) {
            r += String(rtt / 1000) + "." + String((rtt % 1000)) + "s";
        } else {
            r += String(rtt) + "ms";
        }
        r += "| RSSI : " + String(mp.rx_rssi);
        r += "| Hops : " + String(mp.hop_start - mp.hop_limit);
        setResultText(r);
        runState = PING_STATE_RESULT;resultShowTime = millis();

        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        if (screen) {
            screen->forceDisplay();
        }W

        setIntervalFromNow(resultDisplayMs);
    }
    return ProcessMessage::CONTINUE;
}

bool PingModule::shouldDraw()
{
    return runState != PING_STATE_IDLE;
}

#if HAS_SCREEN
void PingModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    LOG_DEBUG("Ping drawFrame called: runState=%d", runState);

    display->setTextAlignment(TEXT_ALIGN_CENTER);

    if (runState == PING_STATE_TRACKING) {
        display->setFont(FONT_MEDIUM);
        int centerY = y + (display->getHeight() / 2) - (FONT_HEIGHT_MEDIUM / 2);
        display->drawString(display->getWidth() / 2 + x, centerY, bannerText);

    } else if (runState == PING_STATE_RESULT) {
        display->setFont(FONT_MEDIUM);
        display->setTextAlignment(TEXT_ALIGN_LEFT);

        display->drawString(x, y, "Ping Result");

        int contentStartY = y + FONT_HEIGHT_MEDIUM + 2; // Add more spacing after title
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(FONT_SMALL);
        display->drawString(x + 2, contentStartY, resultText);

    } else if (runState == PING_STATE_COOLDOWN) {
        display->setFont(FONT_MEDIUM);
        int centerY = y + (display->getHeight() / 2) - (FONT_HEIGHT_MEDIUM / 2);
        display->drawString(display->getWidth() / 2 + x, centerY, bannerText);
    }
}
#endif

int32_t PingModule::runOnce()
{
    if (!initialized) {
        lastPingTime = 0;
        initialized = true;
        runState = PING_STATE_IDLE;
        return INT32_MAX;
    }

    unsigned long now = millis();

    switch(runState) {
        case PING_STATE_IDLE:
            return INT32_MAX; // Sleep indefinitely until startPing() wakes us

        case PING_STATE_TRACKING:
            if (now - lastPingTime > trackingTimeoutMs) {
                LOG_INFO("Ping timeout, no response received");
                runState = PING_STATE_RESULT;
                setResultText("No response received");
                resultShowTime = now;
                pingTarget = 0;

                requestFocus();
                UIFrameEvent e;
                e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                notifyObservers(&e);
                if (screen) screen->forceDisplay();

                return resultDisplayMs; // Wake up later to clear the result
            }
            return 50; // Check for timeout every 50ms

        case PING_STATE_RESULT:
            if (now - resultShowTime >= resultDisplayMs) {
                // Done showing the result, return to IDLE
                LOG_INFO("Clearing ping result from screen");
                runState = PING_STATE_IDLE;
                pingTarget = 0;
                
                UIFrameEvent e;
                e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                notifyObservers(&e);
                return INT32_MAX;
            }
            // Sleep for whatever time is remaining for the result display
            return (resultShowTime + resultDisplayMs) - now; 

        case PING_STATE_COOLDOWN:
            unsigned long elapsed = now - lastPingTime;
            if (elapsed < cooldownMs) {
                unsigned long wait = (cooldownMs - elapsed) / 1000;
                bannerText = String("Wait for ") + String(wait) + String("s");

                requestFocus();
                UIFrameEvent e;
                e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                notifyObservers(&e);
                if (screen) screen->forceDisplay();

                return 1000; // Update countdown every 1 second
            } else {
                LOG_INFO("Ping cooldown finished, returning to IDLE");
                runState = PING_STATE_IDLE;
                resultText = "";
                bannerText = "";
                UIFrameEvent e;
                e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                notifyObservers(&e);
                return INT32_MAX;
            }
    }

    return INT32_MAX;
}
