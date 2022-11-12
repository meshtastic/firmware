#pragma once

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "NodeDB.h"
#include <Arduino.h>
#include <driver/adc.h>
#include <functional>
#if defined(ARCH_ESP32) && defined(USE_SX1280)
#include <codec2.h>
#include <ButterworthFilter.h>
#include <FastAudioFIFO.h>
#endif

#define ADC_BUFFER_SIZE 320 // 40ms of voice in 8KHz sampling frequency
#define ENCODE_FRAME_SIZE 40 // 5 codec2 frames of 8 bytes each

class AudioModule : public SinglePortModule, private concurrency::OSThread
{
#if defined(ARCH_ESP32) && defined(USE_SX1280)
  bool firstTime = 1;
  hw_timer_t* adcTimer = NULL;
  uint16_t adc_buffer[ADC_BUFFER_SIZE] = {};
  int16_t speech[ADC_BUFFER_SIZE] = {};
  int16_t output_buffer[ADC_BUFFER_SIZE] = {};
  unsigned char rx_encode_frame[ENCODE_FRAME_SIZE] = {};
  unsigned char tx_encode_frame[ENCODE_FRAME_SIZE] = {};
  int tx_encode_frame_index = 0;
  FastAudioFIFO audio_fifo;
  uint16_t adc_buffer_index = 0;
  adc1_channel_t mic_chan = (adc1_channel_t)0;
  struct CODEC2* codec2_state = NULL;

  enum State
  {
	  standby, rx, tx 
  };
  volatile State state = State::tx;

  public:
    AudioModule();

    /**
     * Send our payload into the mesh
     */
    void sendPayload(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  protected:
    virtual int32_t runOnce() override;

    static void handleInterrupt();

    void onTimer();

    void run_codec2();

    virtual MeshPacket *allocReply() override;

    /** Called to handle a particular incoming message
     * @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for it
     */
    virtual ProcessMessage handleReceived(const MeshPacket &mp) override;
#endif
};

extern AudioModule *audioModule;

