#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(HAS_RAKHUB)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "RAKSensorHub.h"
#include "TelemetrySensor.h"
#include "UptimeClock.h"
#include "concurrency/LockGuard.h"
#include "concurrency/Periodic.h"
#include <RAK-OneWireSerial.h>
#include <Throttle.h>
#include <onewire_master_protocol.h> // for DELIMTER/WAKEUPBYTE and frame layout

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>

// Uplink module (RAKSensorHubUplink.cpp): IPSO parse + metrics cache.
namespace RAKSensorHubUplink
{
bool handleIpsoEvent(const char *via, uint8_t pid, uint8_t sid, uint8_t *msg, uint16_t len);
void setProbeSensors(uint8_t pid, const uint8_t *ipso, uint16_t n);
bool getMetrics(meshtastic_Telemetry *measurement);
uint16_t getBusVoltageMv();
int16_t getCurrentMa();
int getBusBatteryPercent();
bool isCharging();
} // namespace RAKSensorHubUplink

using namespace concurrency;

/** Construct RAK 1-Wire sensor hub (type SENSOR_UNSET, name "RAKSensorHub");
 * actual init in runOnce(). */
RAKSensorHub::RAKSensorHub() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "RAKSensorHub") {}

RAKSensorHub rakSensorHub;

static Periodic *onewireRxPeriodic;
static Periodic *onewirePollPeriodic;

// Half-duplex on UARTE (RXD = TXD = HALF_UART_PIN); pin is an output only while sending.
static Uart &hubSerial = Serial1;
static Lock onewireLock;

static uint8_t buff[0x200];
static uint16_t bufflen = 0;

// ----- Link timing -----
static constexpr uint32_t RSP_TIMEOUT_MS = 2000;                 // give up on an outstanding poll
static constexpr uint32_t DISCOVERY_RSP_TIMEOUT_MS = 1200;       // probes answer in ~600 ms
static constexpr uint32_t DISCOVERY_INTERVAL_MS = 2000;          // PID sweep step while empty
static constexpr uint32_t BOOT_RESCAN_EMPTY_MS = 60 * 1000;      // BOOT while no probe known
static constexpr uint32_t BOOT_RESCAN_KNOWN_MS = 15 * 60 * 1000; // BOOT for late probes
static constexpr uint8_t MAX_MISSED_POLLS = 3;                   // then forget and rediscover
static constexpr uint32_t TX_SETTLE_MS = 2;                      // last stop bit + echo after ENDTX

// RAK probe ID range
static constexpr uint8_t PROBE_PID_MIN = 1;
static constexpr uint8_t PROBE_PID_MAX = 250;

// Frame offsets in buff (starts at 0x7E, no wakeup byte); checksum at [5 + length]
static constexpr uint16_t FRM_TYPE = 3, FRM_FLAG = 4, FRM_DEST = 5, FRM_SRC = 6, FRM_HUBTYPE = 8, FRM_PLDTYPE = 10;
static constexpr uint16_t FRM_RUI3_PAYLOAD = 5;
// SNHub_Api_Provision_t fields, from [11]
static constexpr uint16_t PROV_SN = 15, PROV_SN_LEN = 18, PROV_ID = 33, PROV_MODEL = 41, PROV_MODEL_LEN = 20;
static constexpr uint16_t PROV_SNSR_NUM = 65, PROV_SNSR = 66;

// ----- Probe and polling state -----
struct ProbeState {
    uint32_t lastPollMs = 0;
    bool polled = false;                // false: poll on next tick
    uint8_t missed = 0;                 // consecutive unanswered polls
    uint32_t retryMs = 0;               // retry delay after a miss, 0 = regular interval
    std::map<uint8_t, uint8_t> sidIpso; // SID -> IPSO, from provisioning or first data
    std::set<uint8_t> paramPending;     // SIDs to query interval/rule
};
static std::map<uint8_t, ProbeState> probes;                      // active probes by PID
static std::map<uint8_t, std::array<uint8_t, PROV_SN_LEN>> pidSn; // assigned PIDs by serial

