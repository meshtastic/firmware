#ifdef SENSECAP_INDICATOR

#include "IndicatorSerial.h"
#include "concurrency/LockGuard.h"
#include "mesh/comms/FakeUART.h"
#include <HardwareSerial.h>
#include <pb_decode.h>
#include <pb_encode.h>

SensecapIndicator *sensecapIndicator;

SensecapIndicator::SensecapIndicator(HardwareSerial &serial) : OSThread("SensecapIndicator")
{
    if (!running) {
        _serial = &serial;
        // Twice the largest frame: the pump runs from the cooperative main
        // loop, which can stall for tens of ms while data keeps arriving
        _serial->setRxBufferSize(2 * PB_BUFSIZE);
        _serial->setPins(SENSOR_RP2040_RXD, SENSOR_RP2040_TXD);
        _serial->begin(SENSOR_BAUD_RATE);
        running = true;
        LOG_DEBUG("Start indicator communication thread");
    }
}

int32_t SensecapIndicator::runOnce()
{
    if (running) {
        concurrency::LockGuard guard(&link_lock);
        pump();
        return (10);
    } else {
        LOG_DEBUG("Not running");
        return (1000);
    }
}

// Read whatever is available on the link and process complete packets
void SensecapIndicator::pump()
{
    size_t space_left = PB_BUFSIZE - pb_rx_size;
    pb_rx_size += serial_check((char *)pb_rx_buf + pb_rx_size, space_left);
    check_packet();
}

// Pump the link until `flag` goes true or the timeout expires
bool SensecapIndicator::wait_response(bool &flag, uint32_t timeout_ms)
{
    uint32_t start = millis();
    while (!flag) {
        pump();
        if (flag)
            break;
        if (millis() - start >= timeout_ms)
            return false;
        delay(1);
    }
    return true;
}

// assign the next correlation id to a request, skipping the unsolicited 0
uint32_t SensecapIndicator::stamp_request(meshtastic_InterdeviceMessage &request)
{
    if (++next_request_id == 0)
        next_request_id = 1;
    request.id = next_request_id;
    expected_id = next_request_id;
    return next_request_id;
}

bool SensecapIndicator::i2c_transact(meshtastic_InterdeviceMessage &request, meshtastic_I2CResult *result, uint32_t timeout_ms)
{
    concurrency::LockGuard guard(&link_lock);
    if (packets_received == 0)
        return false; // co-processor has never talked to us, fail fast instead of timing out

    stamp_request(request);
    i2c_result_ready = false;
    if (!send_uplink_unlocked(request))
        return false;

    if (!wait_response(i2c_result_ready, timeout_ms))
        return false;
    *result = i2c_result;
    i2c_result_ready = false;
    return true;
}

bool SensecapIndicator::file_request(meshtastic_InterdeviceMessage &request, meshtastic_FileTransfer *out, uint32_t timeout_ms)
{
    concurrency::LockGuard guard(&link_lock);
    if (packets_received == 0)
        return false;

    stamp_request(request);
    file_response_ready = false;
    pending_file = out;
    if (!send_uplink_unlocked(request) || !wait_response(file_response_ready, timeout_ms)) {
        pending_file = NULL;
        return false;
    }
    return true;
}

bool SensecapIndicator::file_read(const char *path, uint32_t offset, uint32_t length, meshtastic_FileTransfer *out,
                                  uint32_t timeout_ms)
{
    meshtastic_InterdeviceMessage &msg = tx_message;
    memset(&msg, 0, sizeof(msg));
    msg.which_data = meshtastic_InterdeviceMessage_file_transfer_tag;
    msg.data.file_transfer.operation = meshtastic_FileOperation_GET;
    strncpy(msg.data.file_transfer.filepath, path, sizeof(msg.data.file_transfer.filepath) - 1);
    msg.data.file_transfer.offset = offset;
    msg.data.file_transfer.length = length;
    return file_request(msg, out, timeout_ms);
}

bool SensecapIndicator::file_write(const char *path, uint32_t offset, const uint8_t *data, size_t len, bool create,
                                   meshtastic_FileTransfer *out, uint32_t timeout_ms)
{
    meshtastic_InterdeviceMessage &msg = tx_message;
    memset(&msg, 0, sizeof(msg));
    msg.which_data = meshtastic_InterdeviceMessage_file_transfer_tag;
    msg.data.file_transfer.operation = create ? meshtastic_FileOperation_POST : meshtastic_FileOperation_PUT;
    strncpy(msg.data.file_transfer.filepath, path, sizeof(msg.data.file_transfer.filepath) - 1);
    msg.data.file_transfer.offset = offset;
    if (len > sizeof(msg.data.file_transfer.filedata.bytes))
        return false;
    msg.data.file_transfer.filedata.size = len;
    memcpy(msg.data.file_transfer.filedata.bytes, data, len);
    return file_request(msg, out, timeout_ms);
}

bool SensecapIndicator::file_remove(const char *path, meshtastic_FileTransfer *out, uint32_t timeout_ms)
{
    meshtastic_InterdeviceMessage &msg = tx_message;
    memset(&msg, 0, sizeof(msg));
    msg.which_data = meshtastic_InterdeviceMessage_file_transfer_tag;
    msg.data.file_transfer.operation = meshtastic_FileOperation_DELETE;
    strncpy(msg.data.file_transfer.filepath, path, sizeof(msg.data.file_transfer.filepath) - 1);
    return file_request(msg, out, timeout_ms);
}

