#include "configuration.h"
#include "AudioModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "FSCommon.h"

#include <assert.h>

/*
    AudioModule
        A interface to send raw codec2 audio data over the mesh network. Based on the example code from the ESP32_codec2 project.
        https://github.com/deulis/ESP32_Codec2

        Codec 2 is a low-bitrate speech audio codec (speech coding) 
        that is patent free and open source develop by David Grant Rowe.
        http://www.rowetel.com/ and https://github.com/drowe67/codec2

    Basic Usage:
        1) Enable the module by setting audio.codec2_enabled to 1.
        2) Set the pins (audio.mic_pin / audio.amp_pin) for your preferred microphone and amplifier GPIO pins.
           On tbeam, recommend to use:
                audio.mic_chan 6 (GPIO 34)
                audio.amp_pin 14
                audio.ptt_pin 39 
        3) Set audio.timeout to the amount of time to wait before we consider
           your voice stream as "done".
        4) Set audio.bitrate to the desired codec2 rate (CODEC2_3200, CODEC2_2400, CODEC2_1600, CODEC2_1400, CODEC2_1300, CODEC2_1200, CODEC2_700, CODEC2_700B)

    KNOWN PROBLEMS
        * Until the module is initilized by the startup sequence, the amp_pin pin is in a floating
          state. This may produce a bit of "noise".
        * Will not work on NRF and the Linux device targets.
*/

#define AMIC 6
#define AAMP 14
#define PTT_PIN 39

#define AUDIO_MODULE_RX_BUFFER 128
#define AUDIO_MODULE_DATA_MAX Constants_DATA_PAYLOAD_LEN
#define AUDIO_MODULE_MODE 7 // 700B
#define AUDIO_MODULE_ACK 1

#if defined(ARCH_ESP32) && defined(USE_SX1280)

AudioModule *audioModule;

ButterworthFilter hp_filter(240, 8000, ButterworthFilter::ButterworthFilter::Highpass, 1);

//int16_t 1KHz sine test tone
int16_t Sine1KHz[8] = { -21210 , -30000, -21210, 0 , 21210 , 30000 , 21210, 0 };
int Sine1KHz_index = 0;

uint8_t rx_raw_audio_value = 127;

AudioModule::AudioModule() : SinglePortModule("AudioModule", PortNum_AUDIO_APP), concurrency::OSThread("AudioModule") {
    audio_fifo.init();
}

void AudioModule::run_codec2()
{
    if (state == State::tx)
    {
        for (int i = 0; i < ADC_BUFFER_SIZE; i++)
            speech[i] = (int16_t)hp_filter.Update((float)speech[i]);

        codec2_encode(codec2_state, tx_encode_frame + tx_encode_frame_index, speech);	

        //increment the pointer where the encoded frame must be saved
        tx_encode_frame_index += 8; 

        //If it is the 5th time then we have a ready trasnmission frame
        if (tx_encode_frame_index == ENCODE_FRAME_SIZE)
        {
            tx_encode_frame_index = 0;
            //Transmit it
            sendPayload();
        }
    }
    if (state == State::rx) //Receiving
    {
        //Make a cycle to get each codec2 frame from the received frame
        for (int i = 0; i < ENCODE_FRAME_SIZE; i += 8)
        {
            //Decode the codec2 frame
            codec2_decode(codec2_state, output_buffer, rx_encode_frame + i);
            
            // Add to the audio buffer the 320 samples resulting of the decode of the codec2 frame.
            for (int g = 0; g < ADC_BUFFER_SIZE; g++)
                audio_fifo.put(output_buffer[g]);
        }
    }
    state = State::standby;
}

void AudioModule::handleInterrupt()
{
    audioModule->onTimer();
}

void AudioModule::onTimer()
{
    if (state == State::tx) {
        adc_buffer[adc_buffer_index++] = (16 * adc1_get_raw(mic_chan)) - 32768;

        //If you want to test with a 1KHz tone, comment the line above and descomment the three lines below

        // adc_buffer[adc_buffer_index++] = Sine1KHz[Sine1KHz_index++];
        // if (Sine1KHz_index >= 8)
        // Sine1KHz_index = 0;

        if (adc_buffer_index == ADC_BUFFER_SIZE) {
            adc_buffer_index = 0;
            memcpy((void*)speech, (void*)adc_buffer, 2 * ADC_BUFFER_SIZE);
            audioModule->setIntervalFromNow(0); // process buffer immediately
        }
    } else if (state == State::rx)	{

        int16_t v;

        //Get a value from audio_fifo and convert it to 0 - 255 to play it in the ADC
        //If none value is available the DAC will play the last one that was read, that's
        //why the rx_raw_audio_value variable is a global one.
        if (audio_fifo.get(&v))
            rx_raw_audio_value = (uint8_t)((v + 32768) / 256);

        //Play
        dacWrite(moduleConfig.audio.amp_pin ? moduleConfig.audio.amp_pin : AAMP, rx_raw_audio_value);
    }
}

