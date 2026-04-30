#pragma once

#include "MeshTypes.h"
#include "mesh/generated/meshtastic/config.pb.h"
#include "mesh/generated/meshtastic/portnums.pb.h"

#include <stdint.h>

constexpr uint8_t DCR_CR_SLIM = 5;
constexpr uint8_t DCR_CR_NORMAL = 6;
constexpr uint8_t DCR_CR_ROBUST = 7;
constexpr uint8_t DCR_CR_RESCUE = 8;

// Coarse packet classes keep the policy stable when the payload is already
// encrypted or a repeater is running in a mode that skips decoding. Portnums
// refine the class when we still have decoded local context.
enum class DcrPacketClass : uint8_t { Expendable = 0, Normal = 1, Control = 2, Urgent = 3 };

enum DcrReasonFlags : uint32_t {
    DCR_REASON_NONE = 0,
    DCR_REASON_IDLE = 1 << 0,
    DCR_REASON_BUSY = 1 << 1,
    DCR_REASON_CONGESTED = 1 << 2,
    DCR_REASON_PERIODIC = 1 << 3,
    DCR_REASON_USER = 1 << 4,
    DCR_REASON_CONTROL = 1 << 5,
    DCR_REASON_URGENT = 1 << 6,
    DCR_REASON_RETRY = 1 << 7,
    DCR_REASON_FINAL_RETRY = 1 << 8,
    DCR_REASON_QUIET_LOSS = 1 << 9,
    DCR_REASON_COLLISION_PRESSURE = 1 << 10,
    DCR_REASON_RELAY = 1 << 11,
    DCR_REASON_LATE_RELAY = 1 << 12,
    DCR_REASON_LAST_HOP = 1 << 13,
    DCR_REASON_WEAK_LINK = 1 << 14,
    DCR_REASON_STRONG_LINK = 1 << 15,
    DCR_REASON_DUTY_CYCLE = 1 << 16,
    DCR_REASON_TOKEN_BUCKET = 1 << 17,
    DCR_REASON_AIRTIME_CAP = 1 << 18,
};

struct DcrSettings {
    meshtastic_Config_LoRaConfig_DynamicCodingRateMode mode =
        meshtastic_Config_LoRaConfig_DynamicCodingRateMode_DCR_OFF;
    // These are LoRa CR denominators: 5 means CR 4/5, 8 means CR 4/8.
    uint8_t minCr = DCR_CR_SLIM;
    uint8_t maxCr = DCR_CR_RESCUE;
    // Class clamps deliberately make background traffic spend less of the
    // shared robust-airtime budget than user/control/urgent traffic.
    uint8_t telemetryMaxCr = DCR_CR_NORMAL;
    // Let normal user traffic become CR 4/5 under pressure by default. Idle,
    // weak-link, and retry scoring can still raise it back to robust CRs.
    uint8_t userMinCr = DCR_CR_SLIM;
    uint8_t alertMinCr = DCR_CR_ROBUST;
    uint8_t robustAirtimePct = 10;
    bool trackNeighborCr = true;
};

struct ChannelAirtimeStats {
    float channelUtilizationPercent = 0.0f;
    float txUtilizationPercent = 0.0f;
    float dutyCyclePercent = 100.0f;
    uint8_t queueDepth = 0;
};

struct DcrRetryContext {
    uint8_t attempt = 0;
    bool finalRetry = false;
    // True when the routing layer thinks the last loss was quiet/link-related.
    // The policy still rechecks current channel pressure before adding FEC.
    bool quietLoss = false;
};

struct DcrPacketContext {
    const meshtastic_MeshPacket *packet = nullptr;
    meshtastic_PortNum portnum = meshtastic_PortNum_UNKNOWN_APP;
    DcrPacketClass packetClass = DcrPacketClass::Normal;
    meshtastic_MeshPacket_Priority priority = meshtastic_MeshPacket_Priority_UNSET;
    uint8_t baseCr = DCR_CR_SLIM;
    uint32_t packetLen = 0;
    uint32_t predictedAirtimeMs = 0;
    DcrRetryContext retry;
    bool classKnown = false;
    bool localOrigin = false;
    bool relay = false;
    bool lateRelay = false;
    bool lastHop = false;
    bool direct = false;
    float rxSnr = 0.0f;
    int32_t rxRssi = 0;
    uint32_t nowMsec = 0;
};

struct DcrDecision {
    uint8_t cr = DCR_CR_SLIM;
    DcrPacketClass packetClass = DcrPacketClass::Normal;
    uint32_t reasonFlags = DCR_REASON_NONE;
    int8_t score = 0;
    uint32_t predictedAirtimeMs = 0;
    bool changed = false;
};

struct DcrRxObservation {
    NodeNum from = 0;
    uint8_t relayNode = 0;
    uint8_t hopStart = 0;
    uint8_t hopLimit = 0;
    uint8_t rxCr = 0;
    uint32_t airtimeMs = 0;
    float snr = 0.0f;
    int32_t rssi = 0;
    uint32_t nowMsec = 0;
};

struct DcrTxResult {
    NodeNum to = 0;
    NodeNum from = 0;
    PacketId id = 0;
    uint8_t cr = 0;
    bool success = false;
};

struct NeighborCrStats {
    NodeNum nodeNum = 0;
    // RX CR is the physical CR of the last transmitter we heard on this hop,
    // not necessarily the original MeshPacket sender.
    uint8_t lastRxCr = 0;
    uint8_t rxCrHist[4] = {};
    float rxCrEwma = 0.0f;
    float lastSnr = 0.0f;
    int32_t lastRssi = 0;
    uint32_t lastSeenMsec = 0;
    uint8_t lastTxCr = 0;
    uint16_t txAttemptsByCr[4] = {};
    uint16_t txSuccessByCr[4] = {};
    uint16_t txFailByCr[4] = {};
    uint8_t capabilityFlags = 0;
};

