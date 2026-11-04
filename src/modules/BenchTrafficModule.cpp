#include "BenchTrafficModule.h"

#ifdef BENCH_TRAFFIC_LOAD_PCT

#include "MeshService.h"
#include "RadioInterface.h"
#include "Router.h"
#include "Throttle.h"
#include "airtime.h"
#include "configuration.h"
#include <math.h>
#include <string.h>

// Payload length range in bytes, drawn uniformly per frame
#ifndef BENCH_TRAFFIC_MIN_LEN
#define BENCH_TRAFFIC_MIN_LEN 16
#endif
#ifndef BENCH_TRAFFIC_MAX_LEN
#define BENCH_TRAFFIC_MAX_LEN 200
#endif
// First frame this long after boot, so the node has its config before it starts
#ifndef BENCH_TRAFFIC_START_DELAY_MS
#define BENCH_TRAFFIC_START_DELAY_MS 30000
#endif
#ifndef BENCH_TRAFFIC_SUMMARY_MS
#define BENCH_TRAFFIC_SUMMARY_MS 60000
#endif
// Stop after this many frames have been sent; 0 runs until reboot
#ifndef BENCH_TRAFFIC_COUNT
#define BENCH_TRAFFIC_COUNT 0
#endif
// Unset: exponential gaps (Poisson arrivals). Set to j: each gap is the mean +- up to j%, drawn uniformly.
#ifdef BENCH_TRAFFIC_JITTER_PCT
static_assert(BENCH_TRAFFIC_JITTER_PCT >= 0 && BENCH_TRAFFIC_JITTER_PCT <= 100, "BENCH_TRAFFIC_JITTER_PCT is 0 to 100");
#endif

static_assert(BENCH_TRAFFIC_LOAD_PCT >= 0 && BENCH_TRAFFIC_LOAD_PCT <= 50, "BENCH_TRAFFIC_LOAD_PCT is 0 to 50");
static_assert(BENCH_TRAFFIC_MIN_LEN >= 4 && BENCH_TRAFFIC_MIN_LEN <= BENCH_TRAFFIC_MAX_LEN, "length range");
static_assert(BENCH_TRAFFIC_MAX_LEN <= sizeof(meshtastic_Data_payload_t::bytes), "BENCH_TRAFFIC_MAX_LEN exceeds a payload");

static constexpr uint32_t IDLE_POLL_MS = 60000;

BenchTrafficModule *benchTrafficModule;

BenchTrafficModule::BenchTrafficModule()
    : SinglePortModule("BenchTraffic", meshtastic_PortNum_PRIVATE_APP), concurrency::OSThread("BenchTraffic")
{
    LOG_INFO("BenchTraffic: %u%% of airtime, %u-%u byte broadcasts, hop limit 0, from %u ms after boot",
             (unsigned)BENCH_TRAFFIC_LOAD_PCT, (unsigned)BENCH_TRAFFIC_MIN_LEN, (unsigned)BENCH_TRAFFIC_MAX_LEN,
             (unsigned)BENCH_TRAFFIC_START_DELAY_MS);
#ifdef BENCH_TRAFFIC_JITTER_PCT
    LOG_INFO("BenchTraffic: gaps uniform, mean +-%u%%, %u frames", (unsigned)BENCH_TRAFFIC_JITTER_PCT,
             (unsigned)BENCH_TRAFFIC_COUNT);
#else
    LOG_INFO("BenchTraffic: gaps exponential, %u frames", (unsigned)BENCH_TRAFFIC_COUNT);
#endif
    setIntervalFromNow(BENCH_TRAFFIC_START_DELAY_MS);
}

uint32_t BenchTrafficModule::exponentialMs(uint32_t meanMs)
{
    // u in (0, 1]: never 0, so the log stays finite; the longest draw is about 11 means
    const float u = (float)random(1, 65537) / 65536.0f;
    return (uint32_t)(-logf(u) * (float)meanMs + 0.5f);
}

uint32_t BenchTrafficModule::gapMs(uint32_t meanMs)
{
#ifdef BENCH_TRAFFIC_JITTER_PCT
    const int32_t span = (int32_t)((uint64_t)meanMs * BENCH_TRAFFIC_JITTER_PCT / 100);
    const int32_t gap = (int32_t)meanMs + (span ? (int32_t)random(-span, span + 1) : 0);
    return gap > 0 ? (uint32_t)gap : 0;
#else
    return exponentialMs(meanMs);
#endif
}

ProcessMessage BenchTrafficModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (isFromUs(&mp) || mp.decoded.payload.size < sizeof(uint32_t))
        return ProcessMessage::CONTINUE;
    uint32_t seq;
    memcpy(&seq, mp.decoded.payload.bytes, sizeof(seq));
    heard++;
    LOG_INFO("BenchTraffic: rx from=0x%08x n=%u id=0x%08x len=%u snr=%g rssi=%d", (unsigned)mp.from, (unsigned)seq,
             (unsigned)mp.id, (unsigned)mp.decoded.payload.size, mp.rx_snr, (int)mp.rx_rssi);
    return ProcessMessage::CONTINUE;
}

