#include "TraceRouteModule.h"
#include "MeshService.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "mesh/Router.h"
#include "meshUtils.h"
#include <vector>

extern graphics::Screen *screen;

TraceRouteModule *traceRouteModule;

bool TraceRouteModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_RouteDiscovery *r)
{
    // We only alter the packet in alterReceivedProtobuf()
    return false; // let it be handled by RoutingModule
}

void TraceRouteModule::alterReceivedProtobuf(meshtastic_MeshPacket &p, meshtastic_RouteDiscovery *r)
{
    const meshtastic_Data &incoming = p.decoded;

    // Insert unknown hops if necessary
    insertUnknownHops(p, r, !incoming.request_id);

    // Append ID and SNR. If the last hop is to us, we only need to append the SNR
    appendMyIDandSNR(r, p.rx_snr, !incoming.request_id, isToUs(&p));
    if (!incoming.request_id)
        printRoute(r, p.from, p.to, true);
    else
        printRoute(r, p.to, p.from, false);

    // Set updated route to the payload of the to be flooded packet
    p.decoded.payload.size =
        pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), &meshtastic_RouteDiscovery_msg, r);

    if (tracingNode != 0) {
        // check isResponseFromTarget
        bool isResponseFromTarget = (incoming.request_id != 0 && p.from == tracingNode);
        bool isRequestToUs = (incoming.request_id == 0 && p.to == nodeDB->getNodeNum() && tracingNode != 0);

        // Check if this is a trace route response containing our target node
        bool containsTargetNode = false;
        for (uint8_t i = 0; i < r->route_count; i++) {
            if (r->route[i] == tracingNode) {
                containsTargetNode = true;
                break;
            }
        }
        for (uint8_t i = 0; i < r->route_back_count; i++) {
            if (r->route_back[i] == tracingNode) {
                containsTargetNode = true;
                break;
            }
        }

        // Check if this response contains a complete route to our target
        bool hasCompleteRoute = (r->route_count > 0 && r->route_back_count > 0) ||
                                (containsTargetNode && (r->route_count > 0 || r->route_back_count > 0));

        LOG_INFO("TracRoute packet analysis: tracingNode=0x%08x, p.from=0x%08x, p.to=0x%08x, request_id=0x%08x", tracingNode,
                 p.from, p.to, incoming.request_id);
        LOG_INFO("TracRoute conditions: isResponseFromTarget=%d, isRequestToUs=%d, containsTargetNode=%d, hasCompleteRoute=%d",
                 isResponseFromTarget, isRequestToUs, containsTargetNode, hasCompleteRoute);

        if (isResponseFromTarget || isRequestToUs || (containsTargetNode && hasCompleteRoute)) {
            LOG_INFO("TracRoute result detected: isResponseFromTarget=%d, isRequestToUs=%d", isResponseFromTarget, isRequestToUs);

            LOG_INFO("SNR arrays - towards_count=%d, back_count=%d", r->snr_towards_count, r->snr_back_count);
            for (int i = 0; i < r->snr_towards_count; i++) {
                LOG_INFO("SNR towards[%d] = %d (%.1fdB)", i, r->snr_towards[i], (float)r->snr_towards[i] / 4.0f);
            }
            for (int i = 0; i < r->snr_back_count; i++) {
                LOG_INFO("SNR back[%d] = %d (%.1fdB)", i, r->snr_back[i], (float)r->snr_back[i] / 4.0f);
            }

            String result = "";

            // Show request path (from initiator to target)
            if (r->route_count > 0) {
                result += getNodeName(nodeDB->getNodeNum());
                for (uint8_t i = 0; i < r->route_count; i++) {
                    result += " > ";
                    const char *name = getNodeName(r->route[i]);
                    float snr =
                        (i < r->snr_towards_count && r->snr_towards[i] != INT8_MIN) ? ((float)r->snr_towards[i] / 4.0f) : 0.0f;
                    result += name;
                    if (snr != 0.0f) {
                        result += "(";
                        result += String(snr, 1);
                        result += "dB)";
                    }
                }
                result += " > ";
                result += getNodeName(tracingNode);
                if (r->snr_towards_count > 0 && r->snr_towards[r->snr_towards_count - 1] != INT8_MIN) {
                    result += "(";
                    result += String((float)r->snr_towards[r->snr_towards_count - 1] / 4.0f, 1);
                    result += "dB)";
                }
                result += "\n";
            } else {
                // Direct connection (no intermediate hops)
                result += getNodeName(nodeDB->getNodeNum());
                result += " > ";
                result += getNodeName(tracingNode);
                if (r->snr_towards_count > 0 && r->snr_towards[0] != INT8_MIN) {
                    result += "(";
                    result += String((float)r->snr_towards[0] / 4.0f, 1);
                    result += "dB)";
                }
                result += "\n";
            }

            // Show response path (from target back to initiator)
            if (r->route_back_count > 0) {
                result += getNodeName(tracingNode);
                for (int8_t i = r->route_back_count - 1; i >= 0; i--) {
                    result += " > ";
                    const char *name = getNodeName(r->route_back[i]);
                    float snr = (i < r->snr_back_count && r->snr_back[i] != INT8_MIN) ? ((float)r->snr_back[i] / 4.0f) : 0.0f;
                    result += name;
                    if (snr != 0.0f) {
                        result += "(";
                        result += String(snr, 1);
                        result += "dB)";
                    }
                }
                // add initiator node
                result += " > ";
                result += getNodeName(nodeDB->getNodeNum());
                if (r->snr_back_count > 0 && r->snr_back[r->snr_back_count - 1] != INT8_MIN) {
                    result += "(";
                    result += String((float)r->snr_back[r->snr_back_count - 1] / 4.0f, 1);
                    result += "dB)";
                }
            } else {
                // Direct return path (no intermediate hops)
                result += getNodeName(tracingNode);
                result += " > ";
                result += getNodeName(nodeDB->getNodeNum());
                if (r->snr_back_count > 0 && r->snr_back[0] != INT8_MIN) {
                    result += "(";
                    result += String((float)r->snr_back[0] / 4.0f, 1);
                    result += "dB)";
                }
            }

            LOG_INFO("Trace route result: %s", result.c_str());
            handleTraceRouteResult(result);
        }
    }
}

