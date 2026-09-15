#if defined(T5_S3_EPAPER_PRO) && defined(MESHTASTIC_INCLUDE_INKHUD)

#include "./T5HomeApplet.h"

#include "BluetoothStatus.h"
#include "DisplayFormatters.h"
#include "GPSStatus.h"
#include "PowerStatus.h"
#include "UptimeClock.h"
#include "airtime.h"
#include "gps/GeoCoord.h"
#include "gps/RTC.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include "mesh/Channels.h"
#include "mesh/MeshRadio.h"
#include "mesh/NodeDB.h"

#include <algorithm>

using namespace NicheGraphics;

namespace
{
constexpr int16_t MARGIN = 16;

// Session-local NEW counts: zero at boot, so conversations that already exist start as seen
uint16_t newDMs = 0;
uint16_t newChannel0 = 0;

bool isOnScreen(const char *appletName)
{
    const int8_t i = InkHUD::T5Applet::indexOf(appletName);
    return i >= 0 && InkHUD::InkHUD::getInstance()->userApplets[i]->isForeground();
}

// Draws nothing, but renders with every display update: a conversation is seen once it is actually on screen,
// however it was opened. Renderer skips it while another system applet locks rendering, when nothing beneath shows.
class SeenTracker : public InkHUD::SystemApplet
{
  public:
    SeenTracker() { alwaysRender = true; }

    void onRender(bool full) override
    {
        (void)full;
        if (isOnScreen("DMs"))
            newDMs = 0;
        if (isOnScreen("Channel 0"))
            newChannel0 = 0;
    }
};

// Seconds since a stored message, or -1 if its timestamp can't be compared with now
int32_t ageSecs(const StoredMessage &m)
{
    const uint32_t now = m.isBootRelative ? Time::getUptimeSecs() : getValidTime(RTCQuality::RTCQualityDevice, true);
    return (now && now >= m.timestamp) ? now - m.timestamp : -1;
}
} // namespace

void InkHUD::T5HomeApplet::begin()
{
    InkHUD *hud = InkHUD::getInstance();
    Persistence::Settings &saved = hud->persistence->settings;

    // T5 applets are appended after the shared applets, so their saved indices are untouched.
    // Settings saved before a T5 applet existed have it inactive; all must be available, and Home shown at boot.
    for (const char *name : {"Home", "Nodes", "Node Detail"}) {
        const int8_t i = indexOf(name);
        assert(i >= 0);
        saved.userApplets.active[i] = true;
        if (!hud->userApplets[i]->isActive())
            hud->userApplets[i]->activate();
    }
    const int8_t index = indexOf("Home");
    if (!hud->userApplets[index]->isForeground()) {
        saved.userTiles.focused = 0;
        hud->showApplet(index);
    }

    SeenTracker *tracker = new SeenTracker;
    tracker->name = "T5SeenTracker";
    (new Tile)->assignApplet(tracker); // Zero-size tile: Renderer never clears any pixels for it
    tracker->activate();
    tracker->bringToForeground();
    hud->systemApplets.push_back(tracker);
}

InkHUD::T5HomeApplet::T5HomeApplet() : concurrency::OSThread("T5HomeApplet")
{
    OSThread::disable();
}

void InkHUD::T5HomeApplet::onActivate()
{
    textMessageObserver.observe(textMessageModule);
    OSThread::enabled = true;
    OSThread::setIntervalFromNow(60 * 1000UL);
}

void InkHUD::T5HomeApplet::onDeactivate()
{
    textMessageObserver.unobserve(textMessageModule);
    OSThread::disable();
}

// While shown, redraw on each minute boundary: the finest step of anything Home shows (clock, ages, 60 s channel load).
// Skipped under a notification banner, which our redraw would erase while it still takes input; dismissing it redraws all
int32_t InkHUD::T5HomeApplet::runOnce()
{
    if (isForeground() && !inkhud->getSystemApplet("Notification")->isForeground())
        requestUpdate(); // UNSPECIFIED: DisplayHealth folds the FULL refreshes the panel owes into these
    return (60 - getValidTime(RTCQuality::RTCQualityDevice, true) % 60) * 1000UL;
}