void BenchTrafficModule::logSummary(uint32_t now, const char *what)
{
    const uint32_t elapsed = now - firstSendMs;
    LOG_INFO("BenchTraffic: %s: %u sent, %u rejected, %u skipped for duty cycle, %u heard, %u ms offered in %u ms (%u.%u%%), "
             "send blocked %u ms/frame, started late %u ms/frame, %u reanchors",
             what, (unsigned)sent, (unsigned)rejected, (unsigned)skippedDutyCycle, (unsigned)heard, (unsigned)offeredAirMs,
             (unsigned)elapsed, (unsigned)(elapsed ? offeredAirMs * 100ULL / elapsed : 0),
             (unsigned)(elapsed ? offeredAirMs * 1000ULL / elapsed % 10 : 0), (unsigned)(sent ? sendBlockedMs / sent : 0),
             (unsigned)(sent ? lateMs / sent : 0), (unsigned)reanchors);
}

uint32_t BenchTrafficModule::sendOne()
{
    RadioInterface *radio = router ? router->getRadioIface() : nullptr;
    if (!radio)
        return 0;
    // The duty-cycle limit applies to this traffic like any other; the bench never raises it
    if (!airTime->isTxAllowedAirUtil()) {
        skippedDutyCycle++;
        return 0;
    }
    meshtastic_MeshPacket *p = allocDataPacket();
    if (!p)
        return 0;
    const uint32_t len = (uint32_t)random(BENCH_TRAFFIC_MIN_LEN, BENCH_TRAFFIC_MAX_LEN + 1);
    const uint32_t seq = sent + rejected + 1; // a rejected frame keeps its number, so a gap in rx seqs is not all loss
    memcpy(p->decoded.payload.bytes, &seq, sizeof(seq));
    // Random fill, never uniform: the bench reads a uniform-fill frame as a failed SPI read
    for (uint32_t i = sizeof(seq); i < len; i++)
        p->decoded.payload.bytes[i] = (uint8_t)random(0, 256);
    p->decoded.payload.size = len;
    p->to = NODENUM_BROADCAST;
    p->hop_limit = 0;
    p->hop_start = 0;
    p->channel = 0;
    p->want_ack = false;
    p->priority = meshtastic_MeshPacket_Priority_DEFAULT;
    const uint32_t airMs = radio->getPacketTime(p);
    const PacketId id = p->id;
    const ErrorCode res = service->sendToMesh(p);
    if (res != ERRNO_OK && res != ERRNO_SHOULD_RELEASE) {
        rejected++;
        LOG_WARN("BenchTraffic: tx n=%u id=0x%08x refused, err=%d", (unsigned)seq, (unsigned)id, (int)res);
        return airMs; // still paces the schedule, so a full queue does not turn into a burst
    }
    sent++;
    offeredAirMs += airMs;
    LOG_INFO("BenchTraffic: tx n=%u id=0x%08x len=%u air=%u ms", (unsigned)seq, (unsigned)id, (unsigned)len, (unsigned)airMs);
    return airMs;
}

int32_t BenchTrafficModule::runOnce()
{
#if BENCH_TRAFFIC_LOAD_PCT == 0
    return IDLE_POLL_MS; // the load-0 arm: the same image and node on the air, sending nothing of its own
#else
    uint32_t now = millis();
    if (done)
        return IDLE_POLL_MS;
    if (BENCH_TRAFFIC_COUNT && sent + rejected >= BENCH_TRAFFIC_COUNT) {
        done = true;
        logSummary(now, "done");
        return IDLE_POLL_MS;
    }
    if (!firstSendMs) {
        firstSendMs = now ? now : 1;
        lastSummaryMs = firstSendMs;
        nextDueMs = now;
    }
    if (Throttle::deadlinePassedAt(now, nextDueMs))
        lateMs += now - nextDueMs;
    const uint32_t airMs = sendOne();
    const uint32_t after = millis();
    sendBlockedMs += after - now;
    now = after;
    if (!Throttle::isWithinTimespanMs(lastSummaryMs, BENCH_TRAFFIC_SUMMARY_MS)) {
        lastSummaryMs = now;
        logSummary(now, "summary");
    }
    // Mean gap = this frame's airtime / load, so the offered fraction averages BENCH_TRAFFIC_LOAD_PCT
    const uint32_t meanMs = (airMs ? airMs : 50) * 100 / BENCH_TRAFFIC_LOAD_PCT;
    nextDueMs += gapMs(meanMs);
    // Behind schedule: send at once, but past a few mean gaps restart from now rather than burst to catch up
    if (Throttle::deadlinePassedAt(now, nextDueMs + 4 * meanMs)) {
        reanchors++;
        nextDueMs = now;
    }
    return Throttle::deadlinePassedAt(now, nextDueMs) ? 1 : (int32_t)(nextDueMs - now);
#endif
}

#endif
