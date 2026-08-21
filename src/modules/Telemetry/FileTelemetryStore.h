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
/// record-sized write per reading: SD is the intended target, internal flash wears. Each op opens
/// and closes, so nothing is stranded by a sleep, and a drain costs one open per record.
template <typename T, typename FsT = typename std::remove_reference<decltype(FSCom)>::type>
class FileTelemetryStore : public TelemetryStore<T>
{
    static constexpr uint32_t MAGIC = 0x4D544853; // "MTHS"
    static constexpr uint16_t VERSION = 1;

    // Packed: its layout has to be stable to reopen a file.
    struct __attribute__((packed)) Header {
        uint32_t magic;
        uint16_t version;
        uint16_t recordSize; // catches payload-layout drift across firmware builds
        uint16_t slots;
        uint16_t head;
        uint16_t count;
    };

    // Not packed: T holds floats and unaligned access faults. Drift is caught by hdr.recordSize.
    struct Record {
        uint32_t time;
        uint8_t publishedMask;
        T metrics;
    };

    FsT &fs;
    const char *path;
    Header hdr = {};
    bool usable = false;

    static uint32_t offsetOf(uint16_t slot) { return sizeof(Header) + (uint32_t)slot * sizeof(Record); }

    bool writeHeader()
    {
        concurrency::LockGuard g(spiLock);
        auto f = fs.open(path, TELEMETRY_STORE_O_RW);
        if (!f)
            return false;
        f.seek(0);
        const bool ok = f.write((const uint8_t *)&hdr, sizeof(hdr)) == sizeof(hdr);
        f.flush();
        f.close();
        return ok;
    }

    /// Header plus zeroed slots up front, so the file never grows later.
    bool create(uint16_t slots)
    {
        concurrency::LockGuard g(spiLock);
        auto f = fs.open(path, TELEMETRY_STORE_O_CREATE);
        if (!f) {
            LOG_ERROR("Telemetry store: cannot create %s", path);
            return false;
        }

        hdr = {MAGIC, VERSION, (uint16_t)sizeof(Record), slots, 0, 0};
        bool ok = f.write((const uint8_t *)&hdr, sizeof(hdr)) == sizeof(hdr);

        const Record blank = {};
        for (uint16_t i = 0; ok && i < slots; i++)
            ok = f.write((const uint8_t *)&blank, sizeof(blank)) == sizeof(blank);

        f.flush();
        f.close();

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

  public:
    /// @param fs filesystem to keep it on; SD, PSRamFS, anything with the same open/exists subset.
    FileTelemetryStore(const char *path, uint16_t slots, FsT &fs = FSCom) : fs(fs), path(path)
    {
        if (slots == 0)
            return;

        // A different payload layout or ring size cannot be read back - start over.
        if (!fs.exists(path) || !readHeader() || hdr.magic != MAGIC || hdr.version != VERSION ||
            hdr.recordSize != sizeof(Record) || hdr.slots != slots) {
            if (fs.exists(path))
                LOG_INFO("Telemetry store: %s has a different layout, recreating", path);
            usable = create(slots);
            return;
        }

        // Cut-off preallocation
        if (hdr.head >= slots || hdr.count > slots) {
            LOG_WARN("Telemetry store: %s header is inconsistent, recreating", path);
            usable = create(slots);
            return;
        }

        usable = true;
        LOG_INFO("Telemetry store: %s reopened, %u/%u readings", path, (unsigned)hdr.count, (unsigned)hdr.slots);
    }

    FileTelemetryStore(const FileTelemetryStore &) = delete;
    FileTelemetryStore &operator=(const FileTelemetryStore &) = delete;

    bool push(const T &metrics, uint32_t time) override
    {
        if (!usable)
            return false;

        const uint16_t slot = (hdr.head + hdr.count) % hdr.slots;

        Record r = {};
        r.time = time;
        r.publishedMask = 0;
        r.metrics = metrics;

        {
            concurrency::LockGuard g(spiLock);
            auto f = fs.open(path, TELEMETRY_STORE_O_RW);
            if (!f)
                return false;
            f.seek(offsetOf(slot));
            const bool ok = f.write((const uint8_t *)&r, sizeof(r)) == sizeof(r);
            f.flush();
            f.close();
            if (!ok)
                return false;
        }

        if (hdr.count < hdr.slots)
            hdr.count++;
        else
            hdr.head = (hdr.head + 1) % hdr.slots; // the write above overwrote the old head

        return writeHeader();
    }

    uint16_t size() const override { return usable ? hdr.count : 0; }
    uint16_t capacity() const override { return usable ? hdr.slots : 0; }

    bool at(uint16_t i, TelemetryReading<T> &out) override
    {
        if (!usable || i >= hdr.count)
            return false;

        Record r = {};
        {
            concurrency::LockGuard g(spiLock);
            auto f = fs.open(path, FILE_O_READ);
            if (!f)
                return false;
            f.seek(offsetOf((hdr.head + i) % hdr.slots));
            const bool ok = f.read((uint8_t *)&r, sizeof(r)) == sizeof(r);
            f.close();
            if (!ok)
                return false;
        }

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
        f.seek(offsetOf((hdr.head + i) % hdr.slots) + offsetof(Record, publishedMask));
        f.write(&mask, 1);
        f.flush();
        f.close();
    }

    /// False if the file could not be created or reopened; caller should fall back to RAM
    bool isUsable() const { return usable; }
};

/// Deduces FsT so callers naming a non-default filesystem need not spell out the type.
template <typename T, typename FsT> FileTelemetryStore<T, FsT> *makeFileTelemetryStore(const char *path, uint16_t slots, FsT &fs)
{
    return new FileTelemetryStore<T, FsT>(path, slots, fs);
}
