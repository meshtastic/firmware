#pragma once

#include "AudioModeSelect.h"
#include "mesh/MeshTypes.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Wire format for codec2 voice. The old header was three bytes of magic plus a
// mode byte on a port that already says "audio", with no stream identity, no
// sequence and no end marker - so relayed and direct copies of the same
// transmission played in arrival order and two talkers merged into one buffer.
struct __attribute__((packed)) AudioHeader {
    uint8_t magic;   // AUDIO_MAGIC
    uint8_t mode;    // codec2 mode id, upstream numbering
    uint8_t session; // random per PTT press, separates back-to-back presses
    uint8_t last;    // 1 on the final packet of a transmission
    uint16_t seq;    // little-endian, per session, 0-based
};
static_assert(sizeof(AudioHeader) == AUDIO_HEADER_LEN, "AudioHeader must match AUDIO_HEADER_LEN");

#define AUDIO_MAGIC 0xC2

// Packets held before playout starts. Four is a little over a second at 2400
// and six seconds at 450, which is more jitter than the mesh produces.
#define AUDIO_RING_SLOTS 4

// Slots that must fill before playout begins, unless the hold expires first.
#define AUDIO_PRIME_SLOTS 2

// No packet for this long ends the stream, in case `last` never arrives.
#define AUDIO_STREAM_TIMEOUT_MS 5000

/**
 * Reassembles one voice stream into playout order.
 *
 * Deliberately free of codec2, I2S and Arduino so it can be tested on the host:
 * time arrives as a parameter and decoded frames leave through a callback.
 *
 * Half duplex by design, so exactly one stream is tracked. A second talker
 * arriving mid-stream is dropped rather than interleaved.
 */
class AudioStreamAssembler
{
  public:
    // frames/len is one packet's codec2 payload, header already stripped.
    // `conceal` marks a repeat standing in for a packet that never arrived.
    typedef void (*EmitFn)(void *ctx, const uint8_t *frames, size_t len, uint8_t mode, bool conceal);

    void begin(EmitFn fn, void *ctx)
    {
        emit = fn;
        emitCtx = ctx;
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
            // Another talker while one is live. Half duplex: ignore rather than
            // interleave two people into one buffer.
            if (!expired(nowMs))
                return false;
            reset();
        }

        if (!active) {
            active = true;
            priming = true;
            primeStartMs = nowMs;
            streamFrom = from;
            streamSession = h.session;
            streamMode = h.mode;
            // Start where the stream actually reached us; the opening packet is
            // the one most likely to have been lost.
            playoutSeq = h.seq;
        }

        lastRxMs = nowMs;
        if (h.last)
            sawLast = true;

        if (seqBefore(h.seq, playoutSeq)) {
            // Before playout starts, an earlier seq means the burst arrived out
            // of order rather than late - rewind to it. Seeding playoutSeq from
            // the first arrival alone would discard the rest of a reversed
            // burst, which is exactly the case this class exists for.
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
            // Ring full and this packet is further ahead: force the oldest out
            // rather than stalling on a gap that may never fill.
            playOldest(nowMs);
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

    /**
     * Advance playout. Call once per packet-duration while a stream is live.
     * @param dueMs true when the next packet's worth of audio is owed
     */
    void tick(uint32_t nowMs, bool dueMs)
    {
        if (!active)
            return;

        if (expired(nowMs)) {
            drain(nowMs);
            reset();
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
            // Nothing for this slot but the stream is still going: repeat rather
            // than stall. Codec2 has no loss concealment of its own.
            conceal();
        } else {
            return; // starved, wait for more
        }
        playoutSeq++;

        if (sawLast && filled() == 0)
            reset();
    }

    void setPrimeHoldMs(uint32_t ms) { primeHoldMs = ms; }

  private:
    struct Slot {
        uint16_t seq;
        uint8_t mode;
        uint8_t len;
        bool used;
        uint8_t bytes[AUDIO_PAYLOAD_LEN];
    };

    // Sequence comparison that survives the 16-bit wrap.
    static bool seqBefore(uint16_t a, uint16_t b) { return (int16_t)(a - b) < 0; }

    bool expired(uint32_t nowMs) const { return (uint32_t)(nowMs - lastRxMs) > AUDIO_STREAM_TIMEOUT_MS; }

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

    uint8_t lastPayload[AUDIO_PAYLOAD_LEN] = {};
    uint8_t lastPayloadLen = 0;
    uint8_t lastPayloadMode = 0;
    bool haveLastPayload = false;
};
