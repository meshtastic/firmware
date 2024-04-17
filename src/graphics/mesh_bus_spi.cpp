// This code has been copied from LovyanGFX to make the SPI device selectable for touchscreens.
// Ideally this could eventually be an inherited class from BUS_SPI,
// but currently too many internal objects are set private.

#include "configuration.h"
#if ARCH_PORTDUINO
#include "lgfx/v1/misc/pixelcopy.hpp"
#include "main.h"
#include "mesh_bus_spi.h"
#include <Arduino.h>
#include <SPI.h>

namespace lgfx
{
inline namespace v1
{
//----------------------------------------------------------------------------

void Mesh_Bus_SPI::config(const config_t &config)
{
    _cfg = config;

    if (_cfg.pin_dc >= 0) {
        pinMode(_cfg.pin_dc, pin_mode_t::output);
        gpio_hi(_cfg.pin_dc);
    }
}

bool Mesh_Bus_SPI::init(void)
{
    dc_h();
    pinMode(_cfg.pin_dc, pin_mode_t::output);
    if (SPIName != "")
        PrivateSPI->begin(SPIName.c_str());
    else
        PrivateSPI->begin();
    return true;
}

void Mesh_Bus_SPI::release(void)
{
    PrivateSPI->end();
}

void Mesh_Bus_SPI::spi_device(HardwareSPI *newSPI, std::string newSPIName)
{
    PrivateSPI = newSPI;
    SPIName = newSPIName;
}
void Mesh_Bus_SPI::beginTransaction(void)
{
    dc_h();
    SPISettings setting(_cfg.freq_write, MSBFIRST, _cfg.spi_mode);
    PrivateSPI->beginTransaction(setting);
}

void Mesh_Bus_SPI::endTransaction(void)
{
    PrivateSPI->endTransaction();
    dc_h();
}

void Mesh_Bus_SPI::beginRead(void)
{
    PrivateSPI->endTransaction();
    // SPISettings setting(_cfg.freq_read, BitOrder::MSBFIRST, _cfg.spi_mode, false);
    SPISettings setting(_cfg.freq_read, MSBFIRST, _cfg.spi_mode);
    PrivateSPI->beginTransaction(setting);
}

void Mesh_Bus_SPI::endRead(void)
{
    PrivateSPI->endTransaction();
    beginTransaction();
}

void Mesh_Bus_SPI::wait(void) {}

bool Mesh_Bus_SPI::busy(void) const
{
    return false;
}

bool Mesh_Bus_SPI::writeCommand(uint32_t data, uint_fast8_t bit_length)
{
    dc_l();
    PrivateSPI->transfer((uint8_t *)&data, bit_length >> 3);
    dc_h();
    return true;
}

void Mesh_Bus_SPI::writeData(uint32_t data, uint_fast8_t bit_length)
{
    PrivateSPI->transfer((uint8_t *)&data, bit_length >> 3);
}

void Mesh_Bus_SPI::writeDataRepeat(uint32_t data, uint_fast8_t bit_length, uint32_t length)
{
    const uint8_t dst_bytes = bit_length >> 3;
    uint32_t limit = (dst_bytes == 3) ? 12 : 16;
    auto buf = _flip_buffer.getBuffer(512);
    size_t fillpos = 0;
    reinterpret_cast<uint32_t *>(buf)[0] = data;
    fillpos += dst_bytes;
    uint32_t len;
    do {
        len = ((length - 1) % limit) + 1;
        if (limit <= 64)
            limit <<= 1;

        while (fillpos < len * dst_bytes) {
            memcpy(&buf[fillpos], buf, fillpos);
            fillpos += fillpos;
        }

        PrivateSPI->transfer(buf, len * dst_bytes);
    } while (length -= len);
}

void Mesh_Bus_SPI::writePixels(pixelcopy_t *param, uint32_t length)
{
    const uint8_t dst_bytes = param->dst_bits >> 3;
    uint32_t limit = (dst_bytes == 3) ? 12 : 16;
    uint32_t len;
    do {
        len = ((length - 1) % limit) + 1;
        if (limit <= 32)
            limit <<= 1;
        auto buf = _flip_buffer.getBuffer(len * dst_bytes);
        param->fp_copy(buf, 0, len, param);
        PrivateSPI->transfer(buf, len * dst_bytes);
    } while (length -= len);
}

void Mesh_Bus_SPI::writeBytes(const uint8_t *data, uint32_t length, bool dc, bool use_dma)
{
    if (dc)
        dc_h();
    else
        dc_l();
    PrivateSPI->transfer(const_cast<uint8_t *>(data), length);
    if (!dc)
        dc_h();
}

uint32_t Mesh_Bus_SPI::readData(uint_fast8_t bit_length)
{
    uint32_t res = 0;
    bit_length >>= 3;
    if (!bit_length)
        return res;
    int idx = 0;
    do {
        res |= PrivateSPI->transfer(0) << idx;
        idx += 8;
    } while (--bit_length);
    return res;
}

bool Mesh_Bus_SPI::readBytes(uint8_t *dst, uint32_t length, bool use_dma)
{
    do {
        dst[0] = PrivateSPI->transfer(0);
        ++dst;
    } while (--length);
    return true;
}

void Mesh_Bus_SPI::readPixels(void *dst, pixelcopy_t *param, uint32_t length)
{
    uint32_t bytes = param->src_bits >> 3;
    uint32_t dstindex = 0;
    uint32_t len = 4;
    uint8_t buf[24];
    param->src_data = buf;
    do {
        if (len > length)
            len = length;
        readBytes((uint8_t *)buf, len * bytes, true);
        param->src_x = 0;
        dstindex = param->fp_copy(dst, dstindex, dstindex + len, param);
        length -= len;
    } while (length);
}

} // namespace v1
} // namespace lgfx
#endif