#pragma once

#include "util/ILog.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <Arduino.h>

#include "TDisplayP4Board.h"

/**
 * Goodix GT9895 capacitive touch controller, polled (INT and RESET are on the IO expander).
 * Protocol follows LilyGO cpp_bus_driver chip/i2c/touch/gt9895.{h,cpp}.
 * The bus is shared with the main task: every transfer is one Arduino I2C HAL call (HAL lock held
 * for that call only) on a bus the firmware has already started; this class never configures the bus.
 */
struct Touch_GT9895 : public lgfx::ITouch {
    Touch_GT9895(void)
    {
        _cfg.i2c_port = 0;
        _cfg.pin_scl = -1; // pins and clock belong to the firmware
        _cfg.pin_sda = -1;
        _cfg.i2c_addr = 0x5D;
        _cfg.freq = 400000;
        _cfg.x_min = 0;
        _cfg.x_max = 1059; // raw range 1060 x 2400 (LilyGO kRawCoordinateWidth/Height)
        _cfg.y_min = 0;
        _cfg.y_max = 2399;
        _cfg.pin_int = -1;
        _cfg.pin_rst = -1;
        _cfg.pin_cs = -1;
        _cfg.bus_shared = false;
        _cfg.offset_rotation = 0;
    }

    void setBusNum(uint8_t busNum)
    {
        _busNum = busNum;
        _cfg.i2c_port = (int8_t)busNum;
    }

    bool ready(void) const { return _inited; }

    bool init(void) override
    {
        if (_inited)
            return true;
        _touchAddr = 0;
        _count = 0;
        _failCount = 0;
        _backoff = false;

        tDisplayP4TouchReset();

        uint8_t fw[GT9895_FW_INFO_SIZE];
        if (!readFwInfo(fw)) {
            ILOG_ERROR("GT9895: no valid firmware info at 0x%02x (I2C error, checksum or product id)", (unsigned)_cfg.i2c_addr);
            return false;
        }
        if (!readIcInfo()) {
            ILOG_ERROR("GT9895: IC info block unreadable or invalid, touch data address unknown");
            return false;
        }

        char productId[9];
        memcpy(productId, &fw[10], 8);
        productId[8] = '\0';
        ILOG_INFO("GT9895 %s: sensor %u, patch 0x%08x, touch data at 0x%08x", productId, (unsigned)fw[23],
                  (unsigned)le32(&fw[18]), (unsigned)_touchAddr);
        _inited = true;
        return true;
    }

    // Touch is the only wake source of the screen: the controller stays awake
    void wakeup(void) override {}
    void sleep(void) override {}

    uint_fast8_t getTouchRaw(lgfx::touch_point_t *tp, uint_fast8_t count) override
    {
        if (!_inited || tp == nullptr || count == 0 || !tDisplayP4I2cUsable())
            return 0;

        const uint32_t now = millis();
        if (_backoff) {
            if (now - _backoffMs < GT9895_BACKOFF_MS)
                return 0;
            _backoff = false; // _failCount stays at the limit: one more failure pauses again
        }

        poll(count, now);

        // TODO(hw-verify): report rate against the 20 ms poll; contacts are held this long between reports
        if (_count && now - _reportMs > GT9895_STALE_MS)
            _count = 0;
        const uint_fast8_t n = _count < count ? _count : count;
        for (uint_fast8_t i = 0; i < n; ++i)
            tp[i] = _pts[i];
        return n;
    }

  private:
    static constexpr uint32_t GT9895_I2C_TIMEOUT_MS = 20;
    static constexpr size_t GT9895_MAX_CHUNK = 64; // keeps one transfer well below the timeout
    static constexpr uint32_t GT9895_FW_INFO_ADDR = 0x00010014;
    static constexpr size_t GT9895_FW_INFO_SIZE = 28;
    static constexpr uint32_t GT9895_IC_INFO_ADDR = 0x00010070;
    static constexpr size_t GT9895_IC_INFO_MIN_SIZE = 2 + 16 + 10 + 4 + 5 + 48 + 2;
    static constexpr size_t GT9895_IC_INFO_MAX_SIZE = 1024;
    static constexpr size_t GT9895_HEADER_SIZE = 8;
    static constexpr size_t GT9895_CONTACT_SIZE = 8;
    static constexpr size_t GT9895_CHECKSUM_SIZE = 2;
    static constexpr size_t GT9895_MAX_CONTACTS = 10;
    static constexpr size_t GT9895_PREFETCHED_CONTACTS = 2;
    static constexpr size_t GT9895_SINGLE_REPORT_SIZE = GT9895_HEADER_SIZE + GT9895_CONTACT_SIZE + GT9895_CHECKSUM_SIZE;
    static constexpr size_t GT9895_MULTI_REPORT_SIZE =
        GT9895_HEADER_SIZE + GT9895_PREFETCHED_CONTACTS * GT9895_CONTACT_SIZE + GT9895_CHECKSUM_SIZE;
    static constexpr size_t GT9895_MAX_REPORT_SIZE =
        GT9895_HEADER_SIZE + GT9895_MAX_CONTACTS * GT9895_CONTACT_SIZE + GT9895_CHECKSUM_SIZE;
    static constexpr uint8_t GT9895_EVENT_TOUCH = 0x80;
    static constexpr uint8_t GT9895_TYPE_STYLUS_HOVER = 0x01;
    static constexpr uint8_t GT9895_TYPE_STYLUS = 0x03;
    static constexpr uint8_t GT9895_FAIL_LIMIT = 5;
    static constexpr uint32_t GT9895_BACKOFF_MS = 500;
    static constexpr uint32_t GT9895_STALE_MS = 200;

