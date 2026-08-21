#pragma once

#include "DebugConfiguration.h"
#include "FSCommon.h"
#include "SPILock.h"
#include "TelemetryStore.h"
#include "concurrency/LockGuard.h"
#include <type_traits>

// Read-write without truncating: "w" truncates on the string-mode backends, while the
// Adafruit/STM32 flag is already O_RDWR|O_CREAT.
#if defined(ARCH_NRF52) || defined(ARCH_NRF54L15) || defined(ARCH_STM32)
#define TELEMETRY_STORE_O_RW FILE_O_WRITE
#define TELEMETRY_STORE_O_CREATE FILE_O_WRITE
#else
#define TELEMETRY_STORE_O_RW "r+"
#define TELEMETRY_STORE_O_CREATE "w+"
#endif

/// Fixed-size ring in a preallocated file, so readings outlive a reboot or deep sleep. One
/// record-sized write per reading, so SD is the intended target and internal flash wears.
template <typename T, typename FsT = typename std::remove_reference<decltype(FSCom)>::type>
class FileTelemetryStore : public TelemetryStore<T>
{
    static constexpr uint32_t MAGIC = 0x4D544853; // "MTHS"
    static constexpr uint16_t VERSION = 3;

    // Packed, and immutable once written: its layout has to be stable to reopen a file.
    struct __attribute__((packed)) Header {
        uint32_t magic;
        uint16_t version;
        uint16_t recordSize; // catches payload-layout drift across firmware builds
        uint16_t slots;
    };

    // Not packed: T holds floats and unaligned access faults. Drift is caught by hdr.recordSize.
    // seq orders the ring and doubles as the occupied flag, so it is never handed out as 0. It is
    // repeated at the tail because a short or interrupted write lands the new head over a stale
    // tail, which is the one corruption a single-record store can actually see.
    struct Record {
        uint32_t seq;
        uint32_t time;
        uint8_t publishedMask;
        T metrics;
        uint32_t seqEnd;
    };

    static bool recordValid(const Record &r) { return r.seq != 0 && r.seq == r.seqEnd; }

    FsT &fs;
    const char *path;
    Header hdr = {};
    bool usable = false;

    // Derived from the records on open, never persisted on its own. A ring whose order lives in the
    // header needs that header committed after every push, and a push that overwrote the oldest slot
    // has nothing to roll back to if the commit fails.
    uint16_t head = 0;
    uint16_t count = 0;
    uint32_t lastSeq = 0;

    static uint32_t offsetOf(uint16_t slot) { return sizeof(Header) + (uint32_t)slot * sizeof(Record); }

    bool fileExists()
    {
        concurrency::LockGuard g(spiLock);
        return fs.exists(path);
    }

    uint32_t fileSize()
    {
        concurrency::LockGuard g(spiLock);
        auto f = fs.open(path, FILE_O_READ);
        if (!f)
            return 0;
        const uint32_t n = f.size();
        f.close();
        return n;
    }

    bool readSlot(uint16_t slot, Record &r)
    {
        concurrency::LockGuard g(spiLock);
        auto f = fs.open(path, FILE_O_READ);
        if (!f)
            return false;
        f.seek(offsetOf(slot));
        const bool ok = f.read((uint8_t *)&r, sizeof(r)) == sizeof(r);
        f.close();
        return ok;
    }

    bool writeSlot(uint16_t slot, const Record &r)
    {
        concurrency::LockGuard g(spiLock);
        auto f = fs.open(path, TELEMETRY_STORE_O_RW);
        if (!f)
            return false;
        f.seek(offsetOf(slot));
        const bool ok = f.write((const uint8_t *)&r, sizeof(r)) == sizeof(r);
        f.flush();
        f.close();
        return ok;
    }

    /// Header plus zeroed slots up front, so the file never grows later.
    bool create(uint16_t slots)
    {
        concurrency::LockGuard g(spiLock);
        // Explicit, because the flag-mode backends open read-write without truncating
        if (fs.exists(path))
            fs.remove(path);

        auto f = fs.open(path, TELEMETRY_STORE_O_CREATE);
        if (!f) {
            LOG_ERROR("Telemetry store: cannot create %s", path);
            return false;
        }

        hdr = {MAGIC, VERSION, (uint16_t)sizeof(Record), slots};
        bool ok = f.write((const uint8_t *)&hdr, sizeof(hdr)) == sizeof(hdr);

        const Record blank = {};
        for (uint16_t i = 0; ok && i < slots; i++)
            ok = f.write((const uint8_t *)&blank, sizeof(blank)) == sizeof(blank);

        f.flush();
        f.close();

        head = count = 0;
        lastSeq = 0;

        if (!ok)
            LOG_ERROR("Telemetry store: cannot preallocate %s, %u slots", path, (unsigned)slots);
        return ok;
    }