// Only messages received after boot can be NEW
int InkHUD::T5HomeApplet::onReceiveTextMessage(const meshtastic_MeshPacket *p)
{
    if (isFromUs(p))
        return 0;

    if (!isBroadcast(p->to))
        newDMs++;
    else if (p->channel == 0)
        newChannel0++;

    requestUpdate();
    return 0;
}

std::string InkHUD::T5HomeApplet::senderName(NodeNum num)
{
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(num);
    return node ? parseShortName(node) : hexifyNodeNum(num);
}

const StoredMessage *InkHUD::T5HomeApplet::latestIncoming(bool dmOnly)
{
    // The cache also holds broadcasts on channels with no active ThreadedMessageApplet, which never reach the store
    const StoredMessage &m = (latestMessage->wasBroadcast && !dmOnly) ? latestMessage->broadcast : latestMessage->dm;
    if (m.sender && messageStore.isMessageVisible(m))
        return &m;

    // Cached message hidden (sender ignored since): newest visible incoming one in the store, which is oldest first
    const std::deque<StoredMessage> &stored = messageStore.getLiveMessages();
    for (auto it = stored.rbegin(); it != stored.rend(); ++it) {
        if (it->sender && it->sender != nodeDB->getNodeNum() && (!dmOnly || it->type == MessageType::DM_TO_US) &&
            messageStore.isMessageVisible(*it))
            return &*it;
    }
    return nullptr;
}

InkHUD::T5HomeApplet::Status InkHUD::T5HomeApplet::readStatus()
{
    Status s;
    const NodeNum ourNum = nodeDB->getNodeNum();

    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(ourNum);
    if (ourNode)
        s.shortName = parseShortName(ourNode);
    s.nodeId = hexifyNodeNum(ourNum);
    s.clock = getTimeString(); // Empty until the RTC is valid

    if (powerStatus->getHasBattery()) {
        s.battery = to_string(powerStatus->getBatteryChargePercent()) + "%";
        char volts[8];
        snprintf(volts, sizeof(volts), "%.2fV", powerStatus->getBatteryVoltageMv() / 1000.0);
        s.voltage = volts;
    }

    if (myRegion)
        s.region = myRegion->name;
    s.preset = DisplayFormatters::getModemPresetDisplayName(config.lora.modem_preset, false, config.lora.use_preset);

    // No fix type is available (TinyGPS custom fields are compiled out), so only lock and satellites are shown
    if (config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED || !gpsStatus->getIsConnected())
        s.gps = "GPS OFF";
    else if (gpsStatus->getHasLock())
        s.gps = "GPS FIX \xB7 " + to_string(gpsStatus->getNumSatellites());
    else
        s.gps = "GPS NO FIX";

    if (!config.bluetooth.enabled)
        s.bt = "BT OFF";
    else if (bluetoothStatus->getConnectionState() == meshtastic::BluetoothStatus::ConnectionState::CONNECTED)
        s.bt = "BT LINKED";
    else
        s.bt = "BT ON";

    // Percentage of the last 60 seconds the channel was busy
    if (airTime)
        s.channelUtil = roundf(airTime->channelUtilizationPercent());

    std::vector<meshtastic_NodeInfoLite *> heard;
    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (node->num == ourNum)
            continue;
        s.nodes++;
        if (node->has_hops_away) {
            if (node->hops_away == 0)
                s.direct++;
            else if (node->hops_away == 1)
                s.oneHop++;
            else
                s.multiHop++;
        }
        if (heardRecently(node))
            heard.push_back(node);
    }
    std::sort(heard.begin(), heard.end(),
              [](const meshtastic_NodeInfoLite *a, const meshtastic_NodeInfoLite *b) { return a->last_heard > b->last_heard; });
    s.heardCount = heard.size();

    meshtastic_PositionLite ourPos, theirPos;
    const bool haveOurPos = nodeDB->copyNodePosition(ourNum, ourPos);
    for (meshtastic_NodeInfoLite *node : heard) {
        if (s.heard.size() == 6)
            break;
        HeardRow row;
        row.shortName = parseShortName(node);
        row.longName = nodeInfoLiteHasUser(node) ? parse(node->long_name) : hexifyNodeNum(node->num);
        if (haveOurPos && nodeDB->copyNodePosition(node->num, theirPos))
            row.distance = localizeDistance(GeoCoord::latLongToMeter(theirPos.latitude_i * 1e-7, theirPos.longitude_i * 1e-7,
                                                                     ourPos.latitude_i * 1e-7, ourPos.longitude_i * 1e-7));
        if (node->has_hops_away)
            row.hops = hopsString(node->hops_away);
        s.heard.push_back(row);
    }

    s.latest = latestIncoming();
    if (s.latest) {
        const StoredMessage &m = *s.latest;
        s.latestKind = m.type == MessageType::DM_TO_US ? "DM" : parse(channels.getName(m.channelIndex));
        s.latestSender = senderName(m.sender);
        meshtastic_NodeInfoLite *sender = nodeDB->getMeshNode(m.sender);
        if (nodeInfoLiteHasUser(sender)) {
            // Drop what the font can't draw (emoji, variation selectors): parse() marks each with a SUB, which prints as a box
            std::string name = parse(sender->long_name);
            name.erase(std::remove(name.begin(), name.end(), '\x1A'), name.end());
            s.latestSenderLong = name.substr(0, name.find_last_not_of(' ') + 1); // All-emoji name: empty, so join() skips it
        }
        if (!m.isBootRelative)
            s.latestClock = getTimeString(m.timestamp);
        s.latestText = parse(MessageStore::getText(m));
    }

    uint16_t channel0LastHour = 0;
    for (const StoredMessage &m : messageStore.getLiveMessages()) {
        const int32_t age = ageSecs(m);
        if (m.type == MessageType::BROADCAST && m.channelIndex == 0 && age >= 0 && age < 60 * 60 &&
            messageStore.isMessageVisible(m))
            channel0LastHour++;
    }
    s.channel0Title = join("Channel 0", parse(channels.getName(0)));
    s.channel0Subtitle = to_string(channel0LastHour) + (channel0LastHour == 1 ? " message this hour" : " messages this hour");

    if (const StoredMessage *dm = latestIncoming(true)) {
        const int32_t age = ageSecs(*dm);
        s.dmSubtitle = join(senderName(dm->sender), age >= 0 ? agoString(age) : "");
    }

    return s;
}