    static uint16_t le16(const uint8_t *d) { return (uint16_t)(d[0] | (d[1] << 8)); }
    static uint32_t le32(const uint8_t *d)
    {
        return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    }

    // 16-bit sum of all bytes but the last two equals the last two, little-endian
    static bool checksumOk(const uint8_t *d, size_t len)
    {
        if (len < GT9895_CHECKSUM_SIZE)
            return false;
        uint32_t sum = 0;
        for (size_t i = 0; i < len - GT9895_CHECKSUM_SIZE; ++i)
            sum += d[i];
        return (uint16_t)sum == le16(&d[len - GT9895_CHECKSUM_SIZE]);
    }

    // 32-bit big-endian register address, repeated start, read
    bool readReg(uint32_t reg, uint8_t *dst, size_t len)
    {
        while (len) {
            const size_t n = len > GT9895_MAX_CHUNK ? GT9895_MAX_CHUNK : len;
            const uint8_t addr[4] = {(uint8_t)(reg >> 24), (uint8_t)(reg >> 16), (uint8_t)(reg >> 8), (uint8_t)reg};
            size_t got = 0;
            if (i2cWriteReadNonStop(_busNum, (uint16_t)_cfg.i2c_addr, addr, sizeof(addr), dst, n, GT9895_I2C_TIMEOUT_MS, &got) !=
                    ESP_OK ||
                got != n)
                return false;
            reg += n;
            dst += n;
            len -= n;
        }
        return true;
    }

    bool readFwInfo(uint8_t *d)
    {
        for (uint8_t attempt = 0; attempt < 2; ++attempt) {
            if (attempt)
                delay(5);
            if (readReg(GT9895_FW_INFO_ADDR, d, GT9895_FW_INFO_SIZE) && checksumOk(d, GT9895_FW_INFO_SIZE))
                return d[10] == '9' && d[11] == '8' && d[12] == '9' && d[13] == '5';
        }
        return false;
    }

    // The touch data address is not a constant: it comes from the IC info block
    // TODO(hw-verify): the block is read in chunks at increasing addresses, LilyGO reads it in one transfer
    bool readIcInfo(void)
    {
        uint8_t lenBytes[2];
        if (!readReg(GT9895_IC_INFO_ADDR, lenBytes, sizeof(lenBytes)))
            return false;
        const size_t len = le16(lenBytes);
        if (len < GT9895_IC_INFO_MIN_SIZE || len > GT9895_IC_INFO_MAX_SIZE)
            return false;
        uint8_t *d = static_cast<uint8_t *>(malloc(len));
        if (d == nullptr)
            return false;
        const bool ok = readReg(GT9895_IC_INFO_ADDR, d, len) && checksumOk(d, len) && parseIcInfo(d, len);
        free(d);
        return ok;
    }

    bool parseIcInfo(const uint8_t *d, size_t len)
    {
        const size_t end = len - GT9895_CHECKSUM_SIZE;
        size_t off = 2 + 16 + 10 + 4;  // length, version block, feature words, fixed parameters
        for (uint8_t i = 0; i < 5; ++i) { // five arrays: count byte + count * 2 bytes
            if (off >= end)
                return false;
            const size_t bytes = (size_t)d[off++] * 2;
            if (bytes > end - off)
                return false;
            off += bytes;
        }
        if (end - off < 48)
            return false;
        const uint32_t cmdAddr = le32(&d[off]);
        const uint16_t cmdMaxLen = le16(&d[off + 4]);
        const uint32_t touchAddr = le32(&d[off + 44]);
        if (cmdAddr == 0 || cmdMaxLen < 6 || touchAddr == 0)
            return false;
        _touchAddr = touchAddr;
        return true;
    }

