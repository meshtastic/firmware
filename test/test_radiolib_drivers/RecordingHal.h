#pragma once

// A stand-in for the radio chip: records every SPI transaction and answers with scripted bytes.
// Shared by every chip family's tests in this suite; see test_main.cpp for why it is enough.

#include <RadioLib.h>

#include <algorithm>
#include <cstring>
#include <vector>

class RecordingHal : public RadioLibHal
{
  public:
    // A scripted answer, chosen by the leading bytes (the opcode) of what the driver sent.
    struct Reply {
        std::vector<uint8_t> prefix;
        bool nextTransaction; // answer the transaction after the match: LRxxxx reads reply in a second one
        std::vector<uint8_t> head;
        uint8_t fill; // every byte after head
    };

    // 0x04 decodes as success on every status-byte family: LRxxxx CMD_OK, SX126x DATA_AVAILABLE,
    // SX128x CMD_PROCESSED, and is neither 0x00 nor 0xFF (CHIP_NOT_FOUND).
    explicit RecordingHal(uint8_t defaultFill = 0x04) : RadioLibHal(0, 1, 0, 1, 2, 3), defaultFill(defaultFill) {}

    // Back to a fresh chip, keeping the allocation: see freshHal().
    void reset(uint8_t fill = 0x04)
    {
        transactions.clear();
        replies.clear();
        registerEcho = false;
        memset(registers, 0, sizeof(registers));
        defaultFill = fill;
        pending = nullptr;
        nowUs = 0;
    }

    std::vector<std::vector<uint8_t>> transactions;
    std::vector<Reply> replies;

    // SX127x mode: no status byte, and RADIOLIB_SPI_PARANOID reads every register write back, so
    // writes are stored and reads answered from the store.
    bool registerEcho = false;
    uint8_t registers[128] = {};

    void reply(std::vector<uint8_t> prefix, uint8_t fill, std::vector<uint8_t> head = {}, bool nextTransaction = false)
    {
        replies.push_back({std::move(prefix), nextTransaction, std::move(head), fill});
    }

    // Transactions whose leading bytes are `prefix`.
    size_t count(const std::vector<uint8_t> &prefix) const
    {
        size_t n = 0;
        for (const auto &t : transactions)
            n += startsWith(t.data(), t.size(), prefix);
        return n;
    }

    const std::vector<uint8_t> *first(const std::vector<uint8_t> &prefix) const
    {
        for (const auto &t : transactions)
            if (startsWith(t.data(), t.size(), prefix))
                return &t;
        return nullptr;
    }

    const std::vector<uint8_t> *last(const std::vector<uint8_t> &prefix) const
    {
        for (auto it = transactions.rbegin(); it != transactions.rend(); ++it)
            if (startsWith(it->data(), it->size(), prefix))
                return &*it;
        return nullptr;
    }

    void pinMode(uint32_t, uint32_t) override {}
    void digitalWrite(uint32_t, uint32_t) override {}
    uint32_t digitalRead(uint32_t) override { return 0; } // BUSY low
    void attachInterrupt(uint32_t, void (*)(void), uint32_t) override {}
    void detachInterrupt(uint32_t) override {}
    void delay(RadioLibTime_t ms) override { nowUs += ms * 1000; }
    void delayMicroseconds(RadioLibTime_t us) override { nowUs += us; }
    // Advances on every read, so a RadioLib wait loop always reaches its timeout instead of spinning.
    RadioLibTime_t millis() override { return (nowUs += 1000) / 1000; }
    RadioLibTime_t micros() override { return nowUs += 1000; }
    long pulseIn(uint32_t, uint32_t, RadioLibTime_t) override { return 0; }
    void spiBegin() override {}
    void spiBeginTransaction() override {}
    void spiEndTransaction() override {}
    void spiEnd() override {}

    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override
    {
        transactions.emplace_back(out, out + len);
        if (registerEcho) {
            echoRegisters(out, len, in);
            return;
        }
        const Reply *r = pending;
        pending = nullptr;
        for (const auto &c : replies) {
            if (!startsWith(out, len, c.prefix))
                continue;
            if (c.nextTransaction)
                pending = &c;
            else if (!r)
                r = &c;
        }
        for (size_t i = 0; i < len; i++)
            in[i] = !r ? defaultFill : (i < r->head.size() ? r->head[i] : r->fill);
    }

  private:
    uint8_t defaultFill;
    const Reply *pending = nullptr;
    RadioLibTime_t nowUs = 0;

    static bool startsWith(const uint8_t *data, size_t len, const std::vector<uint8_t> &prefix)
    {
        return len >= prefix.size() && std::equal(prefix.begin(), prefix.end(), data);
    }

    // SX127x framing: first byte is the address, bit 7 set for a write; the rest is the burst.
    void echoRegisters(const uint8_t *out, size_t len, uint8_t *in)
    {
        const uint8_t addr = out[0] & 0x7F;
        in[0] = 0;
        for (size_t i = 1; i < len; i++) {
            uint8_t &reg = registers[(addr + i - 1) & 0x7F];
            if (out[0] & 0x80)
                reg = out[i], in[i] = 0;
            else
                in[i] = reg;
        }
    }
};

// Big-endian opcode bytes, for the 16-bit LRxxxx command set.
inline std::vector<uint8_t> op16(uint16_t opcode)
{
    return {static_cast<uint8_t>(opcode >> 8), static_cast<uint8_t>(opcode & 0xFF)};
}

// A failed TEST_ASSERT longjmps out of the test, skipping destructors, so a test-local HAL would leak
// its transaction log and LeakSanitizer would turn every assertion failure into a crash. One static
// instance, reset per test, stays reachable.
inline RecordingHal &freshHal()
{
    static RecordingHal hal;
    hal.reset();
    return hal;
}
