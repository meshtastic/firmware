#include "SfppFrame.h"

#include "SHA256.h"
#include "mesh/Throttle.h"
#include "platform/portduino/PortduinoGlue.h"

#include <algorithm>
#include <cstring>
#include <pb_encode.h>

namespace sfppframe
{

namespace
{

size_t encodedSize(const meshtastic_StoreForwardPlusPlus &msg)
{
    size_t size = 0;
    if (!pb_get_encoded_size(&size, &meshtastic_StoreForwardPlusPlus_msg, &msg))
        return SIZE_MAX;
    return size;
}

} // namespace

uint16_t encodeAge14(uint32_t ageS)
{
    if (ageS > kMaxAgeS)
        ageS = kMaxAgeS;
    uint8_t exponent = 0;
    while ((ageS >> exponent) > 0x3FFu && exponent < 15)
        exponent++;
    return (uint16_t)(((uint16_t)exponent << 10) | ((ageS >> exponent) & 0x3FFu));
}

uint32_t decodeAge14(uint16_t code)
{
    const uint8_t exponent = (uint8_t)((code >> 10) & 0xFu);
    return (uint32_t)(code & 0x3FFu) << exponent;
}

namespace
{

// packed frame field: low byte index, next byte total; 0 means the message fits in one frame.

uint32_t packFrame(uint8_t index, uint8_t total)
{
    return (uint32_t)index | (uint32_t)total << 8;
}

bool readFrame(const meshtastic_StoreForwardPlusPlus &in, uint8_t &index, uint8_t &total)
{
    if (in.frame == 0) {
        index = 0;
        total = 1;
        return true;
    }
    index = in.frame & 0xFF;
    total = in.frame >> 8 & 0xFF;
    return total >= 1 && total <= kMaxFrames && index < total;
}

bool buildFrame(const uint8_t *hash, const uint8_t *slice, size_t len, uint32_t to, uint32_t from, uint32_t id,
                uint32_t heardAgoS, bool originVerified, uint32_t frameField, bool withMeta, meshtastic_StoreForwardPlusPlus &out)
{
    meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_default;
    if (len > sizeof(msg.message.bytes))
        return false;

    msg.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME;
    msg.sr_version = kFrameVersion;
    msg.message_hash.size = kFrameKeyBytes;
    memcpy(msg.message_hash.bytes, hash, kFrameKeyBytes);
    if (withMeta) {
        msg.encapsulated_to = to == kBroadcastNodeNum ? 0 : to;
        msg.encapsulated_from = from;
        msg.encapsulated_id = id;
        msg.age = encodeAge14(heardAgoS);
        msg.origin_verified = originVerified;
    }
    msg.frame = frameField;
    msg.message.size = len;
    memcpy(msg.message.bytes, slice, len);
    out = msg;
    return true;
}

// Encoded size grows with the slice, so the first length that fails bounds every later one. The
// metadata is sized at its worst case so every archive splits the same message identically, even
// though the frame that finally carries it may be a few bytes smaller.
size_t maxSliceFor(const uint8_t *ciphertext, size_t len, uint32_t to, uint32_t from, uint32_t id, uint32_t frameField,
                   bool withMeta, size_t fieldLimit, size_t bodyLimit)
{
    const uint32_t worstHeardAgo = withMeta ? kMaxAgeS : 0;
    const bool worstVerified = withMeta;

    size_t maxSlice = 0;
    for (size_t sliceLen = 1; sliceLen <= len && sliceLen <= fieldLimit; sliceLen++) {
        meshtastic_StoreForwardPlusPlus frame;
        if (!buildFrame(ciphertext, ciphertext, sliceLen, to, from, id, worstHeardAgo, worstVerified, frameField, withMeta,
                        frame))
            break;
        if (encodedSize(frame) > bodyLimit)
            break;
        maxSlice = sliceLen;
    }
    return maxSlice;
}

} // namespace

size_t maxFrameBodyBytes()
{
    const uint32_t configured = portduino_config.sfpp_item_body_max;
    if (configured < kMinFrameBodyBytes)
        return kMinFrameBodyBytes;
    if (configured > kMaxFrameBodyBytes)
        return kMaxFrameBodyBytes;
    return configured;
}

void messageHash(const uint8_t *ciphertext, size_t len, uint32_t to, uint32_t from, uint32_t id, uint8_t out[kMessageHashBytes])
{
    SHA256 hash;
    hash.update(ciphertext, len);
    hash.update(&to, sizeof(to));
    hash.update(&from, sizeof(from));
    hash.update(&id, sizeof(id));
    hash.finalize(out, kMessageHashBytes);
}

bool split(const uint8_t *ciphertext, size_t len, uint32_t to, uint32_t from, uint32_t id, uint32_t heardAgoS,
           bool originVerified, std::vector<meshtastic_StoreForwardPlusPlus> &out)
{
    if (ciphertext == nullptr || len == 0)
        return false;

    uint8_t hash[kMessageHashBytes];
    messageHash(ciphertext, len, to, from, id, hash);

    meshtastic_StoreForwardPlusPlus probe = meshtastic_StoreForwardPlusPlus_init_default;
    const size_t fieldLimit = sizeof(probe.message.bytes);
    const size_t bodyLimit = maxFrameBodyBytes();

    // A message that fits one frame carries no frame field at all, so try that shape first. The
    // envelope is sized at its worst case here too, so the decision is the same for every archive.
    if (len <= fieldLimit) {
        meshtastic_StoreForwardPlusPlus worst;
        if (!buildFrame(hash, ciphertext, len, to, from, id, kMaxAgeS, true, 0, true, worst))
            return false;
        if (encodedSize(worst) <= bodyLimit) {
            meshtastic_StoreForwardPlusPlus single;
            if (!buildFrame(hash, ciphertext, len, to, from, id, heardAgoS, originVerified, 0, true, single))
                return false;
            out.assign(1, single);
            return true;
        }
    }

    // Frame 0 carries the envelope; the rest are key, index and ciphertext only. Both slice sizes
    // use the worst-case envelope, so two archives split the same message identically.
    const size_t maxFirst = maxSliceFor(ciphertext, len, to, from, id, packFrame(0, 2), true, fieldLimit, bodyLimit);
    const size_t maxRest = maxSliceFor(ciphertext, len, to, from, id, packFrame(1, 2), false, fieldLimit, bodyLimit);
    if (maxFirst == 0 || maxRest == 0)
        return false;

    if (len <= maxFirst) {
        meshtastic_StoreForwardPlusPlus single;
        if (!buildFrame(hash, ciphertext, len, to, from, id, heardAgoS, originVerified, 0, true, single))
            return false;
        if (encodedSize(single) > bodyLimit)
            return false;
        out.assign(1, single);
        return true;
    }

    const size_t total = 1 + (len - maxFirst + maxRest - 1) / maxRest;
    if (total > kMaxFrames)
        return false;

    std::vector<meshtastic_StoreForwardPlusPlus> frames;
    frames.reserve(total);
    size_t offset = 0;
    for (size_t i = 0; i < total; i++) {
        const size_t limit = i == 0 ? maxFirst : maxRest;
        const bool withMeta = i == 0;
        const size_t sliceLen = std::min(limit, len - offset);
        meshtastic_StoreForwardPlusPlus frame;
        if (!buildFrame(hash, ciphertext + offset, sliceLen, to, from, id, heardAgoS, originVerified,
                        packFrame((uint8_t)i, (uint8_t)total), withMeta, frame))
            return false;
        if (encodedSize(frame) > bodyLimit)
            return false;
        frames.push_back(frame);
        offset += sliceLen;
    }

    out = std::move(frames);
    return true;
}

Reassembler::Reassembler(size_t slotCount, uint32_t frameDeadlineMs)
    : slots(slotCount ? slotCount : 1), deadlineMs(frameDeadlineMs)
{
}

void Reassembler::reset(Slot &slot)
{
    slot = Slot();
}

Reassembler::Slot *Reassembler::find(const uint8_t key[kFrameKeyBytes])
{
    for (auto &slot : slots) {
        if (slot.total != 0 && memcmp(slot.key, key, kFrameKeyBytes) == 0)
            return &slot;
    }
    return nullptr;
}

Reassembler::Slot *Reassembler::allocate(const uint8_t key[kFrameKeyBytes], uint8_t total, uint32_t nowMs)
{
    Slot *chosen = nullptr;
    for (auto &slot : slots) {
        if (slot.total == 0) {
            chosen = &slot;
            break;
        }
    }
    if (chosen == nullptr) {
        // Largest unsigned age, not smallest timestamp: the subtraction makes the choice correct
        // across the millis() wrap.
        chosen = &slots[0];
        uint32_t oldestAge = nowMs - chosen->lastUsed;
        for (auto &slot : slots) {
            const uint32_t age = nowMs - slot.lastUsed;
            if (age > oldestAge) {
                oldestAge = age;
                chosen = &slot;
            }
        }
    }

    reset(*chosen);
    memcpy(chosen->key, key, kFrameKeyBytes);
    chosen->total = total;
    chosen->deadline = nowMs + deadlineMs;
    chosen->lastUsed = nowMs;
    return chosen;
}

void Reassembler::expire(uint32_t nowMs)
{
    for (auto &slot : slots) {
        if (slot.total != 0 && Throttle::deadlinePassedAt(nowMs, slot.deadline))
            reset(slot);
    }
}

bool Reassembler::offer(const meshtastic_StoreForwardPlusPlus &frame, uint32_t nowMs, Replay &out)
{
    expire(nowMs);

    if (frame.sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
        return false;
    if (frame.sr_version != kFrameVersion || frame.age > kMaxAge14)
        return false;
    if (frame.message_hash.size < kFrameKeyBytes)
        return false;
    if (frame.message.size == 0 || frame.message.size > sizeof(frame.message.bytes))
        return false;

    uint8_t index = 0;
    uint8_t total = 0;
    if (!readFrame(frame, index, total))
        return false;

    uint8_t key[kFrameKeyBytes];
    memcpy(key, frame.message_hash.bytes, kFrameKeyBytes);

    Slot *slot = find(key);
    if (slot == nullptr)
        slot = allocate(key, total, nowMs);
    if (slot->total != total)
        return false;

    slot->lastUsed = nowMs;
    if (slot->have[index])
        return false;

    slot->parts[index].assign(frame.message.bytes, frame.message.bytes + frame.message.size);
    slot->have[index] = true;
    slot->received++;

    if (index == 0) {
        slot->to = frame.encapsulated_to == 0 ? kBroadcastNodeNum : frame.encapsulated_to;
        slot->from = frame.encapsulated_from;
        slot->id = frame.encapsulated_id;
        slot->heardAgoS = decodeAge14((uint16_t)frame.age);
        slot->originVerified = frame.origin_verified;
        slot->haveMeta = true;
    }

    // Without frame 0 there is no envelope to verify against, so the set waits for it.
    if (!slot->haveMeta || slot->received != total)
        return false;

    Replay replay;
    replay.to = slot->to;
    replay.from = slot->from;
    replay.id = slot->id;
    replay.heardAgoS = slot->heardAgoS;
    replay.originVerified = slot->originVerified;

    size_t totalBytes = 0;
    for (uint8_t i = 0; i < total; i++) {
        if (!slot->have[i])
            return false; // a duplicate cannot stand in for a missing index
        totalBytes += slot->parts[i].size();
    }
    if (totalBytes > kMaxCiphertextBytes) {
        reset(*slot);
        return false;
    }

    replay.ciphertext.reserve(totalBytes);
    for (uint8_t i = 0; i < total; i++)
        replay.ciphertext.insert(replay.ciphertext.end(), slot->parts[i].begin(), slot->parts[i].end());

    uint8_t recomputed[kMessageHashBytes];
    messageHash(replay.ciphertext.data(), replay.ciphertext.size(), replay.to, replay.from, replay.id, recomputed);
    const bool verified = memcmp(recomputed, key, kFrameKeyBytes) == 0;

    reset(*slot);
    if (!verified)
        return false;

    out = std::move(replay);
    return true;
}

size_t Reassembler::activeSlots() const
{
    size_t count = 0;
    for (const auto &slot : slots) {
        if (slot.total != 0)
            count++;
    }
    return count;
}

bool Reassembler::hasSlot(const uint8_t key[kFrameKeyBytes]) const
{
    for (const auto &slot : slots) {
        if (slot.total != 0 && memcmp(slot.key, key, kFrameKeyBytes) == 0)
            return true;
    }
    return false;
}

} // namespace sfppframe