void InkHUD::T5HomeApplet::onRender(bool full)
{
    (void)full;
    const Status s = readStatus();
    if (isConsole())
        renderConsole(s);
    else
        renderCarry(s);
}

// Word-wrapped, showing only the whole lines which fit above bottom
void InkHUD::T5HomeApplet::printClipped(int16_t left, int16_t top, uint16_t w, int16_t bottom, const std::string &text)
{
    AppletFont font = getFont();
    const int16_t lineBox = font.heightAboveCursor() + font.heightBelowCursor();
    if (bottom - top < lineBox)
        return;
    const int16_t lines = (bottom - top - lineBox) / font.lineHeight() + 1;
    setCrop(left, top, w, (lines - 1) * font.lineHeight() + lineBox);
    printWrapped(left, top, w, text);
    resetCrop();
}

// Title, subtitle, and an inverted "N NEW" pill while the conversation has unseen messages
void InkHUD::T5HomeApplet::drawConversation(int16_t left, int16_t top, uint16_t w, const std::string &title,
                                            const std::string &subtitle, uint16_t unseen)
{
    const bool console = isConsole();
    const int16_t padX = console ? 12 : 13;
    const int16_t padY = console ? 6 : 7;

    setFont(fontSmall);
    int16_t pillW = 0;
    if (unseen) {
        const std::string pill = to_string(unseen) + " NEW";
        const int16_t pillTop = top + (console ? 18 : 22);
        pillW = getTextWidth(pill) + 2 * padX;
        fillRect(left + w - pillW, pillTop, pillW, fontSmall.lineHeight() + 2 * padY, BLACK);
        setTextColor(WHITE);
        printBold(left + w - padX, pillTop + padY, pill, RIGHT);
        setTextColor(BLACK);
    }

    setCrop(left, top, w - (unseen ? pillW + 8 : 0), console ? 88 : 84);
    printAt(left, top + (console ? 50 : 54), subtitle);
    setFont(fontMedium);
    printBold(left, top + (console ? 8 : 12), title);
    resetCrop();
}