struct DcrCounters {
    uint32_t txCr[4] = {};
    uint32_t rxCr[4] = {};
    uint32_t rxCrUnknown = 0;
    uint32_t txAirtimeMsByCr[4] = {};
    uint32_t ackSuccessByCr[4] = {};
    uint32_t ackFailByCr[4] = {};
    uint32_t retryEscalations = 0;
    uint32_t forcedCompactCongestion = 0;
    uint32_t forcedCompactDutyCycle = 0;
    uint32_t forcedCompactTokenBucket = 0;
    uint32_t forcedCompactAirtimeCap = 0;
    uint32_t relayLateRobust = 0;
};

/**
 * Central policy brain for choices that trade airtime against reliability.
 *
 * DynamicCodingRate is intentionally implemented here instead of in telemetry,
 * routing, or individual radio backends: those subsystems all want airtime, and
 * one shared policy is the only way to make CR8 budget, congestion pressure,
 * retry context, and relay context agree with each other.
 */
class AirtimePolicy
{
  public:
    DcrSettings settingsFromConfig(const meshtastic_Config_LoRaConfig &loraConfig) const;

    void rememberPacketClass(const meshtastic_MeshPacket &packet, uint32_t nowMsec);
    DcrPacketClass classifyPacket(const meshtastic_MeshPacket &packet, meshtastic_PortNum *portnum, bool *known) const;

    DcrDecision choose(const DcrPacketContext &packet, const ChannelAirtimeStats &channel, const DcrSettings &settings,
                       uint32_t (*airtimeForCr)(uint32_t packetLen, uint8_t cr, void *context), void *airtimeContext);

    void observeRx(const DcrRxObservation &obs, bool trackNeighborCr);
    void observeTxStart(const meshtastic_MeshPacket &packet, uint8_t cr, uint32_t airtimeMs, bool urgent, uint32_t nowMsec);
    void observeTxResult(const DcrTxResult &result);

    void noteRetransmission(const meshtastic_MeshPacket &packet, uint8_t attempt, bool finalRetry, bool quietLoss);
    DcrRetryContext getRetryContext(const meshtastic_MeshPacket &packet) const;
    uint8_t getLastTxCr(NodeNum from, PacketId id) const;

    const DcrCounters &getCounters() const { return counters; }
    const NeighborCrStats *getNeighborStats(NodeNum nodeNum) const;

    static uint8_t clampCr(uint8_t cr, uint8_t fallback = DCR_CR_SLIM);
    static uint8_t crIndex(uint8_t cr);

  private:
    struct PacketClassCache {
        // Local-origin decoded packets are classified before encryption, then
        // looked up again immediately before TX. This avoids adding policy-only
        // fields to MeshPacket or inspecting encrypted payloads later.
        const meshtastic_MeshPacket *packetPtr = nullptr;
        NodeNum from = 0;
        PacketId id = 0;
        meshtastic_PortNum portnum = meshtastic_PortNum_UNKNOWN_APP;
        DcrPacketClass packetClass = DcrPacketClass::Normal;
        meshtastic_MeshPacket_Priority priority = meshtastic_MeshPacket_Priority_UNSET;
        uint32_t updatedMsec = 0;
        bool classKnown = false;
    };

    struct RetryCache {
        // Retransmission state is keyed by packet identity because DCR chooses
        // CR at the radio layer, later than the router's retry scheduling.
        NodeNum from = 0;
        PacketId id = 0;
        uint8_t attempt = 0;
        bool finalRetry = false;
        bool quietLoss = false;
    };

    struct TxCache {
        // ACK/NAK/timeout accounting happens after TX, so remember the actual
        // CR selected for the packet rather than assuming the static/base CR.
        NodeNum from = 0;
        NodeNum to = 0;
        PacketId id = 0;
        uint8_t cr = 0;
    };

    static constexpr size_t PACKET_CLASS_CACHE_SIZE = 24;
    static constexpr size_t RETRY_CACHE_SIZE = 16;
    static constexpr size_t TX_CACHE_SIZE = 16;
    static constexpr size_t NEIGHBOR_CR_SLOTS = 32;
    static constexpr uint32_t ROBUST_WINDOW_MSEC = 5 * 60 * 1000;

    PacketClassCache classCache[PACKET_CLASS_CACHE_SIZE] = {};
    RetryCache retryCache[RETRY_CACHE_SIZE] = {};
    TxCache txCache[TX_CACHE_SIZE] = {};
    NeighborCrStats neighborStats[NEIGHBOR_CR_SLOTS] = {};
    DcrCounters counters = {};

    uint32_t robustWindowStartMsec = 0;
    uint32_t robustTotalAirtimeMs = 0;
    uint32_t robustRescueAirtimeMs = 0;
    size_t nextClassCache = 0;
    size_t nextRetryCache = 0;
    size_t nextTxCache = 0;

    const PacketClassCache *findPacketClass(const meshtastic_MeshPacket &packet) const;
    NeighborCrStats *getOrCreateNeighborStats(NodeNum nodeNum);
    NodeNum resolveRelayNode(uint8_t relayNode) const;
    void rotateRobustWindow(uint32_t nowMsec);
    bool robustTokenAllows(uint32_t predictedAirtimeMs, bool urgent, uint8_t robustAirtimePct, uint32_t nowMsec);
};

extern AirtimePolicy *airtimePolicy;
