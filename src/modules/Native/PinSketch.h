#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * PinSketch set reconciliation over GF(2^32).
 *
 * A sketch is a fixed-size digest of a set, holding the odd power sums (BCH syndromes)
 * S_1, S_3, ... S_(2c-1) of its members. Two properties make it useful here:
 *
 *   - It is linear. XOR two sketches and the result is the sketch of the symmetric difference,
 *     because every element held by both sides cancels. Neither side enumerates its set.
 *   - Decoding recovers that difference exactly when it is no larger than the capacity c.
 *
 * Cost is therefore proportional to how much two nodes differ by, not to how much they hold.
 *
 * Beyond c, decoding usually fails outright - but not always. Roughly 1/c! of the syndrome space is
 * occupied by sets small enough to decode, so an over-capacity difference lands on a wrong set that
 * reproduces the same syndromes at about that rate: measured here at 50% for c=2, 16% for c=3, 3.8%
 * for c=4, and under 0.1% by c=6. A sketch alone therefore cannot establish that two sides agree,
 * and capacities below ~6 cannot even reliably signal "too different, escalate". That is what the
 * protocol's separate set checksum is for, and why it is not optional.
 *
 * Capacity c costs 4c bytes on the wire. A capacity-c sketch is a prefix of any larger one, so a
 * sender that builds at high capacity can transmit a truncated prefix and grow it if the receiver
 * fails to decode - see truncate(). Growing a sketch is not possible from the sketch alone.
 *
 * Decoding is Berlekamp-Massey to recover the error locator, then the Berlekamp trace algorithm to
 * find its roots. Chien search is not viable at this field size (2^32 candidates).
 */
namespace pinsketch
{

// A field element, and equivalently a short ID. Zero is not a representable set member.
using Element = uint32_t;

// GF(2^32) modulo x^32 + x^7 + x^3 + x^2 + 1, the smallest irreducible pentanomial at this size.
Element mul(Element a, Element b);
Element sqr(Element a);
Element inv(Element a);

class Sketch
{
  public:
    explicit Sketch(size_t capacity = 0) : syndromes(capacity, 0) {}

    size_t capacity() const { return syndromes.size(); }
    size_t serializedSize() const { return syndromes.size() * sizeof(Element); }

    // Toggles membership - adding a held element removes it again. Rejects zero.
    bool add(Element e);

    // XORs another sketch of equal capacity in, leaving the sketch of the symmetric difference.
    bool merge(const Sketch &other);

    // Recovers the set, sorted ascending. Returns false when the set is larger than the capacity or
    // the syndromes are not a decodable sketch - subject to the ~1/c! misdecode rate noted above.
    bool decode(std::vector<Element> &out) const;

    // Little-endian per element, into a buffer of at least serializedSize() bytes.
    void serialize(uint8_t *out) const;
    bool deserialize(const uint8_t *in, size_t len);

    // Discards capacity from the tail. Only ever shrinks, which is what makes prefix streaming work.
    void truncate(size_t newCapacity);

    bool empty() const;
    void clear();

  private:
    std::vector<Element> syndromes;
};

} // namespace pinsketch
