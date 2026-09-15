#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// KISS framing (the TNC serial protocol, as the raw modem client speaks it over TCP), kept apart from the socket code so
// test/test_kiss_framing can pin it.
namespace kiss
{
constexpr uint8_t FEND = 0xC0;
constexpr uint8_t FESC = 0xDB;
constexpr uint8_t TFEND = 0xDC;
constexpr uint8_t TFESC = 0xDD;

/// Byte-at-a-time deframer: bytes before the first FEND are ignored, empty frames are skipped, an invalid escape drops the
/// escaped byte, and a frame longer than the buffer is discarded. feed() returns true on the FEND that completes a frame;
/// buf[0..len) then holds it until the next feed().
class Deframer
{
  public:
    static constexpr size_t MAX_FRAME = 512;

    bool feed(uint8_t b)
    {
        if (complete) {
            complete = false;
            len = 0;
            inFrame = true;
            escaped = false;
        }
        if (b == FEND) {
            complete = inFrame && len > 0;
            if (!complete) {
                len = 0;
                inFrame = true;
                escaped = false;
            }
            return complete;
        }
        if (!inFrame)
            return false;
        if (escaped) {
            escaped = false;
            if (b == TFEND)
                b = FEND;
            else if (b == TFESC)
                b = FESC;
            else
                return false; // an invalid escape (a second FESC included) drops the byte
        } else if (b == FESC) {
            escaped = true;
            return false;
        }
        if (len >= MAX_FRAME) {
            len = 0;
            inFrame = false;
            return false;
        }
        buf[len++] = b;
        return false;
    }

    void reset()
    {
        len = 0;
        inFrame = escaped = complete = false;
    }

    uint8_t buf[MAX_FRAME];
    size_t len = 0;

  private:
    bool inFrame = false;
    bool escaped = false;
    bool complete = false;
};

/// Appends one escaped frame (type byte, then data, then data2) to out.
inline void encode(std::vector<uint8_t> &out, uint8_t type, const uint8_t *data, size_t len, const uint8_t *data2 = nullptr,
                   size_t len2 = 0)
{
    auto put = [&out](uint8_t b) {
        if (b == FEND) {
            out.push_back(FESC);
            out.push_back(TFEND);
        } else if (b == FESC) {
            out.push_back(FESC);
            out.push_back(TFESC);
        } else {
            out.push_back(b);
        }
    };
    out.push_back(FEND);
    put(type);
    for (size_t i = 0; i < len; i++)
        put(data[i]);
    for (size_t i = 0; i < len2; i++)
        put(data2[i]);
    out.push_back(FEND);
}
} // namespace kiss
