#pragma once

#include "SinglePortModule.h"
#include "concurrency/Lock.h"
#include "configuration.h"
#if defined(HAS_AUDIO_CODEC2) && !MESHTASTIC_EXCLUDE_AUDIO

// Codec2 is float throughout; software float runs ~3.5x slower than real time.
#if !defined(__FPU_PRESENT) && !defined(__XTENSA__) && !defined(ESP_PLATFORM)
#error "HAS_AUDIO_CODEC2 requires a hardware FPU"
#endif

#include "AudioIO.h"
#include "AudioModeSelect.h"
#include "AudioStream.h"
#include "ButterworthFilter.h"
#include "NodeDB.h"
#include <Arduino.h>
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>
#include <codec2.h>

enum RadioState { standby, rx, tx };

#ifndef PTT_PIN
#define PTT_PIN 39
#endif

// Time-out timer, as commercial and amateur PTT radios have. 60 s of speech
// costs ~15 s of airtime at the 25% occupancy ceiling.
#ifndef AUDIO_MAX_PTT_MS
#define AUDIO_MAX_PTT_MS 60000
#endif
#define AUDIO_PTT_WARN_MS 10000

// Hold this long to transmit; a shorter press cycles the destination. Matches
// PTT discipline of keying up before speaking, so nothing is clipped.
#define AUDIO_PTT_ARM_MS 300

class AudioModule : public SinglePortModule, public Observable<const UIFrameEvent *>, private concurrency::OSThread
{
  public:
    AudioModule();

    bool shouldDraw();

  protected:
    virtual int32_t runOnce() override;
    virtual meshtastic_MeshPacket *allocReply() override;
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    virtual bool wantUIFrame() override { return this->shouldDraw(); }
    virtual Observable<const UIFrameEvent *> *getUIFrameObservable() override { return this; }
#if !HAS_SCREEN
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#else
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
#endif

  private:
    bool onPttDown();
    void onPttUp();
    void captureAndEncode();
    void sendPacket(bool last);
    void servicePlayout();
    void cycleDestination();
    const char *destinationLabel();
    const char *talkerLabel();
    static void emitDecoded(void *ctx, const uint8_t *frames, size_t len, uint8_t mode, bool conceal);
    void decodeAndPlay(const uint8_t *frames, size_t len, uint8_t mode);

    struct CODEC2 *codec2 = nullptr;
    const AudioModeInfo *mode = nullptr;

    uint8_t txPacket[meshtastic_Constants_DATA_PAYLOAD_LEN] = {};
    uint16_t txLen = AUDIO_HEADER_LEN;
    uint8_t txSession = 0;
    uint16_t txSeq = 0;

    int16_t speech[AUDIO_FRAME_SAMPLES_MAX] = {};
    int16_t playBuf[AUDIO_FRAME_SAMPLES_MAX] = {};
    int16_t micBuf[AUDIO_FRAME_SAMPLES_MAX] = {};
    int micIndex = 0;

    int frameSamples = 0;
    int bytesPerFrame = 0;
    int framesPerPacket = 0;

    // handleReceived runs on the Router thread, runOnce on ours.
    AudioStreamAssembler stream;
    concurrency::Lock streamLock;

    RadioState radio_state = RadioState::rx;
    bool firstTime = true;
    bool pttLatched = false; // held down but refused, ignore until released
    bool pttHeld = false;
    uint32_t pttDownMs = 0;
    uint32_t nextPlayoutMs = 0;
    uint32_t pttStartMs = 0;

    // Transmit destination. NODENUM_BROADCAST plus a channel index is a channel;
    // any other node number is a DM. Receive is unaffected by this.
    NodeNum txDest = NODENUM_BROADCAST;
    uint8_t txChannel = 0;
    char label[24] = {};
};

extern AudioModule *audioModule;

#endif