int32_t AudioModule::runOnce()
{
    if (moduleConfig.audio.codec2_enabled) {

        if (firstTime) {

            DEBUG_MSG("Initializing ADC on Channel %u\n", moduleConfig.audio.mic_chan ? moduleConfig.audio.mic_chan : AMIC);

            mic_chan = moduleConfig.audio.mic_chan ? (adc1_channel_t)(int)moduleConfig.audio.mic_chan : (adc1_channel_t)AMIC;
            adc1_config_width(ADC_WIDTH_12Bit);
	        adc1_config_channel_atten(mic_chan, ADC_ATTEN_DB_6);

            // Start a timer at 8kHz to sample the ADC and play the audio on the DAC.
            uint32_t cpufreq = getCpuFrequencyMhz();
	        switch (cpufreq){
                case 160:
                    adcTimer = timerBegin(3, 1000, true); // 160 MHz / 1000 = 160KHz
                    break;
                case 240:
                    adcTimer = timerBegin(3, 1500, true); // 240 MHz / 1500 = 160KHz
                    break;
                case 320:
                    adcTimer = timerBegin(3, 2000, true); // 320 MHz / 2000 = 160KHz
                    break;
                case 80:
                default:
                    adcTimer = timerBegin(3, 500, true); // 80 MHz / 500 = 160KHz
                    break;
            }
            timerAttachInterrupt(adcTimer, &AudioModule::handleInterrupt, true);
	        timerAlarmWrite(adcTimer, 20, true); // Interrupts when counter == 20, 8.000 times a second
	        timerAlarmEnable(adcTimer);

            DEBUG_MSG("Initializing DAC on Pin %u\n", moduleConfig.audio.amp_pin ? moduleConfig.audio.amp_pin : AAMP);
            DEBUG_MSG("Initializing PTT on Pin %u\n", moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN);

            // Configure PTT input
	        pinMode(moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN, INPUT_PULLUP);

            state = State::rx;

            DEBUG_MSG("Setting up codec2 in mode %u\n", moduleConfig.audio.bitrate ? moduleConfig.audio.bitrate : AUDIO_MODULE_MODE);

            codec2_state = codec2_create(moduleConfig.audio.bitrate ? moduleConfig.audio.bitrate : AUDIO_MODULE_MODE);
            codec2_set_lpc_post_filter(codec2_state, 1, 0, 0.8, 0.2);
            
            firstTime = 0;
        } else {
            // Check if we have a PTT press
            if (digitalRead(moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN) == LOW) {
                // PTT pressed, recording
                state = State::tx;
            }
            if (state != State::standby) {
                run_codec2();
            }
        }

        return 100;
    } else {
        DEBUG_MSG("Audio Module Disabled\n");

        return INT32_MAX;
    }
}

MeshPacket *AudioModule::allocReply()
{

    auto reply = allocDataPacket(); // Allocate a packet for sending

    return reply;
}

void AudioModule::sendPayload(NodeNum dest, bool wantReplies)
{
    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;

    p->want_ack = AUDIO_MODULE_ACK;

    p->decoded.payload.size =  ENCODE_FRAME_SIZE;
    memcpy(p->decoded.payload.bytes, tx_encode_frame, p->decoded.payload.size);

    service.sendToMesh(p);
}

ProcessMessage AudioModule::handleReceived(const MeshPacket &mp)
{
    if (moduleConfig.audio.codec2_enabled) {
        auto &p = mp.decoded;
        if (getFrom(&mp) != nodeDB.getNodeNum()) {
            if (p.payload.size == ENCODE_FRAME_SIZE) {
                memcpy(rx_encode_frame, p.payload.bytes, p.payload.size);
                state = State::rx;
                audioModule->setIntervalFromNow(0);
                run_codec2();
            } else {
                DEBUG_MSG("Invalid payload size %u != %u\n", p.payload.size, ENCODE_FRAME_SIZE);
            }
        }
    }

    return ProcessMessage::CONTINUE;
}

#endif