static uint32_t last_tx_time = 0;        // Last frame sent
static uint32_t last_rx_time = 0;        // Last byte received
static uint32_t last_err_time = 0;       // Last checksum/sequence error
static uint32_t last_discovery_time = 0; // Last PID sweep request
static uint32_t last_boot_time = 0;      // Last provision BOOT broadcast
static uint8_t discovery_pid = PROBE_PID_MIN;

// ----- Request/response state (half-duplex: only one outstanding request) -----
static bool awaiting_rsp = false;
static uint32_t awaiting_rsp_since = 0;
static uint32_t awaiting_rsp_timeout = RSP_TIMEOUT_MS;
static bool last_sent_pid_valid = false;
static uint8_t last_sent_pid = 0;

// ----- Frame parse state (safe resync on checksum/seq error) -----
static bool processing_frame = false;
static bool frame_error_during_process = false;
static bool frame_ipso_desync = false; // rest of the current frame cannot be decoded

static uint32_t status_delta = 0; // Last time we logged status

// ProbeIO core-1.2.27: DI sensor slot base (snsr_id = 15 + ch - 1 for IOC_DI
// ch>=1).
static constexpr uint8_t RAKHUB_PROBE_DI_SNSR_BASE = 15;

static void hubSerialBegin()
{
    pinMode(HALF_UART_PIN, INPUT_PULLUP);
    hubSerial.setPins(HALF_UART_PIN, HALF_UART_PIN);
    hubSerial.begin(9600);
}

/** Drive the shared pin only for the duration of the frame, then drop our own echo. */
static void hubSerialWrite(const uint8_t *msg, uint16_t len)
{
    NRF_GPIO_Type *port = digitalPinToPort(HALF_UART_PIN);
    const uint32_t mask = digitalPinToBitMask(HALF_UART_PIN);
    port->DIRSET = mask;
    hubSerial.write(msg, len);
    hubSerial.flush(); // waits on ENDTX semaphore, other tasks run
    delay(TX_SETTLE_MS);
    port->DIRCLR = mask;
    while (hubSerial.available())
        hubSerial.read();
    last_tx_time = millis();
}

static void logDiscard(const char *why)
{
    char hex[3 * 16 + 1];
    const uint16_t n = bufflen < 16 ? bufflen : 16;
    for (uint16_t i = 0; i < n; i++)
        snprintf(&hex[i * 3], 4, "%02x ", buff[i]);
    hex[n * 3] = 0;
    LOG_DEBUG("RAKSensorHub: drop %u bytes (%s): %s", (unsigned)bufflen, why, hex);
}

static inline bool isProbePid(uint8_t pid)
{
    return pid >= PROBE_PID_MIN && pid <= PROBE_PID_MAX;
}

static ProbeState *addProbe(uint8_t pid, const char *how)
{
    if (!isProbePid(pid))
        return nullptr;
    auto it = probes.find(pid);
    if (it != probes.end())
        return &it->second;
    LOG_INFO("RAKSensorHub: probe PID[%02x] added (%s)", pid, how);
    if (!pidSn.count(pid))
        pidSn[pid].fill(0); // reserve until it provisions
    return &probes[pid];
}

static void noteSid(ProbeState &st, uint8_t sid, uint8_t ipso)
{
    if (st.sidIpso.emplace(sid, ipso).second)
        st.paramPending.insert(sid);
}

