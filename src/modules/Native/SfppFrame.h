#pragma once

#include "mesh/generated/meshtastic/mesh.pb.h"

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * Framed replay: one message split across independent, hash-keyed frames.
 *
 * A replay frame is an ordinary portnum-35 ITEM_FRAME. The key is the first eight bytes of
 * SHA-256(ciphertext || to || from || id), the same message hash the chain store already computes.
 * Keying on the message rather than the sender is what lets frames from different archives complete
 * one set instead of colliding, and what makes a duplicate cancel.
 *
 * The frame's encoded StoreForwardPlusPlus body is held to 168 bytes, which is what keeps a
 * broadcast frame inside the size an XEdDSA signature still fits. The reassembler is all or nothing:
 * a partial or spliced set is discarded, never decrypted, and only a set whose recomputed hash
 * matches the prefix every frame carried is handed on.
 */
namespace sfppframe
{

// R-sign: encoded body + routing header must leave room for the signature.
constexpr size_t kMaxFrameBodyBytes = 168;

// Any text Meshtastic can send needs at most two; four is the margin.
constexpr uint8_t kMaxFrames = 4;

constexpr size_t kMessageHashBytes = 16;
constexpr size_t kFrameKeyBytes = 8;

// The wire carries broadcast as 0; the message hash and the replay use the real value.
constexpr uint32_t kBroadcastNodeNum = 0xFFFFFFFFu;

constexpr size_t kDefaultSlots = 2;
constexpr uint32_t kDefaultDeadlineMs = 120 * 1000;

// One reassembled replay: the original message plus what the archive attested about it.
struct Replay {
    uint32_t to = 0;
    uint32_t from = 0;
    uint32_t id = 0;
    uint32_t heardAgoS = 0;
    bool originVerified = false;
    std::vector<uint8_t> ciphertext;
};

// SHA-256(ciphertext || to || from || id), truncated to 16 bytes, byte order matching the store.
void messageHash(const uint8_t *ciphertext, size_t len, uint32_t to, uint32_t from, uint32_t id, uint8_t out[kMessageHashBytes]);

// Splits ciphertext into at most kMaxFrames ITEM_FRAMEs, each with an encoded body <= 168 bytes.
// False when the ciphertext cannot be split that small, so no frame is emitted half-sized.
bool split(const uint8_t *ciphertext, size_t len, uint32_t to, uint32_t from, uint32_t id, uint32_t heardAgoS,
           bool originVerified, std::vector<meshtastic_StoreForwardPlusPlus> &out);

/**
 * A fixed pool of reassembly slots, least-recently-used on eviction.
 *
 * deadlineMs is 120 s by default, or four advertised pacings. The caller supplies nowMs so the
 * behaviour is testable without a clock.
 */
class Reassembler
{
  public:
    explicit Reassembler(size_t slots = kDefaultSlots, uint32_t deadlineMs = kDefaultDeadlineMs);

    // Feeds one frame. True only when this frame completed a verified set; `out` is then set.
    bool offer(const meshtastic_StoreForwardPlusPlus &frame, uint32_t nowMs, Replay &out);

    // Drops every slot whose deadline has passed.
    void expire(uint32_t nowMs);

    size_t activeSlots() const;
    bool hasSlot(const uint8_t key[kFrameKeyBytes]) const;

  private:
    // One frame's ciphertext is bounded by the generated wire field (PB_BYTES_ARRAY_T(240), mesh.pb.h).
    static constexpr size_t kMaxCiphertextBytes = kMaxFrames * sizeof(meshtastic_StoreForwardPlusPlus_message_t{}.bytes);

    struct Slot {
        uint8_t key[kFrameKeyBytes] = {0};
        uint8_t total = 0;
        uint8_t received = 0;
        bool haveMeta = false;
        uint32_t to = 0;
        uint32_t from = 0;
        uint32_t id = 0;
        uint32_t heardAgoS = 0;
        bool originVerified = false;
        uint32_t deadline = 0;
        uint32_t lastUsed = 0;
        bool have[kMaxFrames] = {};
        std::vector<uint8_t> parts[kMaxFrames];
    };

    Slot *find(const uint8_t key[kFrameKeyBytes]);
    Slot *allocate(const uint8_t key[kFrameKeyBytes], uint8_t total, uint32_t nowMs);
    static void reset(Slot &slot);

    std::vector<Slot> slots;
    uint32_t deadlineMs;
};

} // namespace sfppframe
