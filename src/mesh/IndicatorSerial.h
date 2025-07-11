#pragma once

#ifndef INDICATORSERIAL_H
#define INDICATORSERIAL_H

#ifdef SENSECAP_INDICATOR

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
    bool send_uplink(meshtastic_InterdeviceMessage message);

  private:
    pb_byte_t pb_tx_buf[PB_BUFSIZE];
    pb_byte_t pb_rx_buf[PB_BUFSIZE];
    size_t pb_rx_size = 0; // Number of bytes currently in the buffer
    HardwareSerial *_serial = &Serial2;
    bool running = false;
    size_t serial_check(char *buf, size_t space_left);
    void check_packet();
    bool handle_packet(size_t payload_len);
    bool send(const char *buf, size_t len);
};

extern SensecapIndicator *sensecapIndicator;

#endif // SENSECAP_INDICATOR
#endif // INDICATORSERIAL_H