void TraceRouteModule::insertUnknownHops(meshtastic_MeshPacket &p, meshtastic_RouteDiscovery *r, bool isTowardsDestination)
{
    pb_size_t *route_count;
    uint32_t *route;
    pb_size_t *snr_count;
    int8_t *snr_list;

    // Pick the correct route array and SNR list
    if (isTowardsDestination) {
        route_count = &r->route_count;
        route = r->route;
        snr_count = &r->snr_towards_count;
        snr_list = r->snr_towards;
    } else {
        route_count = &r->route_back_count;
        route = r->route_back;
        snr_count = &r->snr_back_count;
        snr_list = r->snr_back;
    }

    // Only insert unknown hops if hop_start is valid
    if (p.hop_start != 0 && p.hop_limit <= p.hop_start) {
        uint8_t hopsTaken = p.hop_start - p.hop_limit;
        int8_t diff = hopsTaken - *route_count;
        for (uint8_t i = 0; i < diff; i++) {
            if (*route_count < ROUTE_SIZE) {
                route[*route_count] = NODENUM_BROADCAST; // This will represent an unknown hop
                *route_count += 1;
            }
        }
        // Add unknown SNR values if necessary
        diff = *route_count - *snr_count;
        for (uint8_t i = 0; i < diff; i++) {
            if (*snr_count < ROUTE_SIZE) {
                snr_list[*snr_count] = INT8_MIN; // This will represent an unknown SNR
                *snr_count += 1;
            }
        }
    }
}

void TraceRouteModule::appendMyIDandSNR(meshtastic_RouteDiscovery *updated, float snr, bool isTowardsDestination, bool SNRonly)
{
    pb_size_t *route_count;
    uint32_t *route;
    pb_size_t *snr_count;
    int8_t *snr_list;

    // Pick the correct route array and SNR list
    if (isTowardsDestination) {
        route_count = &updated->route_count;
        route = updated->route;
        snr_count = &updated->snr_towards_count;
        snr_list = updated->snr_towards;
    } else {
        route_count = &updated->route_back_count;
        route = updated->route_back;
        snr_count = &updated->snr_back_count;
        snr_list = updated->snr_back;
    }

    if (*snr_count < ROUTE_SIZE) {
        snr_list[*snr_count] = (int8_t)(snr * 4); // Convert SNR to 1 byte
        *snr_count += 1;
    }
    if (SNRonly)
        return;

    // Length of route array can normally not be exceeded due to the max. hop_limit of 7
    if (*route_count < ROUTE_SIZE) {
        route[*route_count] = myNodeInfo.my_node_num;
        *route_count += 1;
    } else {
        LOG_WARN("Route exceeded maximum hop limit!"); // Are you bridging networks?
    }
}