    bool readHeader()
    {
        concurrency::LockGuard g(spiLock);
        auto f = fs.open(path, FILE_O_READ);
        if (!f)
            return false;
        const bool ok = f.read((uint8_t *)&hdr, sizeof(hdr)) == sizeof(hdr);
        f.close();
        return ok;
    }

    /// Rebuild ring order from the records themselves; the oldest is the lowest sequence present.
    void scanSlots()
    {
        uint32_t oldestSeq = UINT32_MAX;
        head = count = 0;
        lastSeq = 0;

        for (uint16_t i = 0; i < hdr.slots; i++) {
            Record r = {};
            if (!readSlot(i, r) || !recordValid(r))
                continue; // empty, or torn mid-write

            count++;
            if (r.seq > lastSeq)
                lastSeq = r.seq;
            if (r.seq < oldestSeq) {
                oldestSeq = r.seq;
                head = i;
            }
        }
    }

  public:
    /// @param fs filesystem to keep it on; SD, PSRamFS, anything with the same open/exists subset.
    FileTelemetryStore(const char *path, uint16_t slots, FsT &fs = FSCom) : fs(fs), path(path)
    {
        if (slots == 0)
            return;

        // A different payload layout or ring size cannot be read back - start over.
        if (!fileExists() || !readHeader() || hdr.magic != MAGIC || hdr.version != VERSION || hdr.recordSize != sizeof(Record) ||
            hdr.slots != slots) {
            if (fileExists())
                LOG_INFO("Telemetry store: %s has a different layout, recreating", path);
            usable = create(slots);
            return;
        }

        // Shorter than the geometry its header claims: an interrupted preallocation leaves that,
        // and readSlot() would run off the end
        if (fileSize() < offsetOf(slots)) {
            LOG_WARN("Telemetry store: %s is short of its geometry, recreating", path);
            usable = create(slots);
            return;
        }

        usable = true;
        scanSlots();
        LOG_INFO("Telemetry store: %s reopened, %u/%u readings", path, (unsigned)count, (unsigned)hdr.slots);
    }

    FileTelemetryStore(const FileTelemetryStore &) = delete;
    FileTelemetryStore &operator=(const FileTelemetryStore &) = delete;

    bool push(const T &metrics, uint32_t time) override
    {
        if (!usable)
            return false;

        Record r = {};
        r.seq = r.seqEnd = lastSeq + 1;
        r.time = time;
        r.publishedMask = 0;
        r.metrics = metrics;

        // A full ring overwrites its oldest slot, so a failed write here has already damaged it
        const uint16_t slot = (head + count) % hdr.slots;
        if (!writeSlot(slot, r)) {
            const Record blank = {};
            if (writeSlot(slot, blank))
                scanSlots(); // that slot is gone, the rest of the ring is still good
            else
                usable = false;
            LOG_WARN("Telemetry store: %s slot %u write failed", path, (unsigned)slot);
            return false;
        }

        // The record carries its own order, so this is bookkeeping, not a second commit that could
        // fail and leave the file describing a ring it no longer holds
        lastSeq = r.seq;
        if (count < hdr.slots)
            count++;
        else
            head = (head + 1) % hdr.slots; // the write above overwrote the old head

        return true;
    }

    uint16_t size() const override { return usable ? count : 0; }
    uint16_t capacity() const override { return usable ? hdr.slots : 0; }

    bool at(uint16_t i, TelemetryReading<T> &out) override
    {
        if (!usable || i >= count)
            return false;

        Record r = {};
        if (!readSlot((head + i) % hdr.slots, r) || !recordValid(r))
            return false;

        out.metrics = r.metrics;
        out.time = r.time;
        out.publishedMask = r.publishedMask;
        return true;
    }

    void markPublished(uint16_t i, TelemetryPublishChannel ch) override
    {
        TelemetryReading<T> r;
        if (!at(i, r) || (r.publishedMask & ch))
            return; // already marked: skip the write rather than rewrite the same byte

        const uint8_t mask = r.publishedMask | ch;

        concurrency::LockGuard g(spiLock);
        auto f = fs.open(path, TELEMETRY_STORE_O_RW);
        if (!f)
            return;
        // Just the mask byte; the metrics beside it are unchanged
        f.seek(offsetOf((head + i) % hdr.slots) + offsetof(Record, publishedMask));
        const bool ok = f.write(&mask, 1) == 1;
        f.flush();
        f.close();
        if (!ok)
            LOG_WARN("Telemetry store: %s mask write failed, reading %u may resend", path, (unsigned)i);
    }

    /// False if the file could not be created or reopened; caller should fall back to RAM
    bool isUsable() const { return usable; }
};

/// Deduces FsT so callers naming a non-default filesystem need not spell out the type.
template <typename T, typename FsT> FileTelemetryStore<T, FsT> *makeFileTelemetryStore(const char *path, uint16_t slots, FsT &fs)
{
    return new (std::nothrow) FileTelemetryStore<T, FsT>(path, slots, fs);
}