bool SensecapIndicator::list_directory(const char *path, uint32_t offset, meshtastic_DirectoryListing *out, uint32_t timeout_ms)
{
    concurrency::LockGuard guard(&link_lock);
    if (packets_received == 0)
        return false;

    meshtastic_InterdeviceMessage &msg = tx_message;
    memset(&msg, 0, sizeof(msg));
    msg.which_data = meshtastic_InterdeviceMessage_directory_listing_tag;
    strncpy(msg.data.directory_listing.directory, path, sizeof(msg.data.directory_listing.directory) - 1);
    msg.data.directory_listing.offset = offset;

    stamp_request(msg);
    dir_response_ready = false;
    pending_dir = out;
    if (!send_uplink_unlocked(msg) || !wait_response(dir_response_ready, timeout_ms)) {
        pending_dir = NULL;
        return false;
    }
    return true;
}

bool SensecapIndicator::sd_info(meshtastic_SdCardInfo *out, uint32_t timeout_ms)
{
    concurrency::LockGuard guard(&link_lock);
    if (packets_received == 0)
        return false;

    meshtastic_InterdeviceMessage &msg = tx_message;
    memset(&msg, 0, sizeof(msg));
    msg.which_data = meshtastic_InterdeviceMessage_get_sd_info_tag;
    msg.data.get_sd_info = true;

    stamp_request(msg);
    sd_info_ready = false;
    pending_sd_info = out;
    if (!send_uplink_unlocked(msg) || !wait_response(sd_info_ready, timeout_ms)) {
        pending_sd_info = NULL;
        return false;
    }
    return true;
}

bool SensecapIndicator::wait_ready(uint32_t timeout_ms)
{
    concurrency::LockGuard guard(&link_lock);
    uint32_t start = millis();
    while (packets_received == 0) {
        pump();
        if (packets_received > 0)
            break;
        if (millis() - start >= timeout_ms)
            return false;
        delay(1);
    }
    return true;
}

bool SensecapIndicator::send_uplink(const meshtastic_InterdeviceMessage &message)
{
    concurrency::LockGuard guard(&link_lock);
    return send_uplink_unlocked(message);
}

// callers must hold link_lock: pb_tx_buf is shared
bool SensecapIndicator::send_uplink_unlocked(const meshtastic_InterdeviceMessage &message)
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
    while (bytes_read < space_left && _serial->available()) {
        buf[bytes_read++] = _serial->read();
    }
    return bytes_read;
}

void SensecapIndicator::check_packet()
{
    if (pb_rx_size < MT_HEADER_SIZE) {
        // We don't even have a header yet
        return;
    }

    if (pb_rx_buf[0] != MT_MAGIC_0 || pb_rx_buf[1] != MT_MAGIC_1) {
        LOG_DEBUG("Got bad magic");
        memset(pb_rx_buf, 0, PB_BUFSIZE);
        pb_rx_size = 0;
        return;
    }

    uint16_t payload_len = pb_rx_buf[2] << 8 | pb_rx_buf[3];
    if ((size_t)payload_len + MT_HEADER_SIZE > PB_BUFSIZE) {
        // oversized frame can never complete, resync on the next magic
        LOG_DEBUG("Got packet claiming to be ridiculous length");
        memset(pb_rx_buf, 0, PB_BUFSIZE);
        pb_rx_size = 0;
        return;
    }

    if ((size_t)(payload_len + 4) > pb_rx_size) {
        // Packet not complete yet
        return;
    }

    // We have a complete packet, handle it
    handle_packet(payload_len);
}

bool SensecapIndicator::handle_packet(size_t payload_len)
{
    meshtastic_InterdeviceMessage &message = rx_message;
    memset(&message, 0, sizeof(message));

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
    packets_received++;
    switch (message.which_data) {
    case meshtastic_InterdeviceMessage_nmea_tag:
        // send String to NMEA processing
        FakeSerial->stuff_buffer(message.data.nmea, strlen(message.data.nmea));
        return true;
    case meshtastic_InterdeviceMessage_i2c_result_tag:
        // response for the transaction i2c_transact() is waiting on
        if (message.id == expected_id) {
            i2c_result = message.data.i2c_result;
            i2c_result_ready = true;
        } else {
            LOG_DEBUG("Drop stale i2c response id=%u", message.id);
        }
        return true;
    case meshtastic_InterdeviceMessage_file_transfer_tag:
        if (pending_file && message.id == expected_id) {
            *pending_file = message.data.file_transfer;
            pending_file = NULL;
            file_response_ready = true;
        } else {
            LOG_DEBUG("Drop stale file response id=%u", message.id);
        }
        return true;
    case meshtastic_InterdeviceMessage_directory_listing_tag:
        if (pending_dir && message.id == expected_id) {
            *pending_dir = message.data.directory_listing;
            pending_dir = NULL;
            dir_response_ready = true;
        } else {
            LOG_DEBUG("Drop stale listing response id=%u", message.id);
        }
        return true;
    case meshtastic_InterdeviceMessage_sd_info_tag:
        if (pending_sd_info && message.id == expected_id) {
            *pending_sd_info = message.data.sd_info;
            pending_sd_info = NULL;
            sd_info_ready = true;
        } else {
            LOG_DEBUG("Drop stale sd info response id=%u", message.id);
        }
        return true;
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