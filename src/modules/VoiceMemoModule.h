#pragma once

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "input/InputBroker.h"
#include "mesh/generated/meshtastic/module_config.pb.h"

#if defined(ARCH_ESP32) && defined(HAS_I2S) && !MESHTASTIC_EXCLUDE_VOICEMEMO

#include <Arduino.h>
#include <ButterworthFilter.h>
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>
#include <codec2.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * VoiceMemoModule - Store and forward short codec2 encoded audio messages
 *
 * Unlike the existing AudioModule which is designed for real-time push-to-talk,
 * this module is designed for short voice memos that are:
 * - Recorded when the user holds Shift+Space
 * - Encoded with Codec2 for compression
 * - Sent over the mesh with hop_limit=0 (local only)
 * - Stored on receiving devices for later playback
 * - Played back when user long-presses on the notification
 */

// Voice memo states
enum class VoiceMemoState { IDLE, RECORDING, SENDING, RECEIVING, PLAYING };

// Codec2 magic header for voice memos
const char VOICEMEMO_MAGIC[4] = {0xc0, 0xde, 0xc2, 0x4d}; // c0dec2M (M for Memo)

struct VoiceMemoHeader {
    char magic[4];
    uint8_t mode;       // Codec2 mode
    uint8_t sequence;   // Packet sequence number (for multi-packet memos)
    uint8_t totalParts; // Total number of packets in this memo (0 = unknown/streaming)
    uint8_t memoId;     // Unique ID for this recording session (to identify related packets)
};

// Maximum recording time in seconds
#define VOICEMEMO_MAX_RECORD_SECS 10
#define VOICEMEMO_ADC_BUFFER_SIZE 320       // Codec2 samples per frame
#define VOICEMEMO_UPSAMPLE_BUFFER_SIZE 3600 // 320 * (44100/8000) * 2 (stereo) ≈ 3528, rounded up
#define VOICEMEMO_I2S_PORT I2S_NUM_0
// Codec2 mode - use protobuf enum minus 1 to get codec2 library mode
#define VOICEMEMO_CODEC2_MODE (meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_700 - 1)

// Storage for received voice memos
#define VOICEMEMO_MAX_STORED 5
struct StoredVoiceMemo {
    NodeNum from;
    uint32_t timestamp;
    uint8_t data[meshtastic_Constants_DATA_PAYLOAD_LEN * 4]; // Allow up to 4 packets
    size_t dataLen;
    uint8_t codec2Mode;
    uint8_t memoId;        // Memo ID from sender (to identify related packets)
    uint8_t receivedParts; // Bitmask of received packet sequences
    uint8_t expectedParts; // Total expected parts (0 = unknown)
    bool played;
};

class VoiceMemoModule : public SinglePortModule, public Observable<const UIFrameEvent *>, private concurrency::OSThread
{
  public:
    VoiceMemoModule();

    /**
     * Check if we should draw the UI frame
     */
    bool shouldDraw();

    /**
     * Handle keyboard input for Shift+Space detection
     */
    int handleInputEvent(const InputEvent *event);

    /**
     * Play a stored voice memo
     */
    void playStoredMemo(int index);

    /**
     * Get number of unplayed memos
     */
    int getUnplayedCount();

    /**
     * Get stored memo info for UI
     */
    const StoredVoiceMemo *getStoredMemo(int index);

  protected:
    virtual int32_t runOnce() override;
    virtual meshtastic_MeshPacket *allocReply() override;
    virtual bool wantUIFrame() override { return shouldDraw(); }
    virtual Observable<const UIFrameEvent *> *getUIFrameObservable() override { return this; }

#if HAS_SCREEN
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
#endif

    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  private:
    // State machine
    VoiceMemoState state = VoiceMemoState::IDLE;

    // Codec2
    CODEC2 *codec2 = nullptr;
    int encodeCodecSize = 0;
    int adcBufferSize = 0;

    // Audio buffers
    int16_t speechBuffer[VOICEMEMO_ADC_BUFFER_SIZE] = {};
    int16_t outputBuffer[VOICEMEMO_ADC_BUFFER_SIZE] = {};
    int16_t upsampleBuffer[VOICEMEMO_UPSAMPLE_BUFFER_SIZE] = {}; // For 8kHz->44.1kHz upsampling
    uint8_t encodedFrame[meshtastic_Constants_DATA_PAYLOAD_LEN] = {};
    size_t encodedFrameIndex = 0;

    // Recording state
    uint32_t recordingStartMs = 0;
    uint32_t sendingCompleteMs = 0; // When sending completed (for "Sent!" display timeout)
    uint8_t currentMemoId = 0;      // Unique ID for current recording session
    uint8_t currentSequence = 0;    // Current packet sequence number

    // I2S state
    bool i2sInitialized = false;

    // Stored memos for playback
    StoredVoiceMemo storedMemos[VOICEMEMO_MAX_STORED];
    int storedMemoCount = 0;

    // Playback state
    int playingMemoIndex = -1;
    size_t playbackPosition = 0;

    // Filter for audio cleanup
    ButterworthFilter *hpFilter = nullptr;

    // Codec2 task for encoding (needs large stack)
    TaskHandle_t codec2TaskHandle = nullptr;
    volatile bool codec2TaskRunning = false;
    volatile bool audioReady = false;

    // Playback task (also needs large stack for Codec2 decoding)
    TaskHandle_t playbackTaskHandle = nullptr;
    volatile bool playbackTaskRunning = false;
    volatile bool playbackReady = false;
    const StoredVoiceMemo *currentPlaybackMemo = nullptr;

    // Internal methods
    bool initES7210();
    bool initI2S();
    void deinitI2S();
    void startRecording();
    void stopRecording();
    void processRecordingBuffer();
    void sendEncodedPayload();
    void storeMemo(const meshtastic_MeshPacket &mp);
    void playMemo(const StoredVoiceMemo &memo);

  public:
    // Called by the codec2 task - needs to be public for task function access
    void doCodec2Encode();
    void doCodec2Playback();

    // Keyboard observer
    CallbackObserver<VoiceMemoModule, const InputEvent *> inputObserver =
        CallbackObserver<VoiceMemoModule, const InputEvent *>(this, &VoiceMemoModule::handleInputEvent);
};

extern VoiceMemoModule *voiceMemoModule;

#endif // ARCH_ESP32 && HAS_I2S && !MESHTASTIC_EXCLUDE_VOICEMEMO
