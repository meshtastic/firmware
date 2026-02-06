#pragma once

#include "configuration.h"

#if defined(ARCH_ESP32) && defined(USERPREFS_OBDII_ENABLED)

#include "mesh/SinglePortModule.h"
#include "concurrency/OSThread.h"

#include <NimBLEDevice.h>
#include <deque>
#include <string>
#include <vector>

class ObdiiTelemetryModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    ObdiiTelemetryModule();
    int32_t runOnce() override;
    void requestRescan();
    int getLatestRpm() const { return latestRpm; }
    int getLatestVoltageMv() const { return latestVoltageMv; }
    uint32_t getLastUpdateMs() const { return lastUpdateMs; }
    const char *getStateLabel() const;

  private:
    enum class State {
        Idle,
        Scanning,
        Connecting,
        Discovering,
        InitAdapter,
        DiscoverPids,
        Polling,
        Backoff
    };

    State state = State::Idle;

    NimBLEClient *client = nullptr;
    NimBLERemoteCharacteristic *txChar = nullptr;
    NimBLERemoteCharacteristic *rxChar = nullptr;

    std::string rxBuffer;
    std::string lastResponse;
    std::string pendingCommand;
    std::deque<std::string> initQueue;

    std::vector<uint8_t> supportedPids;
    size_t pidIndex = 0;
    uint32_t lastCommandMs = 0;
    uint32_t nextActionMs = 0;

    bool responseReady = false;
    bool inited = false;
    bool pidDiscoveryDone = false;
    bool rescanRequested = false;

    int latestRpm = -1;
    int latestVoltageMv = -1;
    uint32_t lastUpdateMs = 0;

    static void notifyCallback(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length,
                               bool isNotify);
    void handleNotify(const uint8_t *data, size_t length);

    void resetConnectionState();
    bool scanForAdapter();
    bool connectToAdapter();
    bool discoverUartCharacteristics();
    bool startNotifications();

    void enqueueInitCommands();
    bool sendCommand(const std::string &cmd);
    bool waitForResponse(uint32_t timeoutMs);

    bool parsePidSupport(const std::string &response, uint8_t basePid);
    bool parsePidResponse(uint8_t pid, const std::string &response, std::string &jsonOut);

    std::string normalizeResponse(const std::string &response) const;
    bool isResponseOk(const std::string &response) const;
    bool isResponseNoData(const std::string &response) const;
    bool isResponseForPid(const std::string &response, uint8_t pid) const;

    void sendJsonToMesh(const std::string &json);
};

extern ObdiiTelemetryModule *obdiiTelemetryModule;

#endif
