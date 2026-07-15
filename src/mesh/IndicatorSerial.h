#pragma once

#ifdef SENSECAP_INDICATOR

#include "concurrency/Lock.h"
#include "concurrency/OSThread.h"
#include "configuration.h"

#include "mesh/generated/meshtastic/interdevice.pb.h"
#include <atomic>

// Magic number at the start of all MT packets
#define MT_MAGIC_0 0x94
#define MT_MAGIC_1 0xc3

// The header is the magic number plus a 16-bit payload-length field
#define MT_HEADER_SIZE 4

#define PB_BUFSIZE (meshtastic_InterdeviceMessage_size + MT_HEADER_SIZE)

class SensecapIndicator : public concurrency::OSThread
{
  public:
    SensecapIndicator(HardwareSerial &serial);
    int32_t runOnce() override;
    // Standalone send (e.g. NMEA from UARTProxy), takes link_lock to
    // serialize the shared TX buffer against the request methods
    bool send_uplink(const meshtastic_InterdeviceMessage &message);

    // Run one tunneled I2C transaction: an optional write of wlen bytes
    // followed by an optional read of rlen bytes with repeated start. The
    // request is staged under the link lock, so callers need no locking.
    bool i2c_transact(uint8_t address, const uint8_t *wbuf, size_t wlen, size_t rlen, meshtastic_I2CResult *result,
                      uint32_t timeout_ms = 100);

    // Synchronous file operations against the SD card attached to the RP2040.
    // The response (chunk data, file size, status) is written to *out.
    // Returns false when the link is down or the request timed out; a
    // request the co-processor answered is true, with out->status carrying
    // the outcome (FILE_BUSY is worth retrying, the other failures are not).
    bool file_read(const char *path, uint32_t offset, uint32_t length, meshtastic_FileTransfer *out, uint32_t timeout_ms = 1000);
    // Sequential chunked write: create=true starts a new file (offset must be 0),
    // create=false appends (offset must equal the current file size)
    bool file_write(const char *path, uint32_t offset, const uint8_t *data, size_t len, bool create, meshtastic_FileTransfer *out,
                    uint32_t timeout_ms = 1000);
    bool file_remove(const char *path, meshtastic_FileTransfer *out, uint32_t timeout_ms = 1000);
    // List directory entries starting at entry number `offset` (paged,
    // subdirectories get a trailing slash)
    // the first page of a large directory has to walk it to the end to count
    // the entries, so this one gets a longer budget
    bool list_directory(const char *path, uint32_t offset, meshtastic_DirectoryListing *out, uint32_t timeout_ms = 5000);
    // SD card statistics, answered from state the co-processor cached at
    // mount time, so this never waits on the card. used/free are only
    // meaningful once stats_valid is set: the FAT scan behind them runs in
    // the background. `busy` means a card is being mounted right now.
    bool sd_info(meshtastic_SdCardInfo *out, uint32_t timeout_ms = 500);
    // Mount the card, or release it so it can be pulled safely. Answered with
    // the card state as it is right now (a mount is still busy at that point).
    bool sd_command(meshtastic_SdCommand command, meshtastic_SdCardInfo *out, uint32_t timeout_ms = 500);

    // True when the last request was refused by the co-processor rather than
    // lost: retrying it would only be refused again
    bool last_request_nacked() const { return request_nacked; }

    // True once the co-processor has answered and speaks our protocol
    // version. Actively probes it (it never speaks unsolicited unless a GPS
    // module is attached) and pumps the link until a pong arrives or the
    // timeout expires. Used to defer bridge traffic until the co-processor
    // has booted. A version mismatch fails permanently: no request is sent
    // to a co-processor running incompatible firmware.
    bool wait_ready(uint32_t timeout_ms);

  private:
    // The UI task requests map tiles while the main loop pumps the link;
    // every send/pump sequence must hold this lock
    concurrency::Lock link_lock;
    pb_byte_t pb_tx_buf[PB_BUFSIZE];
    pb_byte_t pb_rx_buf[PB_BUFSIZE];
    size_t pb_rx_size = 0; // Number of bytes currently in the buffer
    HardwareSerial *_serial = nullptr;
    uint32_t packets_received = 0;
    // The handshake gates the bridge: no request is sent before the
    // co-processor has answered a ping with the protocol version we speak.
    // A mismatch is permanent, a missing answer is retried by runOnce (the
    // co-processor may boot slower than we do, or reboot on its watchdog).
    bool link_compatible = false;
    bool handshake_done = false;
    uint32_t last_probe = 0;
    // link health diagnostics for the map-tile transfers, reported rarely
    uint32_t link_resyncs = 0;
    uint32_t link_decode_fail = 0;
    uint32_t link_timeouts = 0;
    uint32_t last_link_report = 0;
    meshtastic_I2CResult i2c_result = meshtastic_I2CResult_init_zero;
    bool i2c_result_ready = false;
    // Statically allocated message structs: with 4KB file chunks an
    // InterdeviceMessage is ~4.6KB, too large for task stacks. Both are
    // only touched while link_lock is held, so requests staged by one
    // thread cannot be overwritten by another.
    meshtastic_InterdeviceMessage rx_message;
    meshtastic_InterdeviceMessage tx_message;
    // Response destinations for the file operation in flight
    meshtastic_FileTransfer *pending_file = NULL;
    meshtastic_DirectoryListing *pending_dir = NULL;
    meshtastic_SdCardInfo *pending_sd_info = NULL;
    bool file_response_ready = false;
    bool dir_response_ready = false;
    bool sd_info_ready = false;
    // responses echo the id of the request they answer, so a reply that
    // arrives after its request timed out cannot satisfy a later request
    uint32_t next_request_id = 0;
    uint32_t expected_id = 0;
    // a nack response fails the request in flight without its timeout
    bool request_nacked = false;
    // Number of requesters inside a request/response round trip, each
    // holding link_lock for up to its timeout. runOnce skips its pump while
    // this is nonzero so the cooperative main loop is not blocked on the
    // lock for that long. A counter, not a flag: the UI task and the main
    // loop can both be in a request, and the first one out must not clear
    // the state of the other.
    std::atomic<int> requests_in_flight{0};
    struct InFlight {
        std::atomic<int> &count;
        explicit InFlight(std::atomic<int> &c) : count(c) { count++; }
        ~InFlight() { count--; }
    };
    bool link_ready();
    void probe_link();                          // caller holds link_lock
    void note_handshake(uint32_t peer_version); // caller holds link_lock
    uint32_t stamp_request(meshtastic_InterdeviceMessage &request);
    bool send_uplink_unlocked(const meshtastic_InterdeviceMessage &message);
    // callers hold link_lock (the request was staged in the shared tx_message)
    bool file_request(meshtastic_InterdeviceMessage &request, meshtastic_FileTransfer *out, uint32_t timeout_ms);
    bool wait_response(bool &flag, uint32_t timeout_ms);
    void pump();
    size_t serial_check(char *buf, size_t space_left);
    void check_packet();
    bool handle_packet(size_t payload_len);
    bool send(const char *buf, size_t len);
};

extern SensecapIndicator *sensecapIndicator;

#endif // SENSECAP_INDICATOR