void TraceRouteModule::printRoute(meshtastic_RouteDiscovery *r, uint32_t origin, uint32_t dest, bool isTowardsDestination)
{
#ifdef DEBUG_PORT
    std::string route = "Route traced:\n";
    route += vformat("0x%x --> ", origin);
    for (uint8_t i = 0; i < r->route_count; i++) {
        if (i < r->snr_towards_count && r->snr_towards[i] != INT8_MIN)
            route += vformat("0x%x (%.2fdB) --> ", r->route[i], (float)r->snr_towards[i] / 4);
        else
            route += vformat("0x%x (?dB) --> ", r->route[i]);
    }
    // If we are the destination, or it has already reached the destination, print it
    if (dest == nodeDB->getNodeNum() || !isTowardsDestination) {
        if (r->snr_towards_count > 0 && r->snr_towards[r->snr_towards_count - 1] != INT8_MIN)
            route += vformat("0x%x (%.2fdB)", dest, (float)r->snr_towards[r->snr_towards_count - 1] / 4);

        else
            route += vformat("0x%x (?dB)", dest);
    } else
        route += "...";

    // If there's a route back (or we are the destination as then the route is complete), print it
    if (r->route_back_count > 0 || origin == nodeDB->getNodeNum()) {
        route += "\n";
        if (r->snr_towards_count > 0 && origin == nodeDB->getNodeNum())
            route += vformat("(%.2fdB) 0x%x <-- ", (float)r->snr_back[r->snr_back_count - 1] / 4, origin);
        else
            route += "...";

        for (int8_t i = r->route_back_count - 1; i >= 0; i--) {
            if (i < r->snr_back_count && r->snr_back[i] != INT8_MIN)
                route += vformat("(%.2fdB) 0x%x <-- ", (float)r->snr_back[i] / 4, r->route_back[i]);
            else
                route += vformat("(?dB) 0x%x <-- ", r->route_back[i]);
        }
        route += vformat("0x%x", dest);
    }
    LOG_INFO(route.c_str());
#endif
}

meshtastic_MeshPacket *TraceRouteModule::allocReply()
{
    assert(currentRequest);

    // Ignore multi-hop broadcast requests
    if (isBroadcast(currentRequest->to) && currentRequest->hop_limit < currentRequest->hop_start) {
        ignoreRequest = true;
        return NULL;
    }

    // Copy the payload of the current request
    auto req = *currentRequest;
    const auto &p = req.decoded;
    meshtastic_RouteDiscovery scratch;
    meshtastic_RouteDiscovery *updated = NULL;
    memset(&scratch, 0, sizeof(scratch));
    pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_RouteDiscovery_msg, &scratch);
    updated = &scratch;

    // Create a MeshPacket with this payload and set it as the reply
    meshtastic_MeshPacket *reply = allocDataProtobuf(*updated);

    return reply;
}

TraceRouteModule::TraceRouteModule()
    : ProtobufModule("traceroute", meshtastic_PortNum_TRACEROUTE_APP, &meshtastic_RouteDiscovery_msg), OSThread("TraceRoute")
{
    ourPortNum = meshtastic_PortNum_TRACEROUTE_APP;
    isPromiscuous = true; // We need to update the route even if it is not destined to us
}

const char *TraceRouteModule::getNodeName(NodeNum node)
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

