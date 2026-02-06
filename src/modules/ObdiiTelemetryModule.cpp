#include "configuration.h"
#if defined(ARCH_ESP32) && defined(USERPREFS_OBDII_ENABLED)

#include "ObdiiTelemetryModule.h"
#include "MeshService.h"
#include "Router.h"
#include <Arduino.h>
#include <algorithm>
#include <cctype>

ObdiiTelemetryModule *obdiiTelemetryModule;

namespace
{
constexpr uint32_t kScanDurationSeconds = 3;
constexpr uint32_t kCommandTimeoutMs = 2000;
constexpr uint32_t kReconnectBackoffMs = 5000;
constexpr uint32_t kPollIntervalMs = 500;
constexpr uint32_t kDiscoverIntervalMs = 300;

bool nameLooksLikeObd(const std::string &name)
{
    if (name.empty())
        return false;
    std::string upper = name;
    for (auto &c : upper) {
        c = (char)toupper(static_cast<unsigned char>(c));
    }
    const char *needles[] = {"OBD", "ELM", "OBDII", "VLINK", "V-LINK"};
    for (const auto &needle : needles) {
        if (upper.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

std::string trim(const std::string &in)
{
    size_t start = 0;
    while (start < in.size() && (in[start] == '\r' || in[start] == '\n' || in[start] == ' '))
        start++;
    size_t end = in.size();
    while (end > start && (in[end - 1] == '\r' || in[end - 1] == '\n' || in[end - 1] == ' '))
        end--;
    return in.substr(start, end - start);
}

std::string hexByte(uint8_t v)
{
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", v);
    return std::string(buf);
}

bool parseHexBytes(const std::string &response, std::vector<uint8_t> &out)
{
    out.clear();
    for (size_t i = 0; i < response.size();) {
        while (i < response.size() && response[i] == ' ')
            i++;
        if (i + 1 >= response.size())
            break;
        char c1 = response[i];
        char c2 = response[i + 1];
        if (!isxdigit(static_cast<unsigned char>(c1)) || !isxdigit(static_cast<unsigned char>(c2)))
            return false;
        uint8_t val = (uint8_t)strtoul(response.substr(i, 2).c_str(), nullptr, 16);
        out.push_back(val);
        i += 2;
        if (i < response.size() && response[i] == ' ')
            i++;
    }
    return !out.empty();
}
} // namespace

class ObdiiClientCallbacks : public NimBLEClientCallbacks
{
  public:
    void onDisconnect(NimBLEClient *client) override
    {
        LOG_WARN("OBDII: BLE disconnected");
        if (obdiiTelemetryModule) {
            obdiiTelemetryModule->resetConnectionState();
        }
    }
};

ObdiiTelemetryModule::ObdiiTelemetryModule()
    : SinglePortModule("obdii", meshtastic_PortNum_TEXT_MESSAGE_APP),
      concurrency::OSThread("OBDII")
{
    obdiiTelemetryModule = this;
}

void ObdiiTelemetryModule::notifyCallback(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length,
                                          bool isNotify)
{
    (void)pRemoteCharacteristic;
    (void)isNotify;
    if (!obdiiTelemetryModule)
        return;
    obdiiTelemetryModule->handleNotify(pData, length);
}

void ObdiiTelemetryModule::handleNotify(const uint8_t *data, size_t length)
{
    rxBuffer.append(reinterpret_cast<const char *>(data), length);
    auto promptPos = rxBuffer.find('>');
    if (promptPos != std::string::npos) {
        lastResponse = rxBuffer.substr(0, promptPos);
        rxBuffer.erase(0, promptPos + 1);
        responseReady = true;
    }
}

void ObdiiTelemetryModule::resetConnectionState()
{
    if (client) {
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        client = nullptr;
    }
    txChar = nullptr;
    rxChar = nullptr;
    rxBuffer.clear();
    lastResponse.clear();
    pendingCommand.clear();
    initQueue.clear();
    supportedPids.clear();
    pidIndex = 0;
    responseReady = false;
    inited = false;
    pidDiscoveryDone = false;
    latestRpm = -1;
    latestVoltageMv = -1;
    lastUpdateMs = 0;
    state = State::Backoff;
    nextActionMs = millis() + kReconnectBackoffMs;
}

void ObdiiTelemetryModule::requestRescan()
{
    rescanRequested = true;
}

const char *ObdiiTelemetryModule::getStateLabel() const
{
    switch (state) {
    case State::Idle:
        return "idle";
    case State::Scanning:
        return "scanning";
    case State::Connecting:
        return "connecting";
    case State::Discovering:
        return "discovering";
    case State::InitAdapter:
        return "init";
    case State::DiscoverPids:
        return "pids";
    case State::Polling:
        return "polling";
    case State::Backoff:
        return "backoff";
    default:
        return "unknown";
    }
}

bool ObdiiTelemetryModule::scanForAdapter()
{
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(45);
    scan->setWindow(30);

    LOG_INFO("OBDII: scanning for BLE adapters...");
    NimBLEScanResults results = scan->start(kScanDurationSeconds, false);
    int bestIdx = -1;
    int bestRssi = -999;
    for (int i = 0; i < results.getCount(); ++i) {
        NimBLEAdvertisedDevice dev = results.getDevice(i);
        std::string name = dev.getName();
        if (!nameLooksLikeObd(name))
            continue;
        if (dev.getRSSI() > bestRssi) {
            bestIdx = i;
            bestRssi = dev.getRSSI();
        }
    }

    if (bestIdx < 0 && results.getCount() > 0) {
        // If we didn't find a name match, fall back to the strongest device.
        for (int i = 0; i < results.getCount(); ++i) {
            NimBLEAdvertisedDevice dev = results.getDevice(i);
            if (dev.getRSSI() > bestRssi) {
                bestIdx = i;
                bestRssi = dev.getRSSI();
            }
        }
    }

    if (bestIdx < 0)
        return false;

    NimBLEAdvertisedDevice dev = results.getDevice(bestIdx);
    LOG_INFO("OBDII: found device %s (%s) RSSI %d", dev.getAddress().toString().c_str(), dev.getName().c_str(),
             dev.getRSSI());

    client = NimBLEDevice::createClient();
    client->setClientCallbacks(new ObdiiClientCallbacks(), true);
    if (!client->connect(dev)) {
        NimBLEDevice::deleteClient(client);
        client = nullptr;
        return false;
    }
    return true;
}

bool ObdiiTelemetryModule::connectToAdapter()
{
    if (!client) {
        if (!scanForAdapter())
            return false;
    }
    if (!client->isConnected()) {
        return false;
    }
    return true;
}

bool ObdiiTelemetryModule::discoverUartCharacteristics()
{
    txChar = nullptr;
    rxChar = nullptr;

    auto services = client->getServices();
    for (auto &svcPair : services) {
        NimBLERemoteService *svc = svcPair.second;
        auto chars = svc->getCharacteristics();
        for (auto &charPair : chars) {
            NimBLERemoteCharacteristic *ch = charPair.second;
            bool canWrite = ch->canWrite() || ch->canWriteNoResponse();
            bool canNotify = ch->canNotify() || ch->canIndicate();
            if (canWrite && !txChar)
                txChar = ch;
            if (canNotify && !rxChar)
                rxChar = ch;
        }
        if (txChar && rxChar)
            break;
    }

    if (!txChar || !rxChar) {
        LOG_WARN("OBDII: failed to find UART characteristics");
        return false;
    }

    LOG_INFO("OBDII: TX char %s, RX char %s", txChar->getUUID().toString().c_str(), rxChar->getUUID().toString().c_str());
    return true;
}

bool ObdiiTelemetryModule::startNotifications()
{
    if (!rxChar)
        return false;
    if (!rxChar->subscribe(true, notifyCallback)) {
        LOG_WARN("OBDII: failed to subscribe to notifications");
        return false;
    }
    return true;
}

void ObdiiTelemetryModule::enqueueInitCommands()
{
    initQueue.clear();
    initQueue.emplace_back("ATZ");
    initQueue.emplace_back("ATE0");
    initQueue.emplace_back("ATL0");
    initQueue.emplace_back("ATS0");
    initQueue.emplace_back("ATH0");
    initQueue.emplace_back("ATSP0");
}

bool ObdiiTelemetryModule::sendCommand(const std::string &cmd)
{
    if (!txChar)
        return false;
    std::string full = cmd;
    full.append("\r");
    pendingCommand = cmd;
    responseReady = false;
    lastResponse.clear();
    lastCommandMs = millis();
    return txChar->writeValue(reinterpret_cast<const uint8_t *>(full.data()), full.size(), false);
}

bool ObdiiTelemetryModule::waitForResponse(uint32_t timeoutMs)
{
    if (responseReady)
        return true;
    if (millis() - lastCommandMs > timeoutMs) {
        LOG_WARN("OBDII: command timeout %s", pendingCommand.c_str());
        resetConnectionState();
        return false;
    }
    return false;
}

std::string ObdiiTelemetryModule::normalizeResponse(const std::string &response) const
{
    std::string out;
    out.reserve(response.size());
    for (char c : response) {
        if (c == '\r' || c == '\n')
            out.push_back(' ');
        else
            out.push_back(c);
    }
    out = trim(out);
    while (out.find("  ") != std::string::npos) {
        out.erase(out.find("  "), 1);
    }
    size_t searchingPos = out.find("SEARCHING...");
    if (searchingPos != std::string::npos) {
        out.erase(searchingPos, strlen("SEARCHING..."));
    }
    size_t stoppedPos = out.find("STOPPED");
    if (stoppedPos != std::string::npos) {
        out.erase(stoppedPos, strlen("STOPPED"));
    }
    out = trim(out);
    return out;
}

bool ObdiiTelemetryModule::isResponseOk(const std::string &response) const
{
    return response.find("OK") != std::string::npos;
}

bool ObdiiTelemetryModule::isResponseNoData(const std::string &response) const
{
    return response.find("NO DATA") != std::string::npos || response.find("?") != std::string::npos;
}

bool ObdiiTelemetryModule::isResponseForPid(const std::string &response, uint8_t pid) const
{
    std::string header = "41 " + hexByte(pid);
    return response.find(header) != std::string::npos;
}

bool ObdiiTelemetryModule::parsePidSupport(const std::string &response, uint8_t basePid)
{
    std::string clean = normalizeResponse(response);
    std::vector<uint8_t> bytes;
    if (!parseHexBytes(clean, bytes))
        return false;
    if (bytes.size() < 6 || bytes[0] != 0x41 || bytes[1] != basePid)
        return false;
    for (int bit = 0; bit < 32; ++bit) {
        int byteIndex = 2 + (bit / 8);
        int bitIndex = 7 - (bit % 8);
        bool supported = (bytes[byteIndex] >> bitIndex) & 0x01;
        if (supported) {
            uint8_t pid = basePid + bit + 1;
            supportedPids.push_back(pid);
        }
    }
    return true;
}

bool ObdiiTelemetryModule::parsePidResponse(uint8_t pid, const std::string &response, std::string &jsonOut)
{
    std::string clean = normalizeResponse(response);
    if (!isResponseForPid(clean, pid))
        return false;

    std::vector<uint8_t> bytes;
    if (!parseHexBytes(clean, bytes))
        return false;

    std::string name;
    std::string unit;
    char valueBuf[32] = {};
    bool hasValue = false;

    if (bytes.size() >= 4 && bytes[0] == 0x41 && bytes[1] == pid) {
        uint8_t A = bytes[2];
        uint8_t B = bytes[3];
        switch (pid) {
        case 0x0C: { // Engine RPM
            uint16_t rpm = ((uint16_t)A * 256 + B) / 4;
            name = "rpm";
            unit = "rpm";
            snprintf(valueBuf, sizeof(valueBuf), "%u", rpm);
            hasValue = true;
            break;
        }
        case 0x0D: { // Vehicle speed
            name = "speed";
            unit = "kmh";
            snprintf(valueBuf, sizeof(valueBuf), "%u", A);
            hasValue = true;
            break;
        }
        case 0x05: { // Coolant temp
            name = "coolant_c";
            unit = "c";
            int temp = (int)A - 40;
            snprintf(valueBuf, sizeof(valueBuf), "%d", temp);
            hasValue = true;
            break;
        }
        case 0x0F: { // Intake air temp
            name = "intake_c";
            unit = "c";
            int temp = (int)A - 40;
            snprintf(valueBuf, sizeof(valueBuf), "%d", temp);
            hasValue = true;
            break;
        }
        case 0x11: { // Throttle position
            name = "throttle_pct";
            unit = "pct";
            int pct = (int)((A * 100) / 255);
            snprintf(valueBuf, sizeof(valueBuf), "%d", pct);
            hasValue = true;
            break;
        }
        case 0x0B: { // Intake manifold pressure
            name = "map_kpa";
            unit = "kpa";
            snprintf(valueBuf, sizeof(valueBuf), "%u", A);
            hasValue = true;
            break;
        }
        case 0x10: { // MAF
            uint16_t maf = ((uint16_t)A * 256 + B);
            name = "maf_gps";
            unit = "g/s";
            snprintf(valueBuf, sizeof(valueBuf), "%u", maf / 100);
            hasValue = true;
            break;
        }
        case 0x42: { // Control module voltage
            uint16_t mv = ((uint16_t)A * 256 + B);
            name = "voltage_v";
            unit = "v";
            float volts = (float)mv / 1000.0f;
            snprintf(valueBuf, sizeof(valueBuf), "%.2f", volts);
            hasValue = true;
            latestVoltageMv = mv;
            lastUpdateMs = millis();
            break;
        }
        case 0x04: { // Calculated engine load
            name = "load_pct";
            unit = "pct";
            int pct = (int)((A * 100) / 255);
            snprintf(valueBuf, sizeof(valueBuf), "%d", pct);
            hasValue = true;
            break;
        }
        default:
            break;
        }
    }
    if (pid == 0x0C && hasValue) {
        latestRpm = atoi(valueBuf);
        lastUpdateMs = millis();
    }

    std::string raw;
    raw.reserve(clean.size());
    for (char c : clean) {
        if (c != ' ')
            raw.push_back(c);
    }

    char json[220];
    if (hasValue) {
        if (!unit.empty()) {
            snprintf(json, sizeof(json), "{\"obd\":{\"pid\":\"%s\",\"name\":\"%s\",\"val\":%s,\"unit\":\"%s\"}}",
                     hexByte(pid).c_str(), name.c_str(), valueBuf, unit.c_str());
        } else {
            snprintf(json, sizeof(json), "{\"obd\":{\"pid\":\"%s\",\"name\":\"%s\",\"val\":%s}}", hexByte(pid).c_str(),
                     name.c_str(), valueBuf);
        }
    } else {
        snprintf(json, sizeof(json), "{\"obd\":{\"pid\":\"%s\",\"raw\":\"%s\"}}", hexByte(pid).c_str(), raw.c_str());
    }

    jsonOut = json;
    return true;
}

void ObdiiTelemetryModule::sendJsonToMesh(const std::string &json)
{
    if (json.empty())
        return;
    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = NODENUM_BROADCAST;
    p->channel = 0;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    size_t len = json.size();
    if (len > sizeof(p->decoded.payload.bytes)) {
        len = sizeof(p->decoded.payload.bytes);
    }
    p->decoded.payload.size = len;
    memcpy(p->decoded.payload.bytes, json.data(), len);
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}

int32_t ObdiiTelemetryModule::runOnce()
{
    if (rescanRequested) {
        rescanRequested = false;
        resetConnectionState();
        state = State::Idle;
        nextActionMs = 0;
    }
    if (!config.bluetooth.enabled) {
        return 2000;
    }

    switch (state) {
    case State::Idle: {
        state = State::Scanning;
        if (scanForAdapter()) {
            state = State::Discovering;
        } else {
            state = State::Backoff;
            nextActionMs = millis() + kReconnectBackoffMs;
        }
        break;
    }
    case State::Discovering: {
        if (!connectToAdapter()) {
            resetConnectionState();
            break;
        }
        if (!discoverUartCharacteristics() || !startNotifications()) {
            resetConnectionState();
            break;
        }
        enqueueInitCommands();
        state = State::InitAdapter;
        break;
    }
    case State::InitAdapter: {
        if (pendingCommand.empty() && !initQueue.empty()) {
            sendCommand(initQueue.front());
            initQueue.pop_front();
        } else if (!pendingCommand.empty()) {
            if (waitForResponse(kCommandTimeoutMs)) {
                std::string clean = normalizeResponse(lastResponse);
                if (isResponseNoData(clean)) {
                    resetConnectionState();
                    break;
                }
                pendingCommand.clear();
                responseReady = false;
            }
        }
        if (pendingCommand.empty() && initQueue.empty()) {
            inited = true;
            state = State::DiscoverPids;
            nextActionMs = millis();
        }
        break;
    }
    case State::DiscoverPids: {
        if (millis() < nextActionMs)
            break;
        static const uint8_t bases[] = {0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0};
        static size_t baseIdx = 0;
        if (pendingCommand.empty()) {
            char cmd[8];
            snprintf(cmd, sizeof(cmd), "01%02X", bases[baseIdx]);
            sendCommand(cmd);
        } else if (waitForResponse(kCommandTimeoutMs)) {
            std::string clean = normalizeResponse(lastResponse);
            if (!isResponseNoData(clean)) {
                parsePidSupport(clean, bases[baseIdx]);
            }
            pendingCommand.clear();
            responseReady = false;
            baseIdx++;
            nextActionMs = millis() + kDiscoverIntervalMs;
            if (baseIdx >= (sizeof(bases) / sizeof(bases[0]))) {
                pidDiscoveryDone = true;
                if (std::find(supportedPids.begin(), supportedPids.end(), 0x0C) == supportedPids.end())
                    supportedPids.push_back(0x0C);
                if (std::find(supportedPids.begin(), supportedPids.end(), 0x42) == supportedPids.end())
                    supportedPids.push_back(0x42);
                if (supportedPids.empty())
                    LOG_WARN("OBDII: no supported PIDs found");
                else
                    LOG_INFO("OBDII: discovered %u supported PIDs", (unsigned)supportedPids.size());
                state = State::Polling;
            }
        }
        break;
    }
    case State::Polling: {
        if (!pidDiscoveryDone || supportedPids.empty()) {
            state = State::Backoff;
            nextActionMs = millis() + kReconnectBackoffMs;
            break;
        }
        if (pendingCommand.empty() && (millis() - lastCommandMs) >= kPollIntervalMs) {
            uint8_t pid = supportedPids[pidIndex % supportedPids.size()];
            char cmd[8];
            snprintf(cmd, sizeof(cmd), "01%02X", pid);
            sendCommand(cmd);
        } else if (!pendingCommand.empty()) {
            if (waitForResponse(kCommandTimeoutMs)) {
                std::string json;
                if (!isResponseNoData(lastResponse)) {
                    uint8_t pid = supportedPids[pidIndex % supportedPids.size()];
                    if (parsePidResponse(pid, lastResponse, json)) {
                        sendJsonToMesh(json);
                    }
                }
                pidIndex++;
                pendingCommand.clear();
                responseReady = false;
                lastCommandMs = millis();
            }
        }
        break;
    }
    case State::Backoff: {
        if (millis() >= nextActionMs) {
            state = State::Idle;
        }
        break;
    }
    default:
        break;
    }

    return 100;
}

#endif
