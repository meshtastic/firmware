#ifdef SENSECAP_INDICATOR

#include "IndicatorSerial.h"
#include "mesh/comms/FakeUART.h"
#include "mesh/comms/FakeI2C.h"
#include <HardwareSerial.h>
#include <pb_decode.h>
#include <pb_encode.h>

SensecapIndicator *sensecapIndicator;

SensecapIndicator::SensecapIndicator(HardwareSerial &serial) : OSThread("SensecapIndicator") {
    if (!running) {
        _serial = &serial;
        _serial->setRxBufferSize(PB_BUFSIZE);
        _serial->setPins(SENSOR_RP2040_RXD, SENSOR_RP2040_TXD);
        _serial->begin(SENSOR_BAUD_RATE);
        running = true;
        LOG_DEBUG("Start communication thread");
    }
}

int32_t SensecapIndicator::runOnce()
{
    if (running) {
        size_t bytes_read = 0;

        // See if there are any more bytes to add to our buffer.
        size_t space_left = PB_BUFSIZE - pb_rx_size;

        bytes_read = serial_check((char *)pb_rx_buf + pb_rx_size, space_left);

        pb_rx_size += bytes_read;
        check_packet();
        return (10);
    } else {
        LOG_DEBUG("Not running");
        return (1000);
    }
}

bool SensecapIndicator::send_uplink(meshtastic_InterdeviceMessage message)
{
    pb_tx_buf[0] = MT_MAGIC_0;
    pb_tx_buf[1] = MT_MAGIC_1;

    pb_ostream_t stream = pb_ostream_from_buffer(pb_tx_buf + MT_HEADER_SIZE, PB_BUFSIZE);
    if (!pb_encode(&stream, meshtastic_InterdeviceMessage_fields, &message)) {
        LOG_DEBUG("pb_encode failed");
        return false;
    }

    // Store the payload length in the header
    pb_tx_buf[2] = stream.bytes_written / 256;
    pb_tx_buf[3] = stream.bytes_written % 256;

    bool rv = send((const char *)pb_tx_buf, MT_HEADER_SIZE + stream.bytes_written);

    return rv;
}

size_t SensecapIndicator::serial_check(char *buf, size_t space_left)
{
    size_t bytes_read = 0;
    while (_serial->available()) {
        char c = _serial->read();
        *buf++ = c;
        if (++bytes_read >= space_left) {
            LOG_DEBUG("Serial overflow: %d > %d", bytes_read, space_left);
            break;
        }
    }
    return bytes_read;
}

void SensecapIndicator::check_packet()
{
    if (pb_rx_size < MT_HEADER_SIZE) {
        // We don't even have a header yet
        delay(NO_NEWS_PAUSE);
        return;
    }

    if (pb_rx_buf[0] != MT_MAGIC_0 || pb_rx_buf[1] != MT_MAGIC_1) {
        LOG_DEBUG("Got bad magic");
        memset(pb_rx_buf, 0, PB_BUFSIZE);
        pb_rx_size = 0;
        return;
    }

    uint16_t payload_len = pb_rx_buf[2] << 8 | pb_rx_buf[3];
    if (payload_len > PB_BUFSIZE) {
        LOG_DEBUG("Got packet claiming to be ridiculous length");
        return;
    }

    if ((size_t)(payload_len + 4) > pb_rx_size) {
        delay(NO_NEWS_PAUSE);
        return;
    }

    // We have a complete packet, handle it
    handle_packet(payload_len);
}

bool SensecapIndicator::handle_packet(size_t payload_len)
{
    meshtastic_InterdeviceMessage message = meshtastic_InterdeviceMessage_init_zero;

    // Decode the protobuf and shift forward any remaining bytes in the buffer
    // (which, if present, belong to the packet that we're going to process on the
    // next loop)
    pb_istream_t stream = pb_istream_from_buffer(pb_rx_buf + MT_HEADER_SIZE, payload_len);
    bool status = pb_decode(&stream, meshtastic_InterdeviceMessage_fields, &message);
    memmove(pb_rx_buf, pb_rx_buf + MT_HEADER_SIZE + payload_len, PB_BUFSIZE - MT_HEADER_SIZE - payload_len);
    pb_rx_size -= MT_HEADER_SIZE + payload_len;

    if (!status) {
        LOG_DEBUG("Decoding failed");
        return false;
    }
    switch (message.which_data) {
        case meshtastic_InterdeviceMessage_i2c_response_tag:
        if (message.data.i2c_response.status != meshtastic_I2CResponse_Status_OK) {
            LOG_DEBUG("I2C response error: %d", message.data.i2c_response.status);
            return false;
        }
        // send I2C response to FakeI2C
        FakeWire->ingest(message.data.i2c_response);
        return true;
        break;
    case meshtastic_InterdeviceMessage_nmea_tag:
        // send String to NMEA processing
        FakeSerial->stuff_buffer(message.data.nmea, strlen(message.data.nmea));
        return true;
        break;
    default:
        // the other messages really only flow downstream
        LOG_DEBUG("Got a message of unexpected type");
        return false;
    }
}

bool SensecapIndicator::send(const char *buf, size_t len)
{
    size_t wrote = _serial->write(buf, len);
    if (wrote == len)
        return true;
    return false;
}

#endif // SENSECAP_INDICATOR