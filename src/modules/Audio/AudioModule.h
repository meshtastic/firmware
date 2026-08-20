#pragma once

#include "SinglePortModule.h"
#include "concurrency/NotifiedWorkerThread.h"
#include "configuration.h"
#if defined(HAS_AUDIO_CODEC2) && !MESHTASTIC_EXCLUDE_AUDIO

// Voice needs a digital microphone and an amplifier wired up, so it is opted
// into per variant rather than inferred from the radio. The old gate keyed off
// USE_SX1280 as a proxy for "2.4 GHz, therefore no duty cycle", which has
// stopped being true in both directions: newer parts do 2.4 GHz LoRa, and at
// 450 bit/s sub-GHz voice is viable on the faster presets. Regulatory limits
// are now checked at runtime against the band plan instead.

// Codec2 is floating point throughout - quantise.c alone makes 15 log10f and 11
// powf calls per frame. Software float cannot keep up: measured on Cortex-M0+,
// encoding one second of speech costs 3.54 s of CPU. This catches ESP32-S2, C3
// and C6, and RP2040, all of which the old ARCH_ESP32 gate let through.
#if !defined(__FPU_PRESENT) && !defined(__XTENSA__) && !defined(ESP_PLATFORM)
#error "HAS_AUDIO_CODEC2 requires a hardware FPU"
#endif

// Capture and playback are still the ESP32 I2S calls inline in AudioModule.cpp.
// Lifting them behind AudioCapture.h is the next step; until then, enabling this
// flag elsewhere fails loudly rather than mysteriously.
#if !defined(ARCH_ESP32)
#error "HAS_AUDIO_CODEC2: no capture/playback backend for this architecture yet"
#endif

#include "NodeDB.h"
#include <Arduino.h>
#include <ButterworthFilter.h>
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>
#include <codec2.h>
#include <driver/i2s.h>
#include <functional>

enum RadioState { standby, rx, tx };

const char c2_magic[3] = {0xc0, 0xde, 0xc2}; // Magic number for codec2 header

struct c2_header {
    char magic[3];
    char mode;
};

#define ADC_BUFFER_SIZE_MAX 320
#define PTT_PIN 39

#define I2S_PORT I2S_NUM_0

#define AUDIO_MODULE_RX_BUFFER 128
#define AUDIO_MODULE_MODE meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_700

class AudioModule : public SinglePortModule, public Observable<const UIFrameEvent *>, private concurrency::OSThread
{
  public:
    unsigned char rx_encode_frame[meshtastic_Constants_DATA_PAYLOAD_LEN] = {};
    unsigned char tx_encode_frame[meshtastic_Constants_DATA_PAYLOAD_LEN] = {};
    c2_header tx_header = {};
    int16_t speech[ADC_BUFFER_SIZE_MAX] = {};
    int16_t output_buffer[ADC_BUFFER_SIZE_MAX] = {};
    uint16_t adc_buffer[ADC_BUFFER_SIZE_MAX] = {};
    int adc_buffer_size = 0;
    uint16_t adc_buffer_index = 0;
    int tx_encode_frame_index = sizeof(c2_header); // leave room for header
    int rx_encode_frame_index = 0;
    int encode_codec_size = 0;
    int encode_frame_size = 0;
    volatile RadioState radio_state = RadioState::rx;

    struct CODEC2 *codec2 = NULL;
    // int16_t sample;

    AudioModule();

    bool shouldDraw();

    /**
     * Send our payload into the mesh
     */
    void sendPayload(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  protected:
    int encode_frame_num = 0;
    bool firstTime = true;

    virtual int32_t runOnce() override;

    virtual meshtastic_MeshPacket *allocReply() override;

    virtual bool wantUIFrame() override { return this->shouldDraw(); }
    virtual Observable<const UIFrameEvent *> *getUIFrameObservable() override { return this; }
#if !HAS_SCREEN
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#else
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
#endif

    /** Called to handle a particular incoming message
     * @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered
     * for it
     */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};

extern AudioModule *audioModule;

#endif