bool TraceRouteModule::startTraceRoute(NodeNum node)
{
    LOG_INFO("=== TraceRoute startTraceRoute CALLED: node=0x%08x ===", node);
    unsigned long now = millis();

    if (node == 0 || node == NODENUM_BROADCAST) {
        LOG_ERROR("Invalid node number for trace route: 0x%08x", node);
        runState = TRACEROUTE_STATE_RESULT;
        resultText = "Invalid node";
        resultShowTime = millis();
        tracingNode = 0;

        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        return false;
    }

    if (node == nodeDB->getNodeNum()) {
        LOG_ERROR("Cannot trace route to self: 0x%08x", node);
        runState = TRACEROUTE_STATE_RESULT;
        resultText = "Cannot trace self";
        resultShowTime = millis();
        tracingNode = 0;

        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        return false;
    }

    if (!initialized) {
        lastTraceRouteTime = 0;
        initialized = true;
        LOG_INFO("TraceRoute initialized for first time");
    }

    if (runState == TRACEROUTE_STATE_TRACKING) {
        LOG_INFO("TraceRoute already in progress");
        return false;
    }

    if (initialized && lastTraceRouteTime > 0 && now - lastTraceRouteTime < cooldownMs) {
        // Cooldown
        unsigned long wait = (cooldownMs - (now - lastTraceRouteTime)) / 1000;
        bannerText = String("Wait for ") + String(wait) + String("s");
        runState = TRACEROUTE_STATE_COOLDOWN;

        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        LOG_INFO("Cooldown active, please wait %lu seconds before starting a new trace route.", wait);
        return false;
    }

    tracingNode = node;
    lastTraceRouteTime = now;
    runState = TRACEROUTE_STATE_TRACKING;
    bannerText = String("Tracing ") + getNodeName(node);

    LOG_INFO("TraceRoute UI: Starting trace route to node 0x%08x, requesting focus", node);

    // 请求焦点，然后触发UI更新事件
    requestFocus();
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);

    // 设置定时器来处理超时检查
    setIntervalFromNow(1000); // 每秒检查一次状态

    meshtastic_RouteDiscovery req = meshtastic_RouteDiscovery_init_zero;
    LOG_INFO("Creating RouteDiscovery protobuf...");

    // Allocate a packet directly from router like the reference code
    meshtastic_MeshPacket *p = router->allocForSending();
    if (p) {
        // Set destination and port
        p->to = node;
        p->decoded.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        p->decoded.want_response = true;

        // Manually encode the RouteDiscovery payload
        p->decoded.payload.size =
            pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_RouteDiscovery_msg, &req);

        LOG_INFO("Packet allocated successfully: to=0x%08x, portnum=%d, want_response=%d, payload_size=%d", p->to,
                 p->decoded.portnum, p->decoded.want_response, p->decoded.payload.size);
        LOG_INFO("About to call service->sendToMesh...");

        if (service) {
            LOG_INFO("MeshService is available, sending packet...");
            service->sendToMesh(p, RX_SRC_USER);
            LOG_INFO("sendToMesh called successfully for trace route to node 0x%08x", node);
        } else {
            LOG_ERROR("MeshService is NULL!");
            runState = TRACEROUTE_STATE_RESULT;
            resultText = "Service unavailable";
            resultShowTime = millis();
            tracingNode = 0;

            requestFocus();
            UIFrameEvent e2;
            e2.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e2);
            return false;
        }
    } else {
        LOG_ERROR("Failed to allocate TraceRoute packet from router");
        runState = TRACEROUTE_STATE_RESULT;
        resultText = "Failed to send";
        resultShowTime = millis();
        tracingNode = 0;

        requestFocus();
        UIFrameEvent e2;
        e2.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e2);
        return false;
    }
    return true;
}

