#pragma once

#include "SinglePortModule.h"
#include "concurrency/NotifiedWorkerThread.h"
#include "configuration.h"
#if defined(ARCH_ESP32)
#include "NodeDB.h"
#include <Arduino.h>
#include <driver/i2s.h>
// #include <driver/adc.h>
#include <functional>
#include <codec2.h>
#include <ButterworthFilter.h>
#include <FastAudioFIFO.h>

#define ADC_BUFFER_SIZE_MAX 320

enum RadioState { standby, rx, tx };

class AudioModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    unsigned char rx_encode_frame[Constants_DATA_PAYLOAD_LEN] = {};
    unsigned char tx_encode_frame[Constants_DATA_PAYLOAD_LEN] = {};
    int16_t speech[ADC_BUFFER_SIZE_MAX] = {};
    int16_t output_buffer[ADC_BUFFER_SIZE_MAX] = {};
    uint16_t adc_buffer[ADC_BUFFER_SIZE_MAX] = {};
    int adc_buffer_size = 0;
    uint16_t adc_buffer_index = 0;
    int tx_encode_frame_index = 0;
    int rx_encode_frame_index = 0;
    int encode_codec_size = 0;
    int encode_frame_size = 0;
    volatile RadioState radio_state = RadioState::rx;
    FastAudioFIFO fifo;
    struct CODEC2* codec2 = NULL;
    int16_t sample;
    adc1_channel_t mic_chan = (adc1_channel_t)0;
    uint8_t rx_raw_audio_value = 127;

    AudioModule();

    /**
     * Send our payload into the mesh
     */
    void sendPayload(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  protected:
    int encode_frame_num = 0;
    bool firstTime = true;


    virtual int32_t runOnce() override;

    virtual MeshPacket *allocReply() override;

    /** Called to handle a particular incoming message
     * @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for it
     */
    virtual ProcessMessage handleReceived(const MeshPacket &mp) override;
};

extern AudioModule *audioModule;

#endif