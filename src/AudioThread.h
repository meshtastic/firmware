#pragma once
#include "PowerFSM.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include "sleep.h"

#ifdef HAS_I2S
#include <AudioOutputI2S.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioGeneratorRTTTL.h>
#include <ESP8266SAM.h>

// const char example[] PROGMEM = "smb:d=4,o=5,b=100:16e6,16e6,32p,8e6,16c6,8e6,8g6,8p,8g,8p,8c6,16p,8g,16p,8e,16p,8a,8b,16a#,8a,16g.,16e6,16g6,8a6,16f6,8g6,8e6,16c6,16d6,8b,16p,8c6,16p,8g,16p,8e,16p,8a,8b,16a#,8a,16g.,16e6,16g6,8a6,16f6,8g6,8e6,16c6,16d6,8b,8p,16g6,16f#6,16f6,16d#6,16p,16e6,16p,16g#,16a,16c6,16p,16a,16c6,16d6,8p,16g6,16f#6,16f6,16d#6,16p,16e6,16p,16c7,16p,16c7,16c7,p,16g6,16f#6,16f6,16d#6,16p,16e6,16p,16g#,16a,16c6,16p,16a,16c6,16d6,8p,16d#6,8p,16d6,8p,16c6";

#define AUDIO_THREAD_INTERVAL_MS 25

class AudioThread : public concurrency::OSThread
{
public:
    AudioThread() : OSThread("AudioThread")
    {
        initOutput();
    }

    void beginRttl(const void *data, uint32_t len) 
    {
        setCPUFast(true);
        rtttlFile = new AudioFileSourcePROGMEM(data, len);
        i2sRtttl = new AudioGeneratorRTTTL();
        i2sRtttl->begin(rtttlFile, audioOut);
    }

    bool isPlaying() 
    {
        return i2sRtttl->isRunning() && i2sRtttl->loop();
    }

    void stop() 
    {
        i2sRtttl->stop();
    }

/*
        if (i2sRtttl->isRunning() && !i2sRtttl->loop()) {
            i2sRtttl->stop();
        }
*/
    
protected:
    int32_t runOnce() override
    {
        canSleep = true; // Assume we should not keep the board awake
        return AUDIO_THREAD_INTERVAL_MS;
    }

private:
    void initOutput() 
    {
        audioOut = new AudioOutputI2S(1, AudioOutputI2S::EXTERNAL_I2S);
        audioOut->SetPinout(DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT);
        audioOut->SetGain(0.3);
    };

    AudioGeneratorRTTTL *i2sRtttl;
    AudioOutputI2S *audioOut;

    AudioFileSourcePROGMEM *rtttlFile;
};

#endif