void TraceRouteModule::launch(NodeNum node)
{
    if (node == 0 || node == NODENUM_BROADCAST) {
        LOG_ERROR("Invalid node number for trace route: 0x%08x", node);
        runState = TRACEROUTE_STATE_RESULT;
        resultText = "Invalid node";
        resultShowTime = millis();
        tracingNode = 0;

        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        return;
    }

    if (node == nodeDB->getNodeNum()) {
        LOG_ERROR("Cannot trace route to self: 0x%08x", node);
        runState = TRACEROUTE_STATE_RESULT;
        resultText = "Cannot trace self";
        resultShowTime = millis();
        tracingNode = 0;

        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        return;
    }

    if (!initialized) {
        lastTraceRouteTime = 0;
        initialized = true;
        LOG_INFO("TraceRoute initialized for first time");
    }

    unsigned long now = millis();
    if (initialized && lastTraceRouteTime > 0 && now - lastTraceRouteTime < cooldownMs) {
        unsigned long wait = (cooldownMs - (now - lastTraceRouteTime)) / 1000;
        bannerText = String("Wait for ") + String(wait) + String("s");
        runState = TRACEROUTE_STATE_COOLDOWN;

        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        LOG_INFO("Cooldown active, please wait %lu seconds before starting a new trace route.", wait);
        return;
    }

    runState = TRACEROUTE_STATE_TRACKING;
    tracingNode = node;
    lastTraceRouteTime = now;
    bannerText = String("Tracing ") + getNodeName(node);

    requestFocus();

    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);

    setIntervalFromNow(1000);

    meshtastic_RouteDiscovery req = meshtastic_RouteDiscovery_init_zero;
    LOG_INFO("Creating RouteDiscovery protobuf...");

    meshtastic_MeshPacket *p = router->allocForSending();
    if (p) {
        p->to = node;
        p->decoded.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        p->decoded.want_response = true;

        p->decoded.payload.size =
            pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_RouteDiscovery_msg, &req);

        LOG_INFO("Packet allocated successfully: to=0x%08x, portnum=%d, want_response=%d, payload_size=%d", p->to,
                 p->decoded.portnum, p->decoded.want_response, p->decoded.payload.size);

        if (service) {
            service->sendToMesh(p, RX_SRC_USER);
            LOG_INFO("sendToMesh called successfully for trace route to node 0x%08x", node);
        } else {
            LOG_ERROR("MeshService is NULL!");
            runState = TRACEROUTE_STATE_RESULT;
            resultText = "Service unavailable";
            resultShowTime = millis();
            tracingNode = 0;
        }
    } else {
        LOG_ERROR("Failed to allocate TraceRoute packet from router");
        runState = TRACEROUTE_STATE_RESULT;
        resultText = "Failed to send";
        resultShowTime = millis();
        tracingNode = 0;
    }
}

void TraceRouteModule::handleTraceRouteResult(const String &result)
{
    resultText = result;
    runState = TRACEROUTE_STATE_RESULT;
    resultShowTime = millis();
    tracingNode = 0;

    LOG_INFO("TraceRoute result ready, requesting focus. Result: %s", result.c_str());

    setIntervalFromNow(1000);

    requestFocus();
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);

    LOG_INFO("=== TraceRoute handleTraceRouteResult END ===");
}

