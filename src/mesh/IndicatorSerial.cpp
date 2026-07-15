#ifdef SENSECAP_INDICATOR

#include "IndicatorSerial.h"
#include "concurrency/LockGuard.h"
#include "mesh/comms/UARTProxy.h"
#include <HardwareSerial.h>
#include <Throttle.h>
#include <pb_decode.h>
#include <pb_encode.h>

SensecapIndicator *sensecapIndicator;

SensecapIndicator::SensecapIndicator(HardwareSerial &serial) : OSThread("SensecapIndicator")
{
    _serial = &serial;
    // Twice the largest frame: the pump runs from the cooperative main
    // loop, which can stall for tens of ms while data keeps arriving
    _serial->setRxBufferSize(2 * PB_BUFSIZE);
    _serial->setPins(SENSOR_RP2040_RXD, SENSOR_RP2040_TXD);
    _serial->begin(SENSOR_BAUD_RATE);
    LOG_DEBUG("Start indicator communication thread");
}

int32_t SensecapIndicator::runOnce()
{
    // A requester is pumping the link itself and holds link_lock for up
    // to its full request timeout; blocking on the lock here would stall
    // every other thread of the cooperative main loop with it
    if (requests_in_flight > 0)
        return (10);
    concurrency::LockGuard guard(&link_lock);
    pump();
    // Keep probing until the co-processor has answered the handshake: it
    // may boot slower than we do, or reboot on its watchdog. Without this
    // the bridge would stay dead for the rest of the session.
    if (!handshake_done)
        probe_link();
    // Diagnostics for the map-tile transfers: a resync or a decode failure
    // means a response was corrupted on the wire, a timeout means it never
    // arrived. Printed at most once every two seconds, and only when
    // something went wrong.
    if ((link_resyncs || link_decode_fail || link_timeouts) && !Throttle::isWithinTimespanMs(last_link_report, 2000)) {
        last_link_report = millis();
        LOG_WARN("link: resync=%u decode_fail=%u timeout=%u", (unsigned)link_resyncs, (unsigned)link_decode_fail,
                 (unsigned)link_timeouts);
        link_resyncs = link_decode_fail = link_timeouts = 0;
    }
    return (10);
}

// Send a ping, rate limited. The co-processor answers with a pong carrying
// the protocol version it speaks. Caller holds link_lock.
void SensecapIndicator::probe_link()
{
    // last_probe 0 means we have not probed at all yet, which must not throttle
    if (last_probe != 0 && Throttle::isWithinTimespanMs(last_probe, 250))
        return;
    meshtastic_InterdeviceMessage &msg = tx_message;
    memset(&msg, 0, sizeof(msg));
    msg.which_data = meshtastic_InterdeviceMessage_ping_tag;
    msg.data.ping = meshtastic_InterdeviceVersion_INTERDEVICE_VERSION_CURRENT;
    stamp_request(msg);
    send_uplink_unlocked(msg);
    last_probe = millis();
}

// Read whatever is available on the link and process complete packets
void SensecapIndicator::pump()
{
    size_t space_left = PB_BUFSIZE - pb_rx_size;
    pb_rx_size += serial_check((char *)pb_rx_buf + pb_rx_size, space_left);
    check_packet();
}

