#ifdef SENSECAP_INDICATOR

#include "I2CProxy.h"
#include "../IndicatorSerial.h"
#include "LinkSpiLock.h"

I2CProxy *i2cProxy = new I2CProxy();

// the TFT task publishes itself here while it holds spiLock, see LinkSpiLock.h
volatile TaskHandle_t spiLockHolder = nullptr;

// Transaction state of the calling task. Slots are claimed on first use and
// never released: the set of tasks touching the bridged bus is fixed (main
// loop, UI task).
I2CProxy::Context &I2CProxy::ctx()
{
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    for (size_t i = 0; i < MAX_TASKS; i++) {
        if (_ctx[i].task == self)
            return _ctx[i];
    }
    // claiming a slot must be atomic: two tasks that pick the same one would
    // interleave their transactions in one buffer set
    Context *claimed = nullptr;
    portENTER_CRITICAL(&_claim_mux);
    for (size_t i = 0; i < MAX_TASKS && !claimed; i++) {
        if (_ctx[i].task == nullptr) {
            _ctx[i].task = self;
            claimed = &_ctx[i];
        }
    }
    portEXIT_CRITICAL(&_claim_mux);
    if (claimed)
        return *claimed;
    LOG_WARN("I2CProxy: more tasks than contexts, sharing the last one");
    return _ctx[MAX_TASKS - 1];
}

void I2CProxy::beginTransmission(uint8_t address)
{
    Context &c = ctx();
    c.txAddress = address;
    c.txLen = 0;
    c.txPending = true;
}

size_t I2CProxy::write(uint8_t data)
{
    Context &c = ctx();
    if (!c.txPending || c.txLen >= MAX_WRITE)
        return 0;
    c.txBuf[c.txLen++] = data;
    return 1;
}

size_t I2CProxy::write(const uint8_t *data, size_t len)
{
    size_t n = 0;
    while (n < len && write(data[n]))
        n++;
    return n;
}

uint8_t I2CProxy::endTransmission(bool stopBit)
{
    Context &c = ctx();
    if (!stopBit) {
        // Keep the buffered write pending, it is combined with the following
        // requestFrom() into a single write+read transaction
        return 0;
    }
    uint8_t rv = transact(c, c.txAddress, 0);
    c.txLen = 0;
    c.txPending = false;
    return rv;
}

size_t I2CProxy::requestFrom(uint8_t address, size_t len, bool stopBit)
{
    (void)stopBit;
    Context &c = ctx();
    if (len > MAX_READ)
        len = MAX_READ;

    // a write pending for this address is executed together with the read as
    // one transaction with repeated start
    if (!c.txPending || c.txAddress != address)
        c.txLen = 0;

    uint8_t rv = transact(c, address, len);
    c.txLen = 0;
    c.txPending = false;
    return rv == 0 ? c.rxLen : 0;
}

int I2CProxy::available()
{
    Context &c = ctx();
    return (int)(c.rxLen - c.rxPos);
}

int I2CProxy::read()
{
    Context &c = ctx();
    return c.rxPos < c.rxLen ? c.rxBuf[c.rxPos++] : -1;
}

int I2CProxy::peek()
{
    Context &c = ctx();
    return c.rxPos < c.rxLen ? c.rxBuf[c.rxPos] : -1;
}

// Run one tunneled transaction, returns TwoWire endTransmission error codes:
// 0 success, 1 data too long, 2 NACK on address, 3 NACK on data, 4 other, 5 timeout
uint8_t I2CProxy::transact(Context &c, uint8_t address, size_t rlen)
{
    c.rxLen = 0;
    c.rxPos = 0;

    if (!sensecapIndicator)
        return 4;
    if (c.txLen > MAX_WRITE)
        return 1;

    // the keyboard scanner polls this bus from the LVGL task, which holds the
    // SPI lock the radio needs
    SpiLockBreak spiFree;

    meshtastic_I2CResult result = meshtastic_I2CResult_init_zero;
    if (!sensecapIndicator->i2c_transact(address, c.txBuf, c.txLen, rlen, &result))
        return 5;

    switch (result.status) {
    case meshtastic_I2CResult_Status_OK:
        c.rxLen = result.read_data.size > MAX_READ ? MAX_READ : result.read_data.size;
        memcpy(c.rxBuf, result.read_data.bytes, c.rxLen);
        return 0;
    case meshtastic_I2CResult_Status_NACK_ADDRESS:
        return 2;
    case meshtastic_I2CResult_Status_NACK_DATA:
        return 3;
    default:
        // includes UNSPECIFIED: an empty result must not read as success
        return 4;
    }
}

#endif // SENSECAP_INDICATOR