// "⇄ CONSOLE" or "⇄ CARRY". The fonts have no ⇄ glyph, so the arrows are drawn
void InkHUD::T5HomeApplet::drawModeControl(int16_t x, int16_t y, uint16_t w, uint16_t h)
{
    constexpr int16_t arrowW = 20, arrowH = 16, gap = 10, head = 4;
    const std::string label = isConsole() ? "CARRY" : "CONSOLE";

    setFont(fontSmall);
    const int16_t left = x + (w - (arrowW + gap + getTextWidth(label))) / 2;
    const int16_t upper = y + (h - arrowH) / 2 + head;
    const int16_t lower = upper + arrowH - 2 * head;

    drawRect(x, y, w, h, BLACK);
    drawLine(left, upper, left + arrowW, upper, BLACK); // Right-pointing
    drawLine(left + arrowW, upper, left + arrowW - head, upper - head, BLACK);
    drawLine(left + arrowW, upper, left + arrowW - head, upper + head, BLACK);
    drawLine(left, lower, left + arrowW, lower, BLACK); // Left-pointing
    drawLine(left, lower, left + head, lower - head, BLACK);
    drawLine(left, lower, left + head, lower + head, BLACK);
    printAt(left + arrowW + gap, y + h / 2, label, LEFT, MIDDLE);
}

void InkHUD::T5HomeApplet::renderCarry(const Status &s)
{
    const int16_t right = width() - MARGIN;
    const uint16_t innerW = width() - 2 * MARGIN;

    // Identity: short name and node id left, battery and clock right, sharing a baseline
    const std::string power = join(s.battery, s.clock);
    setFont(fontLarge);
    printBold(MARGIN, 14, s.shortName);
    const int16_t nameRight = MARGIN + getTextWidth(s.shortName);
    const int16_t baseline = 14 + fontLarge.heightAboveCursor();
    setFont(fontMedium);
    const int16_t powerLeft = right - getTextWidth(power);
    printBold(right, baseline - fontMedium.heightAboveCursor(), power, RIGHT);
    setFont(fontSmall);
    if (nameRight + 12 + getTextWidth(s.nodeId) + 12 <= powerLeft)
        printAt(nameRight + 12, baseline - fontSmall.heightAboveCursor(), s.nodeId);
    for (int16_t x = 0; x < width(); x += 2)
        drawPixel(x, 75, BLACK);

    // Status band
    int16_t x = MARGIN;
    for (const std::string &chip : {"LORA " + s.region, s.gps, s.bt}) {
        if (x > MARGIN) {
            drawLine(x, 86, x, 112, BLACK);
            x += 12;
        }
        printBold(x, 99 - fontSmall.lineHeight() / 2, chip);
        x += getTextWidth(chip) + 12;
    }
    drawLine(0, 119, width() - 1, 119, BLACK);

    // Unread: latest message, then the channel and DM conversations
    printBold(MARGIN, 128, "UNREAD");
    printBold(right, 128, to_string(newDMs + newChannel0), RIGHT);
    if (s.latest) {
        drawRect(MARGIN, 152, innerW, 116, BLACK);
        drawRect(MARGIN + 1, 153, innerW - 2, 114, BLACK);
        printAt(MARGIN + 12, 160, join(join(s.latestKind, s.latestSender), s.latestClock));
        setFont(fontLarge);
        printClipped(MARGIN + 12, 186, innerW - 24, 266, s.latestText);
    }
    drawLine(MARGIN, 288, right - 1, 288, BLACK);
    drawConversation(MARGIN, 288, innerW, s.channel0Title, s.channel0Subtitle, newChannel0);
    drawLine(MARGIN, 372, right - 1, 372, BLACK);
    drawConversation(MARGIN, 372, innerW, "Direct messages", s.dmSubtitle, newDMs);
    drawLine(MARGIN, 456, right - 1, 456, BLACK);

    // Heard: three most recent
    setFont(fontSmall);
    printBold(MARGIN, 468, "HEARD \xB7 10 MIN");
    printBold(right, 468, to_string(s.heardCount), RIGHT);
    for (uint8_t i = 0; i < 3 && i < s.heard.size(); i++) {
        const HeardRow &row = s.heard[i];
        const int16_t top = 496 + i * 84;
        setFont(fontMedium);
        printBold(MARGIN, top + 12, row.shortName);
        setFont(fontSmall);
        printAt(MARGIN, top + 54, row.longName);
        printBold(right, top + 54, row.distance.empty() ? row.hops : row.distance, RIGHT);
        drawLine(MARGIN, top + 83, right - 1, top + 83, BLACK);
    }

    drawModeControl(right - 168, navTop() - 1 - 17 - 50, 168, 50);
    drawNav(HOME);
}

