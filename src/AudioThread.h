#pragma once
#include "PowerFSM.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include "platform/DevicePowerController.h"
#include "platform/DeviceVariant.h"
#include "sleep.h"
#include <memory>

#ifdef HAS_I2S
#include <AudioFileSourcePROGMEM.h>
#include <AudioGeneratorRTTTL.h>
#include <AudioOutputI2S.h>
#include <ESP8266SAM.h>

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
        if (i2sRtttl != nullptr && getDevicePowerController())
            stop();
        setAudioRoute(false);
        setAmplifier(true);
        setCPUFast(true);
        rtttlFile = std::unique_ptr<AudioFileSourcePROGMEM>(new AudioFileSourcePROGMEM(data, len));
        i2sRtttl = std::unique_ptr<AudioGeneratorRTTTL>(new AudioGeneratorRTTTL());
        i2sRtttl->begin(rtttlFile.get(), audioOut.get());
    }

    // Also handles actually playing the RTTTL, needs to be called in loop
    bool isPlaying()
    {
        if (i2sRtttl != nullptr) {
            const bool playing = i2sRtttl->isRunning() && i2sRtttl->loop();
            if (!playing && getDevicePowerController())
                stop();
            return playing;
        }
        return false;
    }

    void stop()
    {
        if (i2sRtttl != nullptr) {
            i2sRtttl->stop();
            i2sRtttl = nullptr;
        }

        rtttlFile = nullptr;

        setCPUFast(false);
        setAmplifier(false);
        setAudioRoute(false);
    }

    void readAloud(const char *text)
    {
        if (i2sRtttl != nullptr) {
            if (getDevicePowerController())
                stop();
            else {
                i2sRtttl->stop();
                i2sRtttl = nullptr;
            }
        }

        setAmplifier(true);
        setAudioRoute(false);
        auto sam = std::unique_ptr<ESP8266SAM>(new ESP8266SAM);
        sam->Say(audioOut.get(), text);
        setCPUFast(false);
        audioOut->stop();
        setAmplifier(false);
        setAudioRoute(false);
    }

  protected:
    int32_t runOnce() override
    {
        canSleep = true; // Assume we should not keep the board awake

        // if (i2sRtttl != nullptr && i2sRtttl->isRunning()) {
        //     i2sRtttl->loop();
        // }
        return AUDIO_THREAD_INTERVAL_MS;
    }

  private:
    void setAudioRoute(bool a7682e)
    {
        if (auto *controller = getDevicePowerController())
            controller->setAudioRoute(a7682e);
    }

    void setAmplifier(bool enabled)
    {
        if (auto *controller = getDevicePowerController()) {
            controller->setAmplifier(enabled);
            return;
        }
#ifdef AUDIO_AMP_ENABLE
        AUDIO_AMP_ENABLE(enabled);
#else
        (void)enabled;
#endif
    }

    void initOutput()
    {
        audioOut = std::unique_ptr<AudioOutputI2S>(new AudioOutputI2S(1, AudioOutputI2S::EXTERNAL_I2S));
        audioOut->SetPinout(DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT, DAC_I2S_MCLK);
        audioOut->SetGain(0.2);
    };

    std::unique_ptr<AudioGeneratorRTTTL> i2sRtttl = nullptr;
    std::unique_ptr<AudioOutputI2S> audioOut = nullptr;

    std::unique_ptr<AudioFileSourcePROGMEM> rtttlFile = nullptr;
};

#endif
