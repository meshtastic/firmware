#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace meshtastic_security
{

// Compiler-barrier wipe: a plain memset on a dying stack/heap buffer can be
// elided as dead-store. The volatile function pointer forces emission.
inline void secure_zero(void *p, std::size_t n)
{
    if (!p || n == 0)
        return;
    static void *(*volatile memset_v)(void *, int, std::size_t) = std::memset;
    memset_v(p, 0, n);
}

// Fixed-size RAII buffer for key material; zeroed in destructor.
template <std::size_t N> class ZeroizingBuffer
{
  public:
    ZeroizingBuffer() { secure_zero(buf_, N); }
    ~ZeroizingBuffer() { secure_zero(buf_, N); }

    ZeroizingBuffer(const ZeroizingBuffer &) = delete;
    ZeroizingBuffer &operator=(const ZeroizingBuffer &) = delete;

    uint8_t *data() { return buf_; }
    const uint8_t *data() const { return buf_; }
    constexpr std::size_t size() const { return N; }
    uint8_t &operator[](std::size_t i) { return buf_[i]; }
    const uint8_t &operator[](std::size_t i) const { return buf_[i]; }

  private:
    uint8_t buf_[N];
};

// unique_ptr deleter that wipes the buffer before delete[].
struct ZeroizingArrayDeleter {
    std::size_t n;
    void operator()(uint8_t *p) const noexcept
    {
        if (p) {
            secure_zero(p, n);
            delete[] p;
        }
    }
};

using ZeroizingArrayPtr = std::unique_ptr<uint8_t[], ZeroizingArrayDeleter>;

inline ZeroizingArrayPtr make_zeroizing_array(std::size_t n)
{
    return ZeroizingArrayPtr(new uint8_t[n](), ZeroizingArrayDeleter{n});
}

} // namespace meshtastic_security
