#pragma once
#include "DebugConfiguration.h"
#include "PowerFSM.h"
#include "buzz/buzz.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include "sleep.h"
#include <algorithm>
#include <cstring>
#include <deque>
#include <memory>

#ifdef HAS_I2S
#include <AudioFileSourcePROGMEM.h>
#include <AudioGeneratorRTTTL.h>
#include <AudioOutputI2S.h>
#include <ESP8266SAM.h>

// A board with an I2S amplifier opts in by defining AUDIO_AMP_ENABLE(on) in its variant.h to power the
// amp on/off around playback (e.g. an enable pin on an I/O expander). The includes below expose the
// expander instances (io / mcpIoExpander) those macros typically reference.
#ifdef USE_XL9555
#include "ExtensionIOXL9555.hpp"
extern ExtensionIOXL9555 io;
#endif

#ifdef USE_MCP23017
#include "platform/esp32/ExtensionIOMCP23017.h"
#endif

#define AUDIO_THREAD_INTERVAL_MS 100

class AudioThread : public concurrency::OSThread
{
  public:
    AudioThread() : OSThread("Audio") { initOutput(); }

    void beginRttl(const void *data, uint32_t len)
    {
#ifdef AUDIO_AMP_ENABLE
        AUDIO_AMP_ENABLE(true);
#endif
        setCPUFast(true);
        rtttlFile = std::unique_ptr<AudioFileSourcePROGMEM>(new AudioFileSourcePROGMEM(data, len));
        i2sRtttl = std::unique_ptr<AudioGeneratorRTTTL>(new AudioGeneratorRTTTL());
        i2sRtttl->begin(rtttlFile.get(), audioOut.get());
    }

    // Also handles actually playing the RTTTL, needs to be called in loop
    bool isPlaying()
    {
        if (i2sRtttl != nullptr) {
            return i2sRtttl->isRunning() && i2sRtttl->loop();
        }
        return false;
    }

    void stop()
    {
        if (i2sRtttl != nullptr) {
            i2sRtttl->stop();
            i2sRtttl = nullptr;
        } else if (audioOut != nullptr) {
            audioOut->stop();
        }

        rtttlFile = nullptr;

        setCPUFast(false);
#ifdef AUDIO_AMP_ENABLE
        AUDIO_AMP_ENABLE(false);
#endif
    }

    void readAloud(const char *text)
    {
        if (i2sRtttl != nullptr) {
            i2sRtttl->stop();
            i2sRtttl = nullptr;
        }

#ifdef AUDIO_AMP_ENABLE
        AUDIO_AMP_ENABLE(true);
#endif
        auto sam = std::unique_ptr<ESP8266SAM>(new ESP8266SAM);
        sam->Say(audioOut.get(), text);
        setCPUFast(false);
#ifdef AUDIO_AMP_ENABLE
        AUDIO_AMP_ENABLE(false);
#endif
    }

#if defined(NM_EPD_420_BW)
    bool enqueueNmEpd420Tone(NmEpd420Tone tone)
    {
        if (!moduleConfig.audio.codec2_enabled)
            return false;

        if (tone == NmEpd420Tone::Receive &&
            (nmEpd420TonePlaying == NmEpd420Tone::Receive ||
             std::find(nmEpd420ToneQueue.begin(), nmEpd420ToneQueue.end(), NmEpd420Tone::Receive) !=
                 nmEpd420ToneQueue.end())) {
            return false;
        }

        const auto insertion = std::find_if(nmEpd420ToneQueue.begin(), nmEpd420ToneQueue.end(),
                                            [tone](NmEpd420Tone queued) { return tonePriority(tone) > tonePriority(queued); });
        if (nmEpd420ToneQueue.size() >= NM_EPD_420_TONE_QUEUE_SIZE) {
            if (insertion == nmEpd420ToneQueue.end())
                return false;
            nmEpd420ToneQueue.pop_back();
        }
        nmEpd420ToneQueue.insert(insertion, tone);
        return true;
    }
#endif

#if defined(NM_EPD_420_AUDIO_TEST)
    void startNmEpd420AudioHardwareTest()
    {
        nmEpd420AudioTestState = NmEpd420AudioTestState::Rttl;
        LOG_INFO("NM-EPD-420 audio test: MCLK=%d BCLK=%d WS=%d DOUT=%d", DAC_I2S_MCLK, DAC_I2S_BCK, DAC_I2S_WS,
                 DAC_I2S_DOUT);
        LOG_INFO("NM-EPD-420 audio test: starting RTTTL");
    }
#endif