/** IPSO types the RAK library can size; any other type desynchronises the rest of its frame. */
static bool libraryKnowsIpso(uint8_t ipso)
{
    switch (ipso) {
    case RAK_IPSO_DIGITAL_INPUT:
    case RAK_IPSO_DIGITAL_OUTPUT:
    case RAK_IPSO_ANALOG_INPUT:
    case RAK_IPSO_ANALOG_OUT:
    case RAK_IPSO_NITROGEN:
    case RAK_IPSO_PHOSPHORUS:
    case RAK_IPSO_POTASSIUM:
    case RAK_IPSO_SALINITY:
    case RAK_IPSO_DISS_OXYGEN:
    case RAK_IPSO_ORP:
    case RAK_IPSO_COD:
    case RAK_IPSO_TURBIDITY:
    case RAK_IPSO_NO3:
    case RAK_IPSO_NH4PLUS:
    case RAK_IPSO_BOD:
    case RAK_IPSO_ILLUM_SENSOR:
    case RAK_IPSO_PRESENCE_SENSOR:
    case RAK_IPSO_TEMP_SENSOR:
    case RAK_IPSO_HUMIDITY_SENSOR:
    case RAK_IPSO_GAS:
    case RAK_IPSO_HP_HUMIDITY:
    case RAK_IPSO_ACCELEROMETER:
    case RAK_IPSO_BAROMETER:
    case RAK_IPSO_BATTERVALUE:
    case RAK_IPSO_PRECIPITATION:
    case RAK_IPSO_GASPERCENTAGE:
    case RAK_IPSO_CO2:
    case RAK_IPSO_SSN:
    case RAK_IPSO_HP_EC:
    case RAK_IPSO_DISTANCE:
    case RAK_IPSO_GYROMETER:
    case RAK_IPSO_GPS_LOCAL:
    case RAK_IPSO_GNSS_ENHANCED:
    case RAK_IPSO_VOC:
    case RAK_IPSO_GUSTWINDSPEED:
    case RAK_IPSO_STRIKES:
    case RAK_IPSO_CAPACITY:
    case RAK_IPSO_DC_CURRENT:
    case RAK_IPSO_DC_VOLTAGE:
    case RAK_IPSO_MOISTURE:
    case RAK_IPSO_WIND:
    case RAK_IPSO_WIND_DIR:
    case RAK_IPSO_EC:
    case RAK_IPSO_HP_PH:
    case RAK_IPSO_PH:
    case RAK_IPSO_PYRANOMETER:
    case RAK_IPSO_PM10:
    case RAK_IPSO_PM25:
    case RAK_IPSO_XYORIENTATION:
    case RAK_IPSO_NOISE:
    case RAK_IPSO_MODBUS:
    case RAK_IPSO_SDI12:
    case RAK_IPSO_BINARY2BYTE:
    case RAK_IPSO_BINARY4BYTE:
    case RAK_IPSO_FLOAT_IEEE754:
    case RAK_IPSO_INTEGER32:
    case RAK_IPSO_UINTEGER32:
    case RAK_IPSO_BINARYTLV:
        return true;
    default:
        return false;
    }
}

static void sendRequest(uint8_t pid, uint32_t now, uint32_t timeoutMs)
{
    last_sent_pid = pid;
    last_sent_pid_valid = true;
    awaiting_rsp = true;
    awaiting_rsp_since = now;
    awaiting_rsp_timeout = timeoutMs;
}

/** Unanswered request: count a miss against a known probe, forget it after MAX_MISSED_POLLS. */
static void onRequestTimeout(uint32_t now)
{
    awaiting_rsp = false;
    last_err_time = now;
    if (!last_sent_pid_valid)
        return;
    auto it = probes.find(last_sent_pid);
    if (it == probes.end())
        return;
    if (++it->second.missed >= MAX_MISSED_POLLS) {
        LOG_WARN("RAKSensorHub: probe PID[%02x] silent for %u polls, rediscovering", last_sent_pid, (unsigned)MAX_MISSED_POLLS);
        probes.erase(it);
        last_boot_time = 0; // re-broadcast BOOT right away
    } else {
        it->second.retryMs = it->second.missed == 1 ? 3000 : 8000; // probes can stall for several seconds
    }
}

/** RUI3 checksum: popcount of type, flag and payload. */
static uint8_t frameChecksum(const uint8_t *frm, uint16_t payloadLen)
{
    uint8_t sum = __builtin_popcount(frm[FRM_TYPE]) + __builtin_popcount(frm[FRM_FLAG]);
    for (uint16_t i = 0; i < payloadLen; i++)
        sum += __builtin_popcount(frm[FRM_RUI3_PAYLOAD + i]);
    return sum;
}