// Pump the link until `flag` goes true, a nack arrives, or the timeout
// expires
bool SensecapIndicator::wait_response(bool &flag, uint32_t timeout_ms)
{
    uint32_t start = millis();
    while (!flag) {
        pump();
        if (flag)
            break;
        if (request_nacked)
            return false; // the other side could not handle the request
        if (!Throttle::isWithinTimespanMs(start, timeout_ms)) {
            link_timeouts++;
            return false;
        }
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
    request_nacked = false;
    // Start the response for this request from an aligned buffer. A byte run
    // lost mid-response (a UART overflow during a 4KB chunk) leaves the
    // assembly misaligned, and without this that poison would outlive the
    // request and cascade into the following chunks of the same tile. The
    // link is single-outstanding and stale responses are dropped by id, so
    // nothing worth keeping is ever pending here (at most a partial NMEA
    // sentence, which self-heals).
    pb_rx_size = 0;
    return next_request_id;
}

// callers hold link_lock: the co-processor has completed the handshake and
// speaks our protocol version. Fails fast instead of timing out per request.
bool SensecapIndicator::link_ready()
{
    return handshake_done && link_compatible;
}

// The co-processor reported the protocol version it speaks, in a pong or in
// the unsolicited ping it sends when it has booted. Anything else than ours
// means its firmware does not match this build, and every request would be
// misinterpreted. Caller holds link_lock.
void SensecapIndicator::note_handshake(uint32_t peer_version)
{
    bool compatible = peer_version == meshtastic_InterdeviceVersion_INTERDEVICE_VERSION_CURRENT;
    bool changed = !handshake_done || compatible != link_compatible;
    if (changed) {
        if (compatible)
            LOG_INFO("RP2040 link up, interdevice protocol v%u", (unsigned)peer_version);
        else
            LOG_ERROR("RP2040 speaks interdevice protocol v%u, this firmware speaks v%u. Flash the matching "
                      "indicator_rp2040 firmware; sensors, GPS and SD card stay disabled",
                      (unsigned)peer_version, (unsigned)meshtastic_InterdeviceVersion_INTERDEVICE_VERSION_CURRENT);
    }
    link_compatible = compatible;
    handshake_done = true;
}

bool SensecapIndicator::i2c_transact(uint8_t address, const uint8_t *wbuf, size_t wlen, size_t rlen, meshtastic_I2CResult *result,
                                     uint32_t timeout_ms)
{
    InFlight busy(requests_in_flight);
    concurrency::LockGuard guard(&link_lock);
    if (!link_ready())
        return false;

    meshtastic_InterdeviceMessage &msg = tx_message;
    memset(&msg, 0, sizeof(msg));
    msg.which_data = meshtastic_InterdeviceMessage_i2c_transaction_tag;
    msg.data.i2c_transaction.address = address;
    msg.data.i2c_transaction.read_len = rlen;
    if (wlen > sizeof(msg.data.i2c_transaction.write_data.bytes))
        return false;
    msg.data.i2c_transaction.write_data.size = wlen;
    if (wlen)
        memcpy(msg.data.i2c_transaction.write_data.bytes, wbuf, wlen);

    stamp_request(msg);
    i2c_result_ready = false;
    if (!send_uplink_unlocked(msg))
        return false;

    if (!wait_response(i2c_result_ready, timeout_ms))
        return false;
    *result = i2c_result;
    i2c_result_ready = false;
    return true;
}

bool SensecapIndicator::file_request(meshtastic_InterdeviceMessage &request, meshtastic_FileTransfer *out, uint32_t timeout_ms)
{
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
    InFlight busy(requests_in_flight);
    concurrency::LockGuard guard(&link_lock);
    if (!link_ready())
        return false;

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
    InFlight busy(requests_in_flight);
    concurrency::LockGuard guard(&link_lock);
    if (!link_ready())
        return false;

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
    InFlight busy(requests_in_flight);
    concurrency::LockGuard guard(&link_lock);
    if (!link_ready())
        return false;

    meshtastic_InterdeviceMessage &msg = tx_message;
    memset(&msg, 0, sizeof(msg));
    msg.which_data = meshtastic_InterdeviceMessage_file_transfer_tag;
    msg.data.file_transfer.operation = meshtastic_FileOperation_DELETE;
    strncpy(msg.data.file_transfer.filepath, path, sizeof(msg.data.file_transfer.filepath) - 1);
    return file_request(msg, out, timeout_ms);
}

bool SensecapIndicator::list_directory(const char *path, uint32_t offset, meshtastic_DirectoryListing *out, uint32_t timeout_ms)
{
    InFlight busy(requests_in_flight);
    concurrency::LockGuard guard(&link_lock);
    if (!link_ready())
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
    InFlight busy(requests_in_flight);
    concurrency::LockGuard guard(&link_lock);
    if (!link_ready())
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

bool SensecapIndicator::sd_command(meshtastic_SdCommand command, meshtastic_SdCardInfo *out, uint32_t timeout_ms)
{
    InFlight busy(requests_in_flight);
    concurrency::LockGuard guard(&link_lock);
    if (!link_ready())
        return false;

    meshtastic_InterdeviceMessage &msg = tx_message;
    memset(&msg, 0, sizeof(msg));
    msg.which_data = meshtastic_InterdeviceMessage_sd_command_tag;
    msg.data.sd_command = command;

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
    InFlight busy(requests_in_flight);
    concurrency::LockGuard guard(&link_lock);
    uint32_t start = millis();
    while (!handshake_done) {
        // The co-processor never sends anything unsolicited unless a GPS
        // module is attached, so waiting passively would leave the bridge
        // down forever on GPS-less units. Ping until it answers; the pong
        // touches no peripherals on the other side and carries the
        // protocol version it speaks. If it does not answer in time,
        // runOnce keeps probing (a slow boot must not disable the bridge
        // for the session), but no request is sent until it does.
        probe_link();
        pump();
        if (handshake_done)
            break;
        if (!Throttle::isWithinTimespanMs(start, timeout_ms))
            return false;
        delay(1);
    }
    return link_compatible;
}

bool SensecapIndicator::send_uplink(const meshtastic_InterdeviceMessage &message)
{
    InFlight busy(requests_in_flight);
    concurrency::LockGuard guard(&link_lock);
    if (!link_ready())
        return false; // nothing is sent to a peer we do not speak the same protocol with
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
            link_resyncs++;
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
    size_t remaining = pb_rx_size - MT_HEADER_SIZE - payload_len;
    memmove(pb_rx_buf, pb_rx_buf + MT_HEADER_SIZE + payload_len, remaining);
    pb_rx_size = remaining;

    if (!status) {
        link_decode_fail++;
        LOG_DEBUG("Decoding failed");
        return false;
    }
    packets_received++;
    switch (message.which_data) {
    case meshtastic_InterdeviceMessage_nmea_tag:
        // send String to NMEA processing
        uartProxy->stuff_buffer(message.data.nmea, strnlen(message.data.nmea, sizeof(message.data.nmea) - 1));
        return true;
    case meshtastic_InterdeviceMessage_i2c_result_tag:
        // response for the transaction i2c_transact() is waiting on
        if (message.id == expected_id) {
            i2c_result = message.data.i2c_result;
            i2c_result_ready = true;
        } else {
            LOG_DEBUG("Drop stale i2c response id=0x%08x", message.id);
        }
        return true;
    case meshtastic_InterdeviceMessage_file_transfer_tag:
        if (pending_file && message.id == expected_id) {
            *pending_file = message.data.file_transfer;
            pending_file = NULL;
            file_response_ready = true;
        } else {
            LOG_DEBUG("Drop stale file response id=0x%08x", message.id);
        }
        return true;
    case meshtastic_InterdeviceMessage_directory_listing_tag:
        if (pending_dir && message.id == expected_id) {
            *pending_dir = message.data.directory_listing;
            pending_dir = NULL;
            dir_response_ready = true;
        } else {
            LOG_DEBUG("Drop stale listing response id=0x%08x", message.id);
        }
        return true;
    case meshtastic_InterdeviceMessage_ping_tag: {
        // The co-processor pings us unsolicited (id 0) when it has booted,
        // which is also how a reboot after its watchdog is noticed. It
        // reports the version it speaks, so this completes the handshake
        // just like a pong does.
        if (message.id == 0) {
            if (handshake_done)
                LOG_WARN("RP2040 rebooted");
            note_handshake(message.data.ping);
        }
        // answer regardless of state. tx_message is safe to reuse: a request
        // in flight was already encoded into pb_tx_buf when it was sent
        meshtastic_InterdeviceMessage &pong = tx_message;
        memset(&pong, 0, sizeof(pong));
        pong.id = message.id;
        pong.which_data = meshtastic_InterdeviceMessage_pong_tag;
        pong.data.pong = meshtastic_InterdeviceVersion_INTERDEVICE_VERSION_CURRENT;
        send_uplink_unlocked(pong);
        return true;
    }
    case meshtastic_InterdeviceMessage_pong_tag:
        // the answer to our probe, carrying the version it speaks
        note_handshake(message.data.pong);
        return true;
    case meshtastic_InterdeviceMessage_nack_tag:
        // A nack for the request in flight is definitive: resending it would
        // only be refused again. An id of 0 means the co-processor could not
        // even decode the frame, which may just as well have been an
        // unrelated NMEA uplink, so that one only ends the wait (the caller
        // may retry it as the transport failure it is).
        if (message.id == expected_id) {
            LOG_WARN("Request 0x%08x nacked by the co-processor", expected_id);
            request_nacked = true;
        } else if (message.id == 0) {
            LOG_WARN("Co-processor could not decode a frame");
        }
        return true;
    case meshtastic_InterdeviceMessage_sd_info_tag:
        if (pending_sd_info && message.id == expected_id) {
            *pending_sd_info = message.data.sd_info;
            pending_sd_info = NULL;
            sd_info_ready = true;
        } else {
            LOG_DEBUG("Drop stale sd info response id=0x%08x", message.id);
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
