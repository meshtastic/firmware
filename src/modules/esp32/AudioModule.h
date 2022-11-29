#pragma once

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#if defined(ARCH_ESP32)
#include "NodeDB.h"
#include <Arduino.h>
#include <driver/adc.h>
#include <functional>
#include <codec2.h>
#include <ButterworthFilter.h>
#include <FastAudioFIFO.h>

#define ADC_BUFFER_SIZE 320 // 40ms of voice in 8KHz sampling frequency
#define ENCODE_CODEC2_SIZE 8
#define ENCODE_FRAME_SIZE (ENCODE_CODEC2_SIZE * 5) // 5 codec2 frames of 8 bytes each

class Codec2Thread : public concurrency::NotifiedWorkerThread
{
  struct CODEC2* codec2_state = NULL;
  int16_t output_buffer[ADC_BUFFER_SIZE] = {};

  public:
    Codec2Thread();

  protected:
    virtual void onNotify(uint32_t notification) override;
};

class AudioModule : public SinglePortModule, private concurrency::OSThread
{
  bool firstTime = true;
  hw_timer_t* adcTimer = NULL;
  uint16_t adc_buffer_index = 0;


  public:
    unsigned char rx_encode_frame[ENCODE_FRAME_SIZE] = {};
    unsigned char tx_encode_frame[ENCODE_FRAME_SIZE] = {};
    int tx_encode_frame_index = 0;

    AudioModule();

    /**
     * Send our payload into the mesh
     */
    void sendPayload(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  protected:
    virtual int32_t runOnce() override;

    // void run_codec2();

    virtual MeshPacket *allocReply() override;

    /** Called to handle a particular incoming message
     * @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for it
     */
    virtual ProcessMessage handleReceived(const MeshPacket &mp) override;
};

extern AudioModule *audioModule;
extern Codec2Thread *codec2Thread;

extern uint16_t adc_buffer[ADC_BUFFER_SIZE];
extern uint16_t adc_buffer_index;
extern portMUX_TYPE timerMux;
extern int16_t speech[ADC_BUFFER_SIZE];
enum RadioState { standby, rx, tx };
extern volatile RadioState radio_state;
extern adc1_channel_t mic_chan;

IRAM_ATTR void am_onTimer();
#endif