/** Known serial keeps its PID, else the requested PID if free, else the lowest free. */
/** True if a serial-unknown PID can be this joining probe: dropped, or its seen SID/IPSO pairs match the declared list. */
static bool sameSensors(uint8_t pid, const uint8_t *snsr, uint8_t count)
{
    auto it = probes.find(pid);
    if (it == probes.end())
        return true;
    if (it->second.sidIpso.empty())
        return false;
    for (const auto &[sid, ipso] : it->second.sidIpso) {
        bool found = false;
        for (uint8_t i = 0; i < count && !found; i++)
            found = snsr[4 * i] == sid && snsr[4 * i + 1] == ipso;
        if (!found)
            return false;
    }
    return true;
}

static uint8_t assignPid(const uint8_t *sn, uint8_t requested, const uint8_t *snsr, uint8_t count)
{
    for (const auto &[pid, known] : pidSn)
        if (memcmp(known.data(), sn, PROV_SN_LEN) == 0)
            return pid;
    static const std::array<uint8_t, PROV_SN_LEN> unknownSn{};
    uint8_t pid = 0;
    // PID seen on the bus before provisioning (serial unknown) goes to whoever asks for it
    if (isProbePid(requested) && (!pidSn.count(requested) || pidSn[requested] == unknownSn)) {
        pid = requested;
    } else {
        // Reclaim a serial-unknown PID first (a probe that answered before joining), then the lowest free one
        for (uint8_t p = PROBE_PID_MIN; p <= PROBE_PID_MAX && pid == 0; p++)
            if (pidSn.count(p) && pidSn[p] == unknownSn && sameSensors(p, snsr, count))
                pid = p;
        for (uint8_t p = PROBE_PID_MIN; p <= PROBE_PID_MAX && pid == 0; p++)
            if (!pidSn.count(p))
                pid = p;
    }
    if (pid != 0)
        memcpy(pidSn[pid].data(), sn, PROV_SN_LEN);
    return pid;
}