    // Poll path only: after GT9895_FAIL_LIMIT consecutive failures the bus is left alone for a while
    bool noteTransfer(bool ok, uint32_t now)
    {
        if (ok) {
            _failCount = 0;
            return true;
        }
        if (_failCount < GT9895_FAIL_LIMIT)
            ++_failCount;
        if (_failCount >= GT9895_FAIL_LIMIT) {
            _backoff = true;
            _backoffMs = now;
            ILOG_WARN("GT9895: I2C errors, touch paused for %u ms", (unsigned)GT9895_BACKOFF_MS);
        }
        return false;
    }

    // Clears the event flag so that the controller can post the next report
    bool ackEvent(uint32_t now)
    {
        const uint8_t pkt[5] = {(uint8_t)(_touchAddr >> 24), (uint8_t)(_touchAddr >> 16), (uint8_t)(_touchAddr >> 8),
                                (uint8_t)_touchAddr, 0x00};
        return noteTransfer(i2cWrite(_busNum, (uint16_t)_cfg.i2c_addr, pkt, sizeof(pkt), GT9895_I2C_TIMEOUT_MS) == ESP_OK, now);
    }

    // Updates _pts/_count from one report. Any error drops the held contacts (reported as released).
    void poll(uint_fast8_t count, uint32_t now)
    {
        const bool single = count == 1;
        const size_t first = single ? GT9895_SINGLE_REPORT_SIZE : GT9895_MULTI_REPORT_SIZE;
        uint8_t r[GT9895_MAX_REPORT_SIZE];

        if (!noteTransfer(readReg(_touchAddr, r, first), now)) {
            _count = 0;
            return;
        }
        if (r[0] == 0)
            return; // nothing new, nothing to acknowledge
        if (!checksumOk(r, GT9895_HEADER_SIZE)) {
            _count = 0;
            return;
        }
        const uint8_t n = r[2] & 0x0F;
        if ((r[0] & GT9895_EVENT_TOUCH) == 0 || n == 0) { // other event or release
            _count = 0;
            ackEvent(now);
            return;
        }
        if (n > GT9895_MAX_CONTACTS) {
            _count = 0;
            return;
        }
        // Single-touch mode never reads past the first contact
        if (!single && n > GT9895_PREFETCHED_CONTACTS) {
            const size_t total = GT9895_HEADER_SIZE + n * GT9895_CONTACT_SIZE + GT9895_CHECKSUM_SIZE;
            if (!noteTransfer(readReg(_touchAddr + first, r + first, total - first), now)) {
                _count = 0;
                return;
            }
        }
        // A stylus payload is always two records
        const uint8_t type = r[GT9895_HEADER_SIZE] & 0x0F;
        const size_t payloadContacts =
            (type == GT9895_TYPE_STYLUS || type == GT9895_TYPE_STYLUS_HOVER) ? GT9895_PREFETCHED_CONTACTS : n;
        const size_t payloadSize = payloadContacts * GT9895_CONTACT_SIZE + GT9895_CHECKSUM_SIZE;
        const bool payloadRead = !single || GT9895_HEADER_SIZE + payloadSize <= first;
        if (payloadRead && !checksumOk(&r[GT9895_HEADER_SIZE], payloadSize)) {
            _count = 0; // left pending: read again on the next poll
            return;
        }

        const uint8_t parsed = n < count ? n : (uint8_t)count;
        for (uint8_t i = 0; i < parsed; ++i) {
            const uint8_t *c = &r[GT9895_HEADER_SIZE + i * GT9895_CONTACT_SIZE];
            // TODO(hw-verify): raw axes assumed to match the panel axes; id assumed stable per finger
            const uint16_t x = le16(&c[2]);
            const uint16_t y = le16(&c[4]);
            _pts[i].id = c[0] >> 4;
            _pts[i].x = (int16_t)(x > _cfg.x_max ? _cfg.x_max : x); // clamped to the raw range, as LilyGO does
            _pts[i].y = (int16_t)(y > _cfg.y_max ? _cfg.y_max : y);
            _pts[i].size = le16(&c[6]);
        }
        _count = parsed;
        _reportMs = now;
        if (!ackEvent(now))
            _count = 0;
    }

    uint8_t _busNum = 0; // Arduino I2C bus number; 0 is the bus the firmware starts in earlyInitVariant()
    uint32_t _touchAddr = 0;
    lgfx::touch_point_t _pts[GT9895_MAX_CONTACTS];
    uint8_t _count = 0;
    uint32_t _reportMs = 0;
    uint8_t _failCount = 0;
    bool _backoff = false;
    uint32_t _backoffMs = 0;
};