void InkHUD::T5HomeApplet::renderConsole(const Status &s)
{
    const int16_t right = width() - MARGIN;

    // Status line. Real font metrics can't fit all the design shows: drop the node id, then the preset,
    // then the battery voltage, only while the line overflows
    std::string nodeId = s.nodeId;
    std::string lora = "LORA " + s.region + " " + s.preset;
    std::string batt = s.battery.empty() ? "" : join("BATT " + s.battery, s.voltage);
    auto chips = [&]() {
        std::vector<std::string> list;
        for (const std::string &chip : {lora, s.gps, s.bt, batt, s.clock}) {
            if (!chip.empty())
                list.push_back(chip);
        }
        return list;
    };

    setFont(fontLarge);
    const int16_t nameTop = (63 - (fontLarge.heightAboveCursor() + fontLarge.heightBelowCursor())) / 2;
    const int16_t nameRight = MARGIN + getTextWidth(s.shortName);
    printBold(MARGIN, nameTop, s.shortName);
    const int16_t baseline = nameTop + fontLarge.heightAboveCursor();

    setFont(fontSmall);
    auto overflows = [&]() {
        int16_t used = (nodeId.empty() ? nameRight : nameRight + 14 + getTextWidth(nodeId)) + 14;
        for (const std::string &chip : chips())
            used += getTextWidth(chip) + 28;
        return used - 14 > right; // Last chip has no right padding
    };
    if (overflows())
        nodeId.clear();
    if (overflows())
        lora = "LORA " + s.region;
    if (overflows() && !batt.empty())
        batt = "BATT " + s.battery;

    if (!nodeId.empty())
        printAt(nameRight + 14, baseline - fontSmall.heightAboveCursor(), nodeId);
    int16_t x = right;
    const std::vector<std::string> list = chips();
    for (auto chip = list.rbegin(); chip != list.rend(); ++chip) {
        printBold(x, 32 - fontSmall.lineHeight() / 2, *chip, RIGHT);
        x -= getTextWidth(*chip) + 14;
        drawLine(x, 16, x, 47, BLACK);
        x -= 14;
    }
    drawLine(0, 63, width() - 1, 63, BLACK);

    // Latest message
    if (s.latest) {
        printBold(MARGIN, 76, join("LATEST", s.latestKind));
        setCrop(MARGIN, 113, 354, fontSmall.heightAboveCursor() + fontSmall.heightBelowCursor());
        printAt(MARGIN, 113, join(join(s.latestSender, s.latestSenderLong), s.latestClock));
        resetCrop();
        for (int16_t dx = 0; dx < 330; dx += 2)
            drawPixel(MARGIN + dx, 146, BLACK);
        setFont(fontLarge);
        printClipped(MARGIN, 158, 340, 292, s.latestText);
        setFont(fontSmall);
    }
    for (int16_t y = 72; y < 296; y += 2) {
        drawPixel(386, y, BLACK);
        drawPixel(648, y, BLACK);
    }

    // Mesh counters: single-line label / value rows
    constexpr int16_t meshLeft = 404, meshW = 228, rowH = 31;
    const std::pair<const char *, std::string> counters[] = {
        {"NODES", to_string(s.nodes)},          {"HEARD 10M", to_string(s.heardCount)},
        {"DIRECT", to_string(s.direct)},        {"VIA 1 HOP", to_string(s.oneHop)},
        {"VIA 2+ HOPS", to_string(s.multiHop)}, {"CH UTIL 60S", to_string(s.channelUtil) + "%"},
    };
    printBold(meshLeft, 76, "MESH");
    for (uint8_t i = 0; i < 6; i++) {
        const int16_t top = 109 + i * rowH;
        printAt(meshLeft, top + 2, counters[i].first);
        printBold(meshLeft + meshW, top + 2, counters[i].second, RIGHT);
        drawLine(meshLeft, top + rowH - 1, meshLeft + meshW - 1, top + rowH - 1, BLACK);
    }

    // Heard: six most recent
    constexpr int16_t heardLeft = 666, heardW = 246;
    printBold(heardLeft, 76, "HEARD \xB7 10 MIN");
    printBold(heardLeft + heardW, 76, to_string(s.heardCount), RIGHT);
    for (uint8_t i = 0; i < s.heard.size(); i++) {
        const int16_t top = 105 + i * rowH;
        printBold(heardLeft, top + 2, s.heard[i].shortName);
        printAt(heardLeft + heardW, top + 2, join(s.heard[i].distance, s.heard[i].hops), RIGHT);
        drawLine(heardLeft, top + rowH - 1, heardLeft + heardW - 1, top + rowH - 1, BLACK);
    }

    // Unread pair
    drawLine(0, 303, width() - 1, 303, BLACK);
    drawConversation(MARGIN, 316, 436, s.channel0Title, s.channel0Subtitle, newChannel0);
    for (int16_t y = 312; y < 408; y += 2)
        drawPixel(464, y, BLACK);
    drawConversation(482, 316, 430, "Direct messages", s.dmSubtitle, newDMs);

    drawModeControl(navWidth() + (CONSOLE_SLOT_W - 120) / 2, navTop() + (NAV_H - 46) / 2, 120, 46);
    drawNav(HOME);
}