/** Answer a probe provision request here; the RAK library handles only one probe (REC_NUM=1). */
static bool handleProvisionRequest(uint16_t total, uint16_t payloadLen)
{
    if (buff[FRM_TYPE] != RUI3API_TYPE_SENSORHUB || buff[FRM_FLAG] != RUI3API_FLG_REQ ||
        buff[FRM_HUBTYPE] != SNHUB_TYPE_PROVISION)
        return false;
    if (buff[FRM_PLDTYPE] != PLD_PROVI_TYPE_VER3 || total <= PROV_SNSR) {
        LOG_WARN("RAKSensorHub: unsupported provision request (type %u, len %u)", (unsigned)buff[FRM_PLDTYPE], (unsigned)total);
        return true;
    }
    if (frameChecksum(buff, payloadLen) != buff[FRM_RUI3_PAYLOAD + payloadLen]) {
        LOG_INFO("+ERR:CHKSUM (provision)");
        last_err_time = millis();
        return true;
    }

    const uint8_t requested = buff[PROV_ID];
    uint8_t sensors = buff[PROV_SNSR_NUM];
    while (sensors > 0 && PROV_SNSR + 4u * sensors - 1 >= (uint32_t)FRM_RUI3_PAYLOAD + payloadLen)
        sensors--; // entries must end before the checksum
    const uint8_t pid = assignPid(&buff[PROV_SN], requested, &buff[PROV_SNSR], sensors);
    if (pid == 0) {
        LOG_WARN("RAKSensorHub: no free probe PID, ignoring provision request");
        return true;
    }

    static uint8_t tx[sizeof(buff) + 1];
    tx[0] = WAKEUPBYTE;
    memcpy(&tx[1], buff, total);
    uint8_t *rsp = &tx[1];
    rsp[FRM_FLAG] = RUI3API_FLG_RSP;
    rsp[FRM_DEST] = buff[FRM_SRC];
    rsp[FRM_SRC] = buff[FRM_DEST];
    rsp[PROV_ID] = pid;
    rsp[FRM_RUI3_PAYLOAD + payloadLen] = frameChecksum(rsp, payloadLen);
    hubSerialWrite(tx, total + 1);

    char model[PROV_MODEL_LEN + 1];
    for (uint16_t i = 0; i < PROV_MODEL_LEN; i++) {
        const char c = (char)buff[PROV_MODEL + i];
        model[i] = (c >= 0x20 && c < 0x7F) ? c : 0;
    }
    model[PROV_MODEL_LEN] = 0;
    LOG_INFO("RAKSensorHub: provision '%s' fw %u.%u.%u sn %02x%02x%02x%02x.. asked PID[%02x] -> PID[%02x], %u sensors", model,
             (unsigned)buff[12], (unsigned)buff[13], (unsigned)buff[14], buff[PROV_SN], buff[PROV_SN + 1], buff[PROV_SN + 2],
             buff[PROV_SN + 3], (unsigned)requested, (unsigned)pid, (unsigned)sensors);

    ProbeState *st = addProbe(pid, "provision");
    if (st) {
        st->sidIpso.clear();
        st->paramPending.clear();
    }
    uint8_t ipsoList[UINT8_MAX];
    for (uint16_t i = 0; i < sensors; i++) {
        const uint8_t *s = &buff[PROV_SNSR + 4 * i];
        ipsoList[i] = s[1];
        LOG_INFO("RAKSensorHub: PID[%02x] SID[%02x] IPSO[%02x] rule=0x%04x%s", (unsigned)pid, (unsigned)s[0], (unsigned)s[1],
                 (unsigned)(s[2] | (s[3] << 8)), s[0] == RAKHUB_PROBE_DI_SNSR_BASE ? " (ProbeIO DI slot)" : "");
        if (st) {
            noteSid(*st, s[0], s[1]);
            st->paramPending.insert(s[0]); // sensor probes stall ~5 s about 30 s after a join without it
        }
    }
    RAKSensorHubUplink::setProbeSensors(pid, ipsoList, sensors);
    if (st) {
        st->polled = false; // first data right after joining
        st->missed = 0;
        st->retryMs = 0;
    }
    return true;
}

/** RAK protocol event callback. */
static void onewire_evt(const uint8_t pid, const uint8_t sid, const SNHUBAPI_EVT_E eid, uint8_t *msg, uint16_t len)
{
    switch (eid) {
    case SNHUBAPI_EVT_RECV_REQ:
        // Probe-initiated REPORT: adopt unknown PIDs
        LOG_DEBUG("+EVT:PID[%02x],REQ", pid);
        addProbe(pid, "unsolicited report");
        break;
    case SNHUBAPI_EVT_RECV_RSP: {
        LOG_DEBUG("+EVT:PID[%02x],RSP", pid);
        ProbeState *st = addProbe(pid, (last_sent_pid_valid && pid == last_sent_pid) ? "discovery response" : "response");
        if (st) {
            st->missed = 0;
            st->retryMs = 0;
        }
        if (last_sent_pid_valid && pid == last_sent_pid)
            awaiting_rsp = false;
        break;
    }

    case SNHUBAPI_EVT_QSEND:
        hubSerialWrite(msg, len);
        LOG_DEBUG("RAKSensorHub: TX %u bytes (PID=0x%02x)", (unsigned)len, pid);
        break;

    case SNHUBAPI_EVT_ADD_SID:
    case SNHUBAPI_EVT_ADD_PID:
        break; // handled in handleProvisionRequest()

    case SNHUBAPI_EVT_GET_INTV:
        if (len >= 4) {
            const uint32_t intv =
                (uint32_t)msg[0] | ((uint32_t)msg[1] << 8) | ((uint32_t)msg[2] << 16) | ((uint32_t)msg[3] << 24);
            LOG_INFO("RAKSensorHub: PID[%02x] SID[%02x] sensor interval %lu s", pid, sid, (unsigned long)intv);
        }
        break;

    case SNHUBAPI_EVT_GET_ENABLE:
        LOG_INFO("RAKSensorHub: PID[%02x] SID[%02x] periodic rule %s", pid, sid, msg[0] ? "on" : "off");
        break;

    case SNHUBAPI_EVT_SDATA_REQ:
    case SNHUBAPI_EVT_REPORT: {
        const char *via = (eid == SNHUBAPI_EVT_SDATA_REQ) ? "SDATA" : "REPORT";
        if (frame_ipso_desync || len == 0)
            break;
        ProbeState *st = addProbe(pid, via);
        if (!libraryKnowsIpso(msg[0]) || (st && st->sidIpso.count(sid) && st->sidIpso[sid] != msg[0])) {
            LOG_WARN("RAKSensorHub: PID[%02x] SID[%02x] IPSO[%02x] not decodable, rest of frame skipped", pid, sid, msg[0]);
            frame_ipso_desync = true;
            break;
        }
        if (st)
            noteSid(*st, sid, msg[0]);
        if (RAKSensorHubUplink::handleIpsoEvent(via, pid, sid, msg, len))
            rakSensorHub.setLastRead(millis());
        break;
    }

    case SNHUBAPI_EVT_CHKSUM_ERR:
        LOG_INFO("+ERR:CHKSUM");
        // The RX handler will drop the frame it just fed to process().
        // Touching bufflen here can cause uint16 underflow in the RX handler and
        // lead to infinite re-processing of the same corrupted frame.
        if (processing_frame) {
            frame_error_during_process = true;
        }
        last_err_time = millis();
        break;

    case SNHUBAPI_EVT_SEQ_ERR:
        LOG_INFO("+ERR:SEQUCE");
        if (processing_frame) {
            frame_error_during_process = true;
        }
        last_err_time = millis();
        break;

    default:
        break;
    }
}