  protected:
    int32_t runOnce() override
    {
        canSleep = true; // Assume we should not keep the board awake

#if defined(NM_EPD_420_BW)
        if (i2sRtttl != nullptr) {
            if (isPlaying())
                return 1;

            stop();
            nmEpd420TonePlayingValid = false;
        }
        if (!nmEpd420ToneQueue.empty()) {
            playNextNmEpd420Tone();
            return 1;
        }
#endif

#if defined(NM_EPD_420_AUDIO_TEST)
        if (nmEpd420AudioTestState == NmEpd420AudioTestState::Rttl) {
            beginRttl(nm_epd_420_rtttl, sizeof(nm_epd_420_rtttl) - 1);
            nmEpd420AudioTestState = NmEpd420AudioTestState::RttlPlaying;
            return 1;
        }

        if (nmEpd420AudioTestState == NmEpd420AudioTestState::RttlPlaying) {
            if (isPlaying())
                return 1;

            stop();
            LOG_INFO("NM-EPD-420 audio test: RTTTL complete");
            nmEpd420AudioTestState = NmEpd420AudioTestState::Pcm;
            return 100;
        }

        if (nmEpd420AudioTestState == NmEpd420AudioTestState::Pcm) {
            beginNmEpd420OneKhzTone();
            nmEpd420AudioTestState = NmEpd420AudioTestState::PcmPlaying;
            return 1;
        }

        if (nmEpd420AudioTestState == NmEpd420AudioTestState::PcmPlaying) {
            if (writeNmEpd420OneKhzTone())
                return 1;

            stop();
            LOG_INFO("NM-EPD-420 audio test: 1 kHz PCM complete");
            nmEpd420AudioTestState = NmEpd420AudioTestState::Complete;
        }
#endif
        return AUDIO_THREAD_INTERVAL_MS;
    }

  private:
    void initOutput()
    {
        audioOut = std::unique_ptr<AudioOutputI2S>(new AudioOutputI2S(1, AudioOutputI2S::EXTERNAL_I2S));
        audioOut->SetPinout(DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT, DAC_I2S_MCLK);
        audioOut->SetGain(0.2);
    };
#if defined(NM_EPD_420_BW)
    static constexpr size_t NM_EPD_420_TONE_QUEUE_SIZE = 4;

    static uint8_t tonePriority(NmEpd420Tone tone)
    {
        switch (tone) {
        case NmEpd420Tone::DeliveryFailure:
            return 5;
        case NmEpd420Tone::LowBattery:
            return 4;
        case NmEpd420Tone::DeliverySuccess:
            return 3;
        case NmEpd420Tone::Receive:
            return 2;
        case NmEpd420Tone::Boot:
        case NmEpd420Tone::Shutdown:
            return 1;
        }
        return 0;
    }

    static const char *rtttlForNmEpd420Tone(NmEpd420Tone tone)
    {
        switch (tone) {
        case NmEpd420Tone::Boot:
            return "boot:d=32,o=5,b=220:c,e,g";
        case NmEpd420Tone::Shutdown:
            return "shutdown:d=32,o=5,b=200:g,e,c";
        case NmEpd420Tone::LowBattery:
            return "lowbat:d=8,o=4,b=160:c,p,c";
        case NmEpd420Tone::Receive:
            return "receive:d=16,o=5,b=230:g,e";
        case NmEpd420Tone::DeliverySuccess:
            return "success:d=32,o=5,b=240:c,e,g";
        case NmEpd420Tone::DeliveryFailure:
            return "failure:d=8,o=4,b=180:g,c";
        }
        return "";
    }

    void playNextNmEpd420Tone()
    {
        nmEpd420TonePlaying = nmEpd420ToneQueue.front();
        nmEpd420ToneQueue.pop_front();
        nmEpd420TonePlayingValid = true;
        const char *rtttl = rtttlForNmEpd420Tone(nmEpd420TonePlaying);
        LOG_INFO("NM-EPD-420 audio: play tone %u", static_cast<unsigned>(nmEpd420TonePlaying));
        beginRttl(rtttl, strlen(rtttl));
    }

    std::deque<NmEpd420Tone> nmEpd420ToneQueue;
    NmEpd420Tone nmEpd420TonePlaying = NmEpd420Tone::Boot;
    bool nmEpd420TonePlayingValid = false;
#endif

    std::unique_ptr<AudioGeneratorRTTTL> i2sRtttl = nullptr;
    std::unique_ptr<AudioOutputI2S> audioOut = nullptr;

    std::unique_ptr<AudioFileSourcePROGMEM> rtttlFile = nullptr;
};

#endif
