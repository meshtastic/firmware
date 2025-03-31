#pragma once
#include "PowerFSM.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include "sleep.h"

#ifdef HAS_I2S
#include <AudioFileSourcePROGMEM.h>
#include <AudioGeneratorRTTTL.h>
#include <AudioOutputI2S.h>
#include <ESP8266SAM.h>

#define AUDIO_THREAD_INTERVAL_MS 100

class AudioThread : public concurrency::OSThread
{
  public:
    AudioThread() : OSThread("Audio") { initOutput(); }

    void beginRttl(const void *data, uint32_t len)
    {
        setCPUFast(true);
        rtttlFile = new AudioFileSourcePROGMEM(data, len);
        i2sRtttl = new AudioGeneratorRTTTL();
        i2sRtttl->begin(rtttlFile, audioOut);
    }

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
            delete i2sRtttl;
            i2sRtttl = nullptr;
        }
        delete rtttlFile;
        rtttlFile = nullptr;

        setCPUFast(false);
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
    void initOutput()
    {
        audioOut = new AudioOutputI2S(1, AudioOutputI2S::EXTERNAL_I2S);
        audioOut->SetPinout(DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT, DAC_I2S_MCLK);
        audioOut->SetGain(0.2);
    };

    AudioGeneratorRTTTL *i2sRtttl = nullptr;
    AudioOutputI2S *audioOut;

    AudioFileSourcePROGMEM *rtttlFile;
};

#endif