/** Drain the UART and hand complete frames to the protocol. */
static int32_t onewireRxHandle()
{
    const uint32_t now = millis();
    concurrency::LockGuard guard(&onewireLock);

    while (hubSerial.available()) {
        const uint8_t a = (uint8_t)hubSerial.read();
        if (bufflen < sizeof(buff)) {
            buff[bufflen++] = a;
        } else {
            logDiscard("overflow");
            bufflen = 0;
        }
        last_rx_time = millis();
    }

    // Assemble complete frames using the declared payload length.
    while (bufflen > 0) {
        // Align to delimiter (0x7E). Protocol code will recover WAKEUPBYTE itself.
        uint16_t delim_idx = 0;
        while (delim_idx < bufflen && buff[delim_idx] != DELIMTER) {
            delim_idx++;
        }

        if (delim_idx >= bufflen) {
            // No delimiter yet; if we have been idle too long, drop noise.
            if (!Throttle::isWithinTimespanMs(last_rx_time, 50)) {
                logDiscard("no delimiter");
                bufflen = 0;
            }
            break;
        }

        if (delim_idx > 0) {
            memmove(buff, buff + delim_idx, bufflen - delim_idx);
            bufflen -= delim_idx;
            continue;
        }

        // Need at least: start + len(2) + type + flag
        if (bufflen < 5) {
            break;
        }

        // Length is lbyte/hbyte as (lbyte<<8)+hbyte; use 32-bit so 6+payload_len
        // cannot wrap to 0.
        const uint16_t payload_len = ((uint16_t)buff[1] << 8) | buff[2];
        const uint32_t total_needed = 6u + (uint32_t)payload_len; // start+len+type+flag+payload+checksum

        if (payload_len == 0 || total_needed > sizeof(buff)) {
            // Bad length -> discard one byte and resync
            memmove(buff, buff + 1, bufflen - 1);
            bufflen -= 1;
            continue;
        }

        if (bufflen < total_needed) {
            // Wait for the rest; a stalled frame is truncated
            if (!Throttle::isWithinTimespanMs(last_rx_time, 50)) {
                logDiscard("truncated frame");
                bufflen = 0;
            }
            break;
        }

        if (total_needed >= 8) {
            LOG_DEBUG("+RX:len=%u %02x %02x %02x %02x %02x %02x %02x %02x", (unsigned)total_needed, (unsigned)buff[0],
                      (unsigned)buff[1], (unsigned)buff[2], (unsigned)buff[3], (unsigned)buff[4], (unsigned)buff[5],
                      (unsigned)buff[6], (unsigned)buff[7]);
        } else {
            LOG_DEBUG("+RX:len=%u", (unsigned)total_needed);
        }
        const uint16_t prev_len = bufflen;
        processing_frame = true;
        frame_error_during_process = false;
        frame_ipso_desync = false;
        if (!handleProvisionRequest((uint16_t)total_needed, payload_len))
            RakSNHub_Protocl_API.process(buff, (uint16_t)total_needed);
        processing_frame = false;

        if (frame_error_during_process) {
            // Robust resync: if checksum/sequence failed, we might have aligned to a
            // delimiter that is not the true frame start (or bytes were corrupted).
            // Drop one byte and search for the next delimiter.
            if (prev_len > 1) {
                memmove(buff, buff + 1, prev_len - 1);
                bufflen = (uint16_t)(prev_len - 1);
            } else {
                bufflen = 0;
            }
            last_err_time = now;
            continue;
        }

        // Success path: drop exactly this frame (total_needed fits in uint16 after
        // the sizeof check).
        const uint16_t remaining = (prev_len > total_needed) ? (uint16_t)(prev_len - (uint16_t)total_needed) : 0;
        if (remaining > 0) {
            memmove(buff, buff + (uint16_t)total_needed, remaining);
        }
        bufflen = remaining;
    }

    // Fast while a frame arrives; 40 ms idle stays within the 64-byte ring
    return Throttle::isWithinTimespanMs(last_rx_time, 200) ? 5 : 40;
}

