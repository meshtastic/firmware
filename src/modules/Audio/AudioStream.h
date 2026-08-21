#pragma once

#include "AudioModeSelect.h"
#include "mesh/MeshTypes.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Wire format for codec2 voice.
struct __attribute__((packed)) AudioHeader {
    uint8_t magic;   // AUDIO_MAGIC
    uint8_t mode;    // codec2 mode id, upstream numbering
    uint8_t session; // random per PTT press, separates back-to-back presses
    uint8_t last;    // 1 on the final packet of a transmission
    uint16_t seq;    // little-endian, per session, 0-based
};
static_assert(sizeof(AudioHeader) == AUDIO_HEADER_LEN, "AudioHeader must match AUDIO_HEADER_LEN");

#define AUDIO_MAGIC 0xC2

#define AUDIO_RING_SLOTS 4

// Slots to fill before playout, unless the hold expires first.
#define AUDIO_PRIME_SLOTS 2

// Idle timeout, when `last` never arrives. Scaled to the mode: 5 s is only 1.7
// packet intervals at 450, so one lost packet would end a live stream. It also
// gates our own PTT, so it cannot be generous either.
#define AUDIO_IDLE_MIN_MS 10000
#define AUDIO_IDLE_MAX_MS 30000
#define AUDIO_IDLE_PACKETS 5

// A second sender is queued and played after the first, capped: ~35 s at 450,
// ~13 s at 2400. Past that a queued transmission is too stale to be a reply.
#define AUDIO_QUEUE_BYTES 8192

// Reassembles one voice stream into playout order. One stream only: PTT is half
// duplex. Free of codec2/I2S/Arduino so it can be tested on the host.
class AudioStreamAssembler
{
  public:
    // One packet's payload, header stripped. `conceal` marks a repeat.
    typedef void (*EmitFn)(void *ctx, const uint8_t *frames, size_t len, uint8_t mode, bool conceal);

    void begin(EmitFn fn, void *ctx)
    {
        emit = fn;
        emitCtx = ctx;
        // reset() keeps the queue on purpose, so endStream() can promote it.
        queue.used = 0;
        queue.truncated = false;
        reset();
    }

    void reset()
    {
        active = false;
        priming = false;
        sawLast = false;
        haveLastPayload = false;
        for (int i = 0; i < AUDIO_RING_SLOTS; i++)
            ring[i].used = false;
    }

    bool isActive() const { return active; }

    /// @return false if the packet was not ours to take
    bool accept(NodeNum from, const uint8_t *payload, size_t len, uint32_t nowMs)
    {
        if (len <= sizeof(AudioHeader))
            return false;

        AudioHeader h;
        memcpy(&h, payload, sizeof(h));
        if (h.magic != AUDIO_MAGIC)
            return false;

        const uint8_t *frames = payload + sizeof(h);
        const size_t frameLen = len - sizeof(h);

        if (active && (from != streamFrom || h.session != streamSession)) {
            if (!expired(nowMs)) {
                enqueue(from, h.session, payload, len);
                return true;
            }
            endStream();
        }

        if (!active) {
            active = true;
            priming = true;
            primeStartMs = nowMs;
            streamFrom = from;
            streamSession = h.session;
            streamMode = h.mode;
            playoutSeq = h.seq; // the opening packet is the likeliest loss
        }

        lastRxMs = nowMs;
        if (h.last)
            sawLast = true;

        if (seqBefore(h.seq, playoutSeq)) {
            // While priming, an earlier seq is out-of-order rather than late.
            if (priming)
                playoutSeq = h.seq;
            else
                return true; // a late relay copy of something already played
        }
        for (int i = 0; i < AUDIO_RING_SLOTS; i++)
            if (ring[i].used && ring[i].seq == h.seq)
                return true; // duplicate

        int slot = freeSlot();
        if (slot < 0) {
            playOldest(nowMs); // gap at the head must not deadlock a full ring
            slot = freeSlot();
            if (slot < 0)
                return true;
        }

        ring[slot].used = true;
        ring[slot].seq = h.seq;
        ring[slot].mode = h.mode;
        ring[slot].len = (uint8_t)(frameLen > AUDIO_PAYLOAD_LEN ? AUDIO_PAYLOAD_LEN : frameLen);
        memcpy(ring[slot].bytes, frames, ring[slot].len);
        return true;
    }

    /// @param dueMs true when the next packet's worth of audio is owed
    void tick(uint32_t nowMs, bool dueMs)
    {
        if (!active)
            return;

        if (expired(nowMs)) {
            drain(nowMs);
            endStream();
            return;
        }

        if (priming) {
            if (filled() < AUDIO_PRIME_SLOTS && (nowMs - primeStartMs) < primeHoldMs)
                return;
            priming = false;
        }

        if (!dueMs)
            return;

        int slot = findSeq(playoutSeq);
        if (slot >= 0) {
            play(slot, false);
        } else if (filled() > 0 || sawLast) {
            conceal(); // codec2 has no PLC; repeating beats stalling
        } else {
            return; // starved, wait for more
        }
        playoutSeq++;

        if (sawLast && filled() == 0)
            endStream();
    }

    void setPrimeHoldMs(uint32_t ms) { primeHoldMs = ms; }

    // Packet duration for the configured mode; scales the idle timeout.
    void setPacketMs(uint32_t ms) { packetMs = ms; }

    /// True while a transmission is in progress, so PTT should refuse.
    bool isReceiving() const { return active; }

