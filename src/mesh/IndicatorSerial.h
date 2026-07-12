#pragma once

#ifndef INDICATORSERIAL_H
#define INDICATORSERIAL_H

#ifdef SENSECAP_INDICATOR

#include "concurrency/Lock.h"
#include "concurrency/OSThread.h"
#include "configuration.h"

#include "mesh/generated/meshtastic/interdevice.pb.h"

// Magic number at the start of all MT packets
#define MT_MAGIC_0 0x94
#define MT_MAGIC_1 0xc3

// The header is the magic number plus a 16-bit payload-length field
#define MT_HEADER_SIZE 4

// Wait this many msec if there's nothing new on the channel
#define NO_NEWS_PAUSE 25

#define PB_BUFSIZE meshtastic_InterdeviceMessage_size + MT_HEADER_SIZE

class SensecapIndicator : public concurrency::OSThread
{
  public:
    SensecapIndicator(HardwareSerial &serial);
    int32_t runOnce() override;
    // Standalone send (e.g. NMEA from FakeUART), takes link_lock to
    // serialize the shared TX buffer against the request methods
    bool send_uplink(const meshtastic_InterdeviceMessage &message);

    // Send a request and pump the link until the matching response arrives.
    // Cooperative threading means nothing else runs while we wait, so the
    // response cannot be consumed behind our back.
    bool i2c_transact(meshtastic_InterdeviceMessage &request, meshtastic_I2CResult *result, uint32_t timeout_ms = 100);

    // Synchronous file operations against the SD card attached to the RP2040.
    // The response (chunk data, file size, status) is written to *out.
    // Returns false when the link is down or the request timed out.
    bool file_read(const char *path, uint32_t offset, uint32_t length, meshtastic_FileTransfer *out, uint32_t timeout_ms = 1000);
    // Sequential chunked write: create=true starts a new file (offset must be 0),
    // create=false appends (offset must equal the current file size)
    bool file_write(const char *path, uint32_t offset, const uint8_t *data, size_t len, bool create, meshtastic_FileTransfer *out,
                    uint32_t timeout_ms = 1000);
    bool file_remove(const char *path, meshtastic_FileTransfer *out, uint32_t timeout_ms = 1000);
    // List directory entries starting at entry number `offset` (paged,
    // subdirectories get a trailing slash)
    bool list_directory(const char *path, uint32_t offset, meshtastic_DirectoryListing *out, uint32_t timeout_ms = 1000);
    // SD card statistics, answered from a cache on the co-processor.
    // used/free read zero while its background FAT scan is still running
    bool sd_info(meshtastic_SdCardInfo *out, uint32_t timeout_ms = 2000);

    // True once at least one valid packet was received from the RP2040.
    // Pumps the link until then or until the timeout expires. Used to defer
    // bridge traffic until the co-processor has booted.
    bool wait_ready(uint32_t timeout_ms);

  private:
    // The UI task requests map tiles while the main loop pumps the link;
    // every send/pump sequence must hold this lock
    concurrency::Lock link_lock;
    pb_byte_t pb_tx_buf[PB_BUFSIZE];
    pb_byte_t pb_rx_buf[PB_BUFSIZE];
    size_t pb_rx_size = 0; // Number of bytes currently in the buffer
    HardwareSerial *_serial = &Serial2;
    bool running = false;
    uint32_t packets_received = 0;
    meshtastic_I2CResult i2c_result = meshtastic_I2CResult_init_zero;
    bool i2c_result_ready = false;
    // Statically allocated message structs: with 4KB file chunks an
    // InterdeviceMessage is ~4.6KB, too large for task stacks. rx is used
    // by handle_packet (under link_lock); tx only by the file/dir/sd
    // requests, which all originate from the single UI task.
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
    uint32_t stamp_request(meshtastic_InterdeviceMessage &request);
    bool send_uplink_unlocked(const meshtastic_InterdeviceMessage &message);
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
#endif // INDICATORSERIAL_H