/** Next PID for the discovery sweep, skipping PIDs that are already active. */
static uint8_t nextDiscoveryPid()
{
    for (uint16_t n = 0; n < PROBE_PID_MAX; n++) {
        const uint8_t pid = discovery_pid;
        discovery_pid = (discovery_pid >= PROBE_PID_MAX) ? PROBE_PID_MIN : (uint8_t)(discovery_pid + 1);
        if (!probes.count(pid))
            return pid;
    }
    return 0;
}

/** One request at a time: BOOT rescan, PID sweep while empty, sensor params once, then data polls. */
static int32_t onewirePollHandle()
{
    const uint32_t now = millis();
    concurrency::LockGuard guard(&onewireLock);

    if (awaiting_rsp) {
        if (Throttle::isWithinTimespanMs(awaiting_rsp_since, awaiting_rsp_timeout))
            return 50;
        onRequestTimeout(now);
    }

    if (!Throttle::isWithinTimespanMs(status_delta, 60 * 1000)) {
        status_delta = now;
        LOG_DEBUG("RAKSensorHub Status: last_tx: %lums ago, last_rx: %lums ago, probes=%u", (unsigned long)(now - last_tx_time),
                  (unsigned long)(now - last_rx_time), (unsigned)probes.size());
    }

    // Half-duplex: no TX while receiving or right after an error
    const bool link_idle = bufflen == 0 && !Throttle::isWithinTimespanMs(last_rx_time, 10) &&
                           !Throttle::isWithinTimespanMs(last_tx_time, 40) && !Throttle::isWithinTimespanMs(last_err_time, 300);
    if (!link_idle)
        return 50;

    const uint32_t bootInterval = probes.empty() ? BOOT_RESCAN_EMPTY_MS : BOOT_RESCAN_KNOWN_MS;
    if (last_boot_time == 0 || !Throttle::isWithinTimespanMs(last_boot_time, bootInterval)) {
        last_boot_time = Time::skipZero(now);
        last_discovery_time = now; // let probes answer the BOOT first
        LOG_INFO("RAKSensorHub: broadcast provision BOOT (%u probes known)", (unsigned)probes.size());
        RakSNHub_Protocl_API.reboot();
        return 500;
    }

    if (probes.empty()) {
        if (!Throttle::isWithinTimespanMs(last_discovery_time, DISCOVERY_INTERVAL_MS)) {
            last_discovery_time = now;
            const uint8_t pid = nextDiscoveryPid();
            if (pid != 0) {
                sendRequest(pid, now, DISCOVERY_RSP_TIMEOUT_MS);
                RakSNHub_Protocl_API.get.data(pid);
                return 100;
            }
        }
        return 200;
    }

    for (auto &[pid, st] : probes) {
        if (st.paramPending.empty())
            continue;
        const uint8_t sid = *st.paramPending.begin();
        st.paramPending.erase(st.paramPending.begin());
        sendRequest(pid, now, RSP_TIMEOUT_MS);
        RakSNHub_Protocl_API.get.param(pid, sid);
        return 100;
    }

    uint32_t next_due_ms = RAKHUB_POLL_INTERVAL_MS;
    for (auto &[pid, st] : probes) {
        const uint32_t interval = st.retryMs ? st.retryMs : RAKHUB_POLL_INTERVAL_MS;
        if (!st.polled || !Throttle::isWithinTimespanMs(st.lastPollMs, interval)) {
            st.polled = true;
            st.lastPollMs = now;
            LOG_DEBUG("RAKSensorHub: poll get.data(PID=0x%02x)", pid);
            sendRequest(pid, now, RSP_TIMEOUT_MS);
            RakSNHub_Protocl_API.get.data(pid);
            return 100;
        }
        const uint32_t remaining = Throttle::remainingMs(st.lastPollMs, interval);
        if (remaining < next_due_ms)
            next_due_ms = remaining;
    }
    return (int32_t)(next_due_ms < 1000 ? next_due_ms + 1 : 1000);
}