    bool hasQueued() const { return queue.used > 0; }

  private:
    struct Slot {
        uint16_t seq;
        uint8_t mode;
        uint8_t len;
        bool used;
        uint8_t bytes[AUDIO_PAYLOAD_LEN];
    };

    // Survives the 16-bit wrap.
    static bool seqBefore(uint16_t a, uint16_t b) { return (int16_t)(a - b) < 0; }

    uint32_t idleTimeoutMs() const
    {
        uint32_t t = packetMs * AUDIO_IDLE_PACKETS;
        if (t < AUDIO_IDLE_MIN_MS)
            t = AUDIO_IDLE_MIN_MS;
        if (t > AUDIO_IDLE_MAX_MS)
            t = AUDIO_IDLE_MAX_MS;
        return t;
    }

    bool expired(uint32_t nowMs) const { return (uint32_t)(nowMs - lastRxMs) > idleTimeoutMs(); }

    // Packets from a second sender, stored whole so promotion is just a replay.
    void enqueue(NodeNum from, uint8_t session, const uint8_t *payload, size_t len)
    {
        if (queue.used && (queue.from != from || queue.session != session))
            return; // already holding someone else
        if (queue.used == 0) {
            queue.from = from;
            queue.session = session;
        }
        if (queue.used + 2 + len > AUDIO_QUEUE_BYTES) {
            queue.truncated = true;
            return;
        }
        queue.buf[queue.used++] = (uint8_t)(len & 0xff);
        queue.buf[queue.used++] = (uint8_t)(len >> 8);
        memcpy(queue.buf + queue.used, payload, len);
        queue.used += (uint16_t)len;
    }

    void endStream()
    {
        reset();
        promote();
    }

    void promote()
    {
        if (!queue.used)
            return;
        // Copy out first: accept() re-enters enqueue() for anything that does
        // not belong to the stream being promoted.
        static uint8_t scratch[AUDIO_QUEUE_BYTES];
        const uint16_t n = queue.used;
        const NodeNum from = queue.from;
        memcpy(scratch, queue.buf, n);
        queue.used = 0;
        queue.truncated = false;

        uint16_t off = 0;
        while (off + 2 <= n) {
            const uint16_t len = (uint16_t)(scratch[off] | (scratch[off + 1] << 8));
            off += 2;
            if (off + len > n)
                break;
            accept(from, scratch + off, len, lastRxMs);
            off += len;
        }
    }

    int freeSlot() const
    {
        for (int i = 0; i < AUDIO_RING_SLOTS; i++)
            if (!ring[i].used)
                return i;
        return -1;
    }

    int findSeq(uint16_t seq) const
    {
        for (int i = 0; i < AUDIO_RING_SLOTS; i++)
            if (ring[i].used && ring[i].seq == seq)
                return i;
        return -1;
    }

    int filled() const
    {
        int n = 0;
        for (int i = 0; i < AUDIO_RING_SLOTS; i++)
            if (ring[i].used)
                n++;
        return n;
    }

    void play(int slot, bool concealed)
    {
        if (emit)
            emit(emitCtx, ring[slot].bytes, ring[slot].len, ring[slot].mode, concealed);
        memcpy(lastPayload, ring[slot].bytes, ring[slot].len);
        lastPayloadLen = ring[slot].len;
        lastPayloadMode = ring[slot].mode;
        haveLastPayload = true;
        ring[slot].used = false;
    }

    void conceal()
    {
        if (haveLastPayload && emit)
            emit(emitCtx, lastPayload, lastPayloadLen, lastPayloadMode, true);
    }

    void playOldest(uint32_t nowMs)
    {
        (void)nowMs;
        int oldest = -1;
        for (int i = 0; i < AUDIO_RING_SLOTS; i++)
            if (ring[i].used && (oldest < 0 || seqBefore(ring[i].seq, ring[oldest].seq)))
                oldest = i;
        if (oldest >= 0) {
            playoutSeq = (uint16_t)(ring[oldest].seq + 1);
            play(oldest, false);
        }
    }

    void drain(uint32_t nowMs)
    {
        (void)nowMs;
        for (;;) {
            int oldest = -1;
            for (int i = 0; i < AUDIO_RING_SLOTS; i++)
                if (ring[i].used && (oldest < 0 || seqBefore(ring[i].seq, ring[oldest].seq)))
                    oldest = i;
            if (oldest < 0)
                return;
            play(oldest, false);
        }
    }

    EmitFn emit = nullptr;
    void *emitCtx = nullptr;

    Slot ring[AUDIO_RING_SLOTS] = {};
    bool active = false;
    bool priming = false;
    bool sawLast = false;

    NodeNum streamFrom = 0;
    uint8_t streamSession = 0;
    uint8_t streamMode = 0;
    uint16_t playoutSeq = 0;
    uint32_t lastRxMs = 0;
    uint32_t primeStartMs = 0;
    uint32_t primeHoldMs = 1000;
    uint32_t packetMs = 2240;

    struct {
        NodeNum from;
        uint8_t session;
        uint16_t used;
        bool truncated;
        uint8_t buf[AUDIO_QUEUE_BYTES];
    } queue = {};

    uint8_t lastPayload[AUDIO_PAYLOAD_LEN] = {};
    uint8_t lastPayloadLen = 0;
    uint8_t lastPayloadMode = 0;
    bool haveLastPayload = false;
};
