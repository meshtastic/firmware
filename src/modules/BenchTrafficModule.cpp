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
    setIntervalFromNow(BENCH_TRAFFIC_START_DELAY_MS);
}

uint32_t BenchTrafficModule::exponentialMs(uint32_t meanMs)
{
    // u in (0, 1]: never 0, so the log stays finite; the longest draw is about 11 means
    const float u = (float)random(1, 65537) / 65536.0f;
    return (uint32_t)(-logf(u) * (float)meanMs + 0.5f);
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
    const uint32_t seq = ++sent;
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
    service->sendToMesh(p);
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
        const uint32_t elapsed = now - firstSendMs;
        LOG_INFO("BenchTraffic: %u sent, %u skipped for duty cycle, %u ms offered in %u ms (%u.%u%%), send blocked %u ms/frame, "
                 "started late %u ms/frame, %u reanchors",
                 (unsigned)sent, (unsigned)skippedDutyCycle, (unsigned)offeredAirMs, (unsigned)elapsed,
                 (unsigned)(elapsed ? offeredAirMs * 100ULL / elapsed : 0),
                 (unsigned)(elapsed ? offeredAirMs * 1000ULL / elapsed % 10 : 0), (unsigned)(sent ? sendBlockedMs / sent : 0),
                 (unsigned)(sent ? lateMs / sent : 0), (unsigned)reanchors);
    }
    // Mean gap = this frame's airtime / load, so the offered fraction averages BENCH_TRAFFIC_LOAD_PCT
    const uint32_t meanMs = (airMs ? airMs : 50) * 100 / BENCH_TRAFFIC_LOAD_PCT;
    nextDueMs += exponentialMs(meanMs);
    // Behind schedule: send at once, but past a few mean gaps restart from now rather than burst to catch up
    if (Throttle::deadlinePassedAt(now, nextDueMs + 4 * meanMs)) {
        reanchors++;
        nextDueMs = now;
    }
    return Throttle::deadlinePassedAt(now, nextDueMs) ? 1 : (int32_t)(nextDueMs - now);
#endif
}

#endif