bool TraceRouteModule::shouldDraw()
{
    bool draw = (runState != TRACEROUTE_STATE_IDLE);
    static TraceRouteRunState lastLoggedState = TRACEROUTE_STATE_IDLE;
    if (runState != lastLoggedState) {
        LOG_INFO("TraceRoute shouldDraw: runState=%d, draw=%d", runState, draw);
        lastLoggedState = runState;
    }
    return draw;
}
#if HAS_SCREEN
void TraceRouteModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    LOG_DEBUG("TraceRoute drawFrame called: runState=%d", runState);

    display->setTextAlignment(TEXT_ALIGN_CENTER);

    if (runState == TRACEROUTE_STATE_TRACKING) {
        display->setFont(FONT_MEDIUM);
        int centerY = y + (display->getHeight() / 2) - (FONT_HEIGHT_MEDIUM / 2);
        display->drawString(display->getWidth() / 2 + x, centerY, bannerText);

    } else if (runState == TRACEROUTE_STATE_RESULT) {
        display->setFont(FONT_MEDIUM);
        display->setTextAlignment(TEXT_ALIGN_LEFT);

        display->drawString(x, y, "Route Result");

        int contentStartY = y + FONT_HEIGHT_MEDIUM + 2; // Add more spacing after title
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(FONT_SMALL);

        if (resultText.length() > 0) {
            std::vector<String> lines;
            String currentLine = "";
            int maxWidth = display->getWidth() - 4;

            int start = 0;
            int newlinePos = resultText.indexOf('\n', start);

            while (newlinePos != -1 || start < resultText.length()) {
                String segment;
                if (newlinePos != -1) {
                    segment = resultText.substring(start, newlinePos);
                    start = newlinePos + 1;
                    newlinePos = resultText.indexOf('\n', start);
                } else {
                    segment = resultText.substring(start);
                    start = resultText.length();
                }

                if (display->getStringWidth(segment) <= maxWidth) {
                    lines.push_back(segment);
                } else {
                    // Try to break at better positions (space, >, <, -)
                    String remaining = segment;

                    while (remaining.length() > 0) {
                        String tempLine = "";
                        int lastGoodBreak = -1;
                        bool lineComplete = false;

                        for (int i = 0; i < remaining.length(); i++) {
                            char ch = remaining.charAt(i);
                            String testLine = tempLine + ch;

                            if (display->getStringWidth(testLine) > maxWidth) {
                                if (lastGoodBreak >= 0) {
                                    // Break at the last good position
                                    lines.push_back(remaining.substring(0, lastGoodBreak + 1));
                                    remaining = remaining.substring(lastGoodBreak + 1);
                                    lineComplete = true;
                                    break;
                                } else if (tempLine.length() > 0) {
                                    lines.push_back(tempLine);
                                    remaining = remaining.substring(i);
                                    lineComplete = true;
                                    break;
                                } else {
                                    // Single character exceeds width
                                    lines.push_back(String(ch));
                                    remaining = remaining.substring(i + 1);
                                    lineComplete = true;
                                    break;
                                }
                            } else {
                                tempLine = testLine;
                                // Mark good break positions
                                if (ch == ' ' || ch == '>' || ch == '<' || ch == '-' || ch == '(' || ch == ')') {
                                    lastGoodBreak = i;
                                }
                            }
                        }

                        if (!lineComplete) {
                            // Reached end of remaining text
                            if (tempLine.length() > 0) {
                                lines.push_back(tempLine);
                            }
                            break;
                        }
                    }
                }
            }

            int lineHeight = FONT_HEIGHT_SMALL + 1; // Use proper font height with 1px spacing
            for (size_t i = 0; i < lines.size(); i++) {
                int lineY = contentStartY + (i * lineHeight);
                if (lineY + FONT_HEIGHT_SMALL <= display->getHeight()) {
                    display->drawString(x + 2, lineY, lines[i]);
                }
            }
        }

    } else if (runState == TRACEROUTE_STATE_COOLDOWN) {
        display->setFont(FONT_MEDIUM);
        int centerY = y + (display->getHeight() / 2) - (FONT_HEIGHT_MEDIUM / 2);
        display->drawString(display->getWidth() / 2 + x, centerY, bannerText);
    }
}
#endif // HAS_SCREEN
int32_t TraceRouteModule::runOnce()
{
    unsigned long now = millis();

    if (runState == TRACEROUTE_STATE_IDLE) {
        return INT32_MAX;
    }

    // Check for tracking timeout
    if (runState == TRACEROUTE_STATE_TRACKING && now - lastTraceRouteTime > trackingTimeoutMs) {
        LOG_INFO("TraceRoute timeout, no response received");
        runState = TRACEROUTE_STATE_RESULT;
        resultText = "No response received";
        resultShowTime = now;
        tracingNode = 0;

        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);

        setIntervalFromNow(resultDisplayMs);
        return resultDisplayMs;
    }

    // Update cooldown display every second
    if (runState == TRACEROUTE_STATE_COOLDOWN) {
        unsigned long wait = (cooldownMs - (now - lastTraceRouteTime)) / 1000;
        if (wait > 0) {
            String newBannerText = String("Wait for ") + String(wait) + String("s");
            bannerText = newBannerText;
            LOG_INFO("TraceRoute cooldown: updating banner to %s", bannerText.c_str());

            // Force flash UI
            requestFocus();
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);

            if (screen) {
                screen->forceDisplay();
            }

            return 1000;
        } else {
            // Cooldown finished
            LOG_INFO("TraceRoute cooldown finished, returning to IDLE");
            runState = TRACEROUTE_STATE_IDLE;
            bannerText = "";
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
            return INT32_MAX;
        }
    }

    if (runState == TRACEROUTE_STATE_RESULT) {
        if (now - resultShowTime >= resultDisplayMs) {
            LOG_INFO("TraceRoute result display timeout, returning to IDLE");
            runState = TRACEROUTE_STATE_IDLE;
            resultText = "";
            bannerText = "";
            tracingNode = 0;
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
            return INT32_MAX;
        } else {
            return 1000;
        }
    }

    if (runState == TRACEROUTE_STATE_TRACKING) {
        return 1000;
    }

    return INT32_MAX;
}