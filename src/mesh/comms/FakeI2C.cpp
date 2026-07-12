#ifdef SENSECAP_INDICATOR

#include "FakeI2C.h"
#include "../IndicatorSerial.h"

FakeI2C *FakeWire = new FakeI2C();

void FakeI2C::beginTransmission(uint8_t address)
{
    _txAddress = address;
    _txLen = 0;
    _txPending = true;
}

size_t FakeI2C::write(uint8_t data)
{
    if (!_txPending || _txLen >= MAX_WRITE)
        return 0;
    _txBuf[_txLen++] = data;
    return 1;
}

size_t FakeI2C::write(const uint8_t *data, size_t len)
{
    size_t n = 0;
    while (n < len && write(data[n]))
        n++;
    return n;
}

uint8_t FakeI2C::endTransmission(bool stopBit)
{
    if (!stopBit) {
        // Keep the buffered write pending, it is combined with the following
        // requestFrom() into a single write+read transaction
        return 0;
    }
    uint8_t rv = transact(_txAddress, _txBuf, _txLen, 0);
    _txLen = 0;
    _txPending = false;
    return rv;
}

size_t FakeI2C::requestFrom(uint8_t address, size_t len, bool stopBit)
{
    (void)stopBit;
    if (len > MAX_READ)
        len = MAX_READ;

    const uint8_t *wbuf = NULL;
    size_t wlen = 0;
    if (_txPending && _txAddress == address) {
        wbuf = _txBuf;
        wlen = _txLen;
    }
    uint8_t rv = transact(address, wbuf, wlen, len);
    _txLen = 0;
    _txPending = false;
    return rv == 0 ? _rxLen : 0;
}

int FakeI2C::available()
{
    return (int)(_rxLen - _rxPos);
}

int FakeI2C::read()
{
    return _rxPos < _rxLen ? _rxBuf[_rxPos++] : -1;
}

int FakeI2C::peek()
{
    return _rxPos < _rxLen ? _rxBuf[_rxPos] : -1;
}

// Run one tunneled transaction, returns TwoWire endTransmission error codes:
// 0 success, 1 data too long, 2 NACK on address, 3 NACK on data, 4 other, 5 timeout
uint8_t FakeI2C::transact(uint8_t address, const uint8_t *wbuf, size_t wlen, size_t rlen)
{
    _rxLen = 0;
    _rxPos = 0;

    if (!sensecapIndicator)
        return 4;

    // static: ~4.6KB struct, too large for task stacks; I2C transactions
    // only originate from the cooperative main loop
    static meshtastic_InterdeviceMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.which_data = meshtastic_InterdeviceMessage_i2c_transaction_tag;
    msg.data.i2c_transaction.address = address;
    msg.data.i2c_transaction.read_len = rlen;
    if (wlen > sizeof(msg.data.i2c_transaction.write_data.bytes))
        return 1;
    msg.data.i2c_transaction.write_data.size = wlen;
    if (wlen)
        memcpy(msg.data.i2c_transaction.write_data.bytes, wbuf, wlen);

    meshtastic_I2CResult result = meshtastic_I2CResult_init_zero;
    if (!sensecapIndicator->i2c_transact(msg, &result))
        return 5;

    switch (result.status) {
    case meshtastic_I2CResult_Status_OK:
        _rxLen = result.read_data.size > MAX_READ ? MAX_READ : result.read_data.size;
        memcpy(_rxBuf, result.read_data.bytes, _rxLen);
        return 0;
    case meshtastic_I2CResult_Status_NACK_ADDRESS:
        return 2;
    case meshtastic_I2CResult_Status_NACK_DATA:
        return 3;
    default:
        return 4;
    }
}

#endif // SENSECAP_INDICATOR