/** On first call: init 1-Wire UART, protocol, and RX/Poll Periodics; then just
 * return default read interval. */
int32_t RAKSensorHub::runOnce()
{
    if (!rakSensorHub.isInitialized()) {
        LOG_INFO("RAKSensorHub: Initializing OneWire sensor hub...");

        hubSerialBegin();

        // init() sends the first BOOT
        RakSNHub_Protocl_API.init(onewire_evt);
        last_boot_time = Time::skipZero(millis());
        last_discovery_time = last_boot_time;

        onewireRxPeriodic = new Periodic("onewireRxHandle", onewireRxHandle);
        onewirePollPeriodic = new Periodic("onewirePollHandle", onewirePollHandle);

        status = true;
        initialized = true;
    }

    return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
}

/** Sensor setup placeholder; RAK 1-Wire hub needs no extra config. */
void RAKSensorHub::setup()
{
    // Set up oversampling and filter initialization
}

/** Fill measurement variant from the uplink cache. */
bool RAKSensorHub::getMetrics(meshtastic_Telemetry *measurement)
{
    return RAKSensorHubUplink::getMetrics(measurement);
}

/** Bus voltage in mV from RAK power module (IPSO 0xBA DC_VOLTAGE). */
uint16_t RAKSensorHub::getBusVoltageMv()
{
    return RAKSensorHubUplink::getBusVoltageMv();
}

/** Bus current in mA from RAK power module (IPSO 0xB9 DC_CURRENT). */
int16_t RAKSensorHub::getCurrentMa()
{
    return RAKSensorHubUplink::getCurrentMa();
}

/** Battery capacity 0..100 % from RAK power module (IPSO 0xB8 CAPACITY). */
int RAKSensorHub::getBusBatteryPercent()
{
    return RAKSensorHubUplink::getBusBatteryPercent();
}

/** True if current > 0 (charging). */
bool RAKSensorHub::isCharging()
{
    return RAKSensorHubUplink::isCharging();
}

/** Called from onewire_evt when valid sensor data is received; updates
 * TelemetrySensor lastRead. */
void RAKSensorHub::setLastRead(uint32_t lastRead)
{
    this->lastRead = lastRead;
}

#endif // HAS_RAKHUB
