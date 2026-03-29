#pragma once

#include "SinglePortModule.h"
#include "concurrency/NotifiedWorkerThread.h"
#include "configuration.h"
#if defined(ARCH_ESP32) && defined(USE_SX1280)
#include "NodeDB.h"
#include <Arduino.h>
#include <ButterworthFilter.h>
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>
#include <codec2.h>
#include <driver/i2s.h>
#include <freertos/queue.h>
#include <functional>
#include "input/InputBroker.h"

enum RadioState { standby, rx, tx };

const char c2_magic[3] = {0xc0, 0xde, 0xc2}; // Magic number for codec2 header

struct c2_header {
    char magic[3];
    char mode;
};

// Wire header: 4-byte c2_header + 1-byte packet sequence number.
#define AUDIO_HEADER_SIZE (sizeof(c2_header) + 1)

#define ADC_BUFFER_SIZE_MAX 320

#ifdef AUDIO_PTT_PIN
#define PTT_PIN AUDIO_PTT_PIN
#else
#define PTT_PIN 39
#endif

#ifdef AUDIO_I2S_DUAL
// Dual I2S mode: separate buses for mic (RX) and speaker (TX)
#define I2S_PORT_MIC I2S_NUM_0
#define I2S_PORT_SPK I2S_NUM_1
#else
// Single I2S mode: shared bus for both mic and speaker
#define I2S_PORT I2S_NUM_0
#define I2S_PORT_MIC I2S_PORT
#define I2S_PORT_SPK I2S_PORT
#endif

#define AUDIO_MODULE_RX_BUFFER 128
#define AUDIO_MODULE_MODE meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_1600
#define AUDIO_DEFAULT_GAIN 8 // Default gain multiplier when config value is 0

// Shared ring buffer for TX mic data and RX decoded audio.
// Since audio is half-duplex (PTT), only one direction is active at a time,
// so we share one large buffer for both paths.
#define RING_BUF_SAMPLES 16384 // 2.0s at 8kHz — power-of-2 for fast bitmask modulo
#define RING_BUF_MASK    (RING_BUF_SAMPLES - 1)
#define RX_JITTER_SAMPLES 9600   // 1.2s pre-fill ≈ 1 full packet of decoded audio
#define RX_SILENCE_TIMEOUT 2000  // ms of silence before stopping RX playback

// Queue item for received encoded audio packets
struct AudioRxPacket {
    uint8_t data[meshtastic_Constants_DATA_PAYLOAD_LEN];
    uint16_t size;
};

class AudioModule : public SinglePortModule, public Observable<const UIFrameEvent *>, private concurrency::OSThread
{
  public:
    unsigned char rx_encode_frame[meshtastic_Constants_DATA_PAYLOAD_LEN] = {};
    unsigned char tx_encode_frame[meshtastic_Constants_DATA_PAYLOAD_LEN] = {};
    c2_header tx_header = {};
    int16_t speech[ADC_BUFFER_SIZE_MAX] = {};       // scratch buffer for codec2 encode
    int16_t output_buffer[ADC_BUFFER_SIZE_MAX] = {}; // scratch buffer for codec2 decode
    uint16_t adc_buffer[ADC_BUFFER_SIZE_MAX] = {};   // partial I2S read accumulator
    int adc_buffer_size = 0;
    uint16_t adc_buffer_index = 0;
    int tx_encode_frame_index = AUDIO_HEADER_SIZE; // leave room for header + seq byte
    int rx_encode_frame_index = 0;
    int encode_codec_size = 0;
    int encode_frame_size = 0;
    int encode_frame_num = 0;
    uint8_t tx_seq = 0;           // TX packet sequence number (wraps at 255)
    uint8_t rx_seq_expected = 0;  // next expected RX sequence number
    bool rx_seq_initialized = false; // first packet resets expected seq
    volatile RadioState radio_state = RadioState::rx;

    struct CODEC2 *codec2 = NULL;

    // Shared ring buffer for TX mic capture and RX decoded playback.
    // Half-duplex: only one direction active at a time, so one buffer suffices.
    int16_t ring_buf[RING_BUF_SAMPLES] = {};
    volatile uint32_t ring_head = 0;
    volatile uint32_t ring_tail = 0;
    volatile bool rx_playback_active = false;
    volatile bool rx_draining = false;
    volatile bool i2s_reclaimed_for_codec2 = false; // true once we've stopped audioThread & reinstalled speaker I2S
    volatile uint32_t ring_drops = 0;  // count of samples dropped due to ring buffer overflow
    QueueHandle_t rxPacketQueue = nullptr;

    uint32_t ringAvailable();
    void ringWrite(const int16_t *data, int count);
    int ringRead(int16_t *data, int maxCount);
    void ringReset();

    AudioModule();

    bool shouldDraw();

    /**
     * Send our payload into the mesh
     */
    void sendPayload(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

#ifdef AUDIO_I2S_DUAL
    /// Reinstall the speaker I2S driver (needed after AudioThread::stop() uninstalls it)
    void reinstallSpeakerI2S();
#endif

  protected:
    bool firstTime = true;

    virtual int32_t runOnce() override;

    virtual meshtastic_MeshPacket *allocReply() override;

    virtual bool wantUIFrame() override { return true; }
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

#if HAS_SCREEN
  private:
    volatile uint32_t lastDrawMs = 0;
    uint8_t pendingMenu = 0;       // 0=none, 1=mic gain, 2=spk gain, 3=target
    bool suppressNextSelect = false;

    int handleInputEvent(const InputEvent *event);
    void showAudioMenu();
    void showGainMenu(bool isMic);
    void showTargetMenu();

    CallbackObserver<AudioModule, const InputEvent *> inputObserver =
        CallbackObserver<AudioModule, const InputEvent *>(this, &AudioModule::handleInputEvent);
#endif
};

extern AudioModule *audioModule;

#endif