void InkHUD::T5HomeApplet::openApplet(const std::string &name)
{
    const int8_t index = indexOf(name.c_str());
    if (index >= 0)
        inkhud->showApplet(index); // Ignores inactive applets, without a refresh
}

void InkHUD::T5HomeApplet::openLatest()
{
    const StoredMessage *m = latestIncoming();
    if (m)
        openApplet(m->type == MessageType::DM_TO_US ? "DMs" : "Channel " + to_string(m->channelIndex));
}

bool InkHUD::T5HomeApplet::onTouchPoint(uint16_t x, uint16_t y, bool longPress)
{
    if (longPress)
        return false; // Keep the shared long-press fallback (menu)

    // Touch points are in display space; drawing is relative to our tile
    const int16_t tx = x - getTile()->getLeft();
    const int16_t ty = y - getTile()->getTop();

    if (handleNavTap(tx, ty))
        return true;

    if (isConsole()) {
        if (tx >= navWidth() && ty >= navTop())
            t5ToggleMode();
        else if (ty >= 76 && ty < 292 && tx < 386)
            openLatest();
        else if (ty >= 76 && ty < 292 && tx >= 666)
            openApplet("Nodes");
        else if (ty >= 316 && ty < 404)
            openApplet(tx < 464 ? "Channel 0" : "DMs");
    } else {
        if (ty >= navTop() - 68 && ty < navTop() - 18 && tx >= width() - MARGIN - 168 && tx < width() - MARGIN) // Mode control
            t5ToggleMode();
        else if (ty >= 152 && ty < 268)
            openLatest();
        else if (ty >= 288 && ty < 372)
            openApplet("Channel 0");
        else if (ty >= 372 && ty < 456)
            openApplet("DMs");
        else if (ty >= 468 && ty < 748)
            openApplet("Nodes");
    }

    // Consume every tap: the button fallback would cycle applets
    return true;
}

#endif
