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
        // A requester is pumping the link itself and holds link_lock for up
        // to its full request timeout; blocking on the lock here would stall
        // every other thread of the cooperative main loop with it
        if (request_in_flight)
            return (10);
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
    InFlight busy(request_in_flight);
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
    InFlight busy(request_in_flight);
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
    InFlight busy(request_in_flight);
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
    InFlight busy(request_in_flight);
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
    InFlight busy(request_in_flight);
    concurrency::LockGuard guard(&link_lock);
    uint32_t start = millis();
    uint32_t last_probe = 0;
    bool probed = false;
    while (packets_received == 0) {
        // The co-processor never sends anything unsolicited unless a GPS
        // module is attached, so waiting passively would leave the bridge
        // down forever on GPS-less units. Ping until any response marks
        // the link up; pong touches no peripherals on the other side.
        if (!probed || millis() - last_probe >= 250) {
            meshtastic_InterdeviceMessage &msg = tx_message;
            memset(&msg, 0, sizeof(msg));
            msg.which_data = meshtastic_InterdeviceMessage_ping_tag;
            msg.data.ping = true;
            stamp_request(msg);
            send_uplink_unlocked(msg);
            last_probe = millis();
            probed = true;
        }
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

    pb_ostream_t stream = pb_ostream_from_buffer(pb_tx_buf + MT_HEADER_SIZE, PB_BUFSIZE - MT_HEADER_SIZE);
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
    int avail = _serial->available();
    if (avail <= 0)
        return 0;
    if ((size_t)avail > space_left)
        avail = space_left;
    // bulk copy out of the driver's RX buffer; only reads what is available
    return _serial->read((uint8_t *)buf, avail);
}

// Distance to the next byte that could start a frame. Skips the corrupt
// prefix while keeping anything that may be a frame queued behind it (a
// trailing lone MT_MAGIC_0 counts, its successor has not arrived yet).
static size_t scan_magic(const pb_byte_t *buf, size_t len)
{
    for (size_t i = 1; i < len; i++) {
        if (buf[i] == MT_MAGIC_0 && (i + 1 == len || buf[i + 1] == MT_MAGIC_1))
            return i;
    }
    return len;
}

void SensecapIndicator::check_packet()
{
    // process everything buffered; one pump can deliver several frames
    while (pb_rx_size >= MT_HEADER_SIZE) {
        size_t payload_len = (size_t)(pb_rx_buf[2] << 8 | pb_rx_buf[3]);
        if (pb_rx_buf[0] != MT_MAGIC_0 || pb_rx_buf[1] != MT_MAGIC_1 || payload_len + MT_HEADER_SIZE > PB_BUFSIZE) {
            // Corrupt or false header: resync on the next magic instead of
            // flushing, one bad byte must not cost the frames behind it
            size_t skip = scan_magic(pb_rx_buf, pb_rx_size);
            LOG_DEBUG("Bad frame header, dropping %u bytes", (unsigned)skip);
            memmove(pb_rx_buf, pb_rx_buf + skip, pb_rx_size - skip);
            pb_rx_size -= skip;
            continue;
        }

        if (payload_len + MT_HEADER_SIZE > pb_rx_size)
            return; // frame not complete yet

        handle_packet(payload_len);
    }
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
        FakeSerial->stuff_buffer(message.data.nmea, strnlen(message.data.nmea, sizeof(message.data.nmea) - 1));
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
    case meshtastic_InterdeviceMessage_ping_tag: {
        // Liveness probe from the other side, answer regardless of state.
        // tx_message is safe to reuse: a request in flight was already
        // encoded into pb_tx_buf when it was sent
        meshtastic_InterdeviceMessage &pong = tx_message;
        memset(&pong, 0, sizeof(pong));
        pong.id = message.id;
        pong.which_data = meshtastic_InterdeviceMessage_pong_tag;
        pong.data.pong = true;
        send_uplink_unlocked(pong);
        return true;
    }
    case meshtastic_InterdeviceMessage_pong_tag:
        // no payload to deliver: receiving it already counted the packet,
        // which is all wait_ready() is looking for
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