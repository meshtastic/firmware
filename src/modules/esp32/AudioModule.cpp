
#include "configuration.h"
#if defined(ARCH_ESP32)
#include "AudioModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "FSCommon.h"

#include <soc/sens_reg.h>
#include <soc/sens_struct.h>
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
          radio_state. This may produce a bit of "noise".
        * Will not work on NRF and the Linux device targets.
*/

#define AMIC 6
#define AAMP 14
#define PTT_PIN 39

#ifdef ARCH_ESP32
// ESP32 doesn't use that flag
#define YIELD_FROM_ISR(x) portYIELD_FROM_ISR()
#else
#define YIELD_FROM_ISR(x) portYIELD_FROM_ISR(x)
#endif

// #define I2S_WS 13
// #define I2S_SD 15
// #define I2S_SIN 2
// #define I2S_SCK 14

// Use I2S Processor 0
#define I2S_PORT I2S_NUM_0

#define AUDIO_MODULE_RX_BUFFER 128
#define AUDIO_MODULE_MODE ModuleConfig_AudioConfig_Audio_Baud_CODEC2_700

AudioModule *audioModule;
Codec2Thread *codec2Thread;

FastAudioFIFO audio_fifo;
uint16_t adc_buffer[ADC_BUFFER_SIZE] = {};
uint16_t adc_buffer_index = 0;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
int16_t speech[ADC_BUFFER_SIZE] = {};
volatile RadioState radio_state = RadioState::tx;
adc1_channel_t mic_chan = (adc1_channel_t)0;

ButterworthFilter hp_filter(240, 8000, ButterworthFilter::ButterworthFilter::Highpass, 1);

//int16_t 1KHz sine test tone
int16_t Sine1KHz[8] = { -21210 , -30000, -21210, 0 , 21210 , 30000 , 21210, 0 };
int Sine1KHz_index = 0;

uint8_t rx_raw_audio_value = 127;

int IRAM_ATTR local_adc1_read(int channel) {
    uint16_t adc_value;
#if CONFIG_IDF_TARGET_ESP32S3
    SENS.sar_meas1_ctrl2.sar1_en_pad = (1 << channel); // only one channel is selected
    while (SENS.sar_slave_addr1.meas_status != 0);
    SENS.sar_meas1_ctrl2.meas1_start_sar = 0;
    SENS.sar_meas1_ctrl2.meas1_start_sar = 1;
    while (SENS.sar_meas1_ctrl2.meas1_done_sar == 0);
    adc_value = SENS.sar_meas1_ctrl2.meas1_data_sar;
#else
    SENS.sar_meas_start1.sar1_en_pad = (1 << channel); // only one channel is selected
    while (SENS.sar_slave_addr1.meas_status != 0);
    SENS.sar_meas_start1.meas1_start_sar = 0;
    SENS.sar_meas_start1.meas1_start_sar = 1;
    while (SENS.sar_meas_start1.meas1_done_sar == 0);
    adc_value = SENS.sar_meas_start1.meas1_data_sar;
#endif    
    return adc_value;
}

IRAM_ATTR void am_onTimer()
{
    portENTER_CRITICAL_ISR(&timerMux); //Enter crital code without interruptions
    if ((radio_state == RadioState::tx) && (!moduleConfig.audio.i2s_sd)) {
        adc_buffer[adc_buffer_index++] = (16 * local_adc1_read(mic_chan)) - 32768;

        //If you want to test with a 1KHz tone, comment the line above and descomment the three lines below

        // adc_buffer[adc_buffer_index++] = Sine1KHz[Sine1KHz_index++];
        // if (Sine1KHz_index >= 8)
        // Sine1KHz_index = 0;

        if (adc_buffer_index == ADC_BUFFER_SIZE) {
            adc_buffer_index = 0;
            DEBUG_MSG("♪♫♪ memcpy\n");
            memcpy((void*)speech, (void*)adc_buffer, 2 * ADC_BUFFER_SIZE);
            // Notify codec2 task that the buffer is ready.
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            DEBUG_MSG("♪♫♪ notifyFromISR\n");
            codec2Thread->notifyFromISR(&xHigherPriorityTaskWoken, RadioState::tx, true);
            if (xHigherPriorityTaskWoken)
                portYIELD_FROM_ISR();
        }
    } else if ((radio_state == RadioState::rx) && (!moduleConfig.audio.i2s_din)) {
        // ESP32-S3 does not have DAC support
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
        int16_t v;

        //Get a value from audio_fifo and convert it to 0 - 255 to play it in the ADC
        if (audio_fifo.get(&v))
            rx_raw_audio_value = (uint8_t)((v + 32768) / 256);

        dacWrite(moduleConfig.audio.amp_pin ? moduleConfig.audio.amp_pin : AAMP, rx_raw_audio_value);
#endif
    }
    portEXIT_CRITICAL_ISR(&timerMux); // exit critical code
}

Codec2Thread::Codec2Thread() : concurrency::NotifiedWorkerThread("Codec2Thread") {
    if ((moduleConfig.audio.codec2_enabled) && (myRegion->audioPermitted)) {
        DEBUG_MSG("♪♫♪ Setting up codec2 in mode %u", (moduleConfig.audio.bitrate ? moduleConfig.audio.bitrate : AUDIO_MODULE_MODE) - 1);
        codec2_state = codec2_create((moduleConfig.audio.bitrate ? moduleConfig.audio.bitrate : AUDIO_MODULE_MODE) - 1);
        codec2_set_lpc_post_filter(codec2_state, 1, 0, 0.8, 0.2);
        encode_codec_size = (codec2_bits_per_frame(codec2_state) + 7) / 8;
        encode_frame_num = Constants_DATA_PAYLOAD_LEN / encode_codec_size;
        encode_frame_size = encode_frame_num * encode_codec_size; // max 237 bytes
        DEBUG_MSG(" using %d frames of %d bytes for a total payload length of %d bytes\n", encode_frame_num, encode_codec_size, encode_frame_size);
    } else {
        DEBUG_MSG("♪♫♪ Codec2 disabled (AudioModule %d, Region %s, permitted %d)\n", moduleConfig.audio.codec2_enabled, myRegion->name, myRegion->audioPermitted);
    }
}

AudioModule::AudioModule() : SinglePortModule("AudioModule", PortNum_AUDIO_APP), concurrency::OSThread("AudioModule") {
    audio_fifo.init();
    new Codec2Thread();
    //debug
    moduleConfig.audio.i2s_ws = 13;
    moduleConfig.audio.i2s_sd = 15;
    moduleConfig.audio.i2s_din = 2;
    moduleConfig.audio.i2s_sck =  14;
}

void IRAM_ATTR Codec2Thread::onNotify(uint32_t notification)
{
    switch (notification) {
        case RadioState::tx:
            for (int i = 0; i < ADC_BUFFER_SIZE; i++)
                speech[i] = (int16_t)hp_filter.Update((float)speech[i]);

            codec2_encode(codec2_state, audioModule->tx_encode_frame + tx_encode_frame_index, speech);    

            //increment the pointer where the encoded frame must be saved
            tx_encode_frame_index += encode_codec_size; 

            //If it this is reached we have a ready trasnmission frame
            if (tx_encode_frame_index == encode_frame_size)
            {
                tx_encode_frame_index = 0;
                //Transmit it
                audioModule->sendPayload();
            }
            break;
        case RadioState::rx:
            //Make a cycle to get each codec2 frame from the received frame
            for (int i = 0; i < encode_frame_size; i += encode_codec_size)
            {
                //Decode the codec2 frame
                codec2_decode(codec2_state, output_buffer, audioModule->rx_encode_frame + i);
                
                // Add to the audio buffer the 320 samples resulting of the decode of the codec2 frame.
                for (int g = 0; g < ADC_BUFFER_SIZE; g++)
                    audio_fifo.put(output_buffer[g]);
            }
            break;
        default:
            assert(0); // We expected to receive a valid notification from the ISR
            break;
    }
}

int32_t AudioModule::runOnce()
{
    if ((moduleConfig.audio.codec2_enabled) && (myRegion->audioPermitted)) {
        esp_err_t res;
        if (firstTime) {
            // if we have I2S_SD defined, take samples from digital mic. I2S_DIN means digital output to amp.
            if (moduleConfig.audio.i2s_sd || moduleConfig.audio.i2s_din) {
                // Set up I2S Processor configuration. This will produce 16bit samples at 8 kHz instead of 12 from the ADC
                DEBUG_MSG("♪♫♪ Initializing I2S SD: %d DIN: %d WS: %d SCK:%d\n", moduleConfig.audio.i2s_sd, moduleConfig.audio.i2s_din, moduleConfig.audio.i2s_ws, moduleConfig.audio.i2s_sck);
                i2s_config_t i2s_config = {
                    .mode = (i2s_mode_t)(I2S_MODE_MASTER | (moduleConfig.audio.i2s_sd ? I2S_MODE_RX : 0) | (moduleConfig.audio.i2s_din ? I2S_MODE_TX : 0)),
                    .sample_rate = 8000,
                    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
                    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
                    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
                    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
                    .dma_buf_count = 8,
                    .dma_buf_len = ADC_BUFFER_SIZE, // 320 * 2 bytes
                    .use_apll = false,
                    .tx_desc_auto_clear = true,
                    .fixed_mclk = 0
                };
                res = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
                if(res != ESP_OK)
                    DEBUG_MSG("♪♫♪ Failed to install I2S driver: %d\n", res);

                const i2s_pin_config_t pin_config = {
                    .bck_io_num = moduleConfig.audio.i2s_sck,
                    .ws_io_num = moduleConfig.audio.i2s_ws,
                    .data_out_num = moduleConfig.audio.i2s_din ? moduleConfig.audio.i2s_din : I2S_PIN_NO_CHANGE,
                    .data_in_num = moduleConfig.audio.i2s_sd ? moduleConfig.audio.i2s_sd : I2S_PIN_NO_CHANGE
                };
                res = i2s_set_pin(I2S_PORT, &pin_config);
                if(res != ESP_OK)
                    DEBUG_MSG("♪♫♪ Failed to set I2S pin config: %d\n", res);

                res = i2s_start(I2S_PORT);
                if(res != ESP_OK)
                    DEBUG_MSG("♪♫♪ Failed to start I2S: %d\n", res);
            }
            
            if (!moduleConfig.audio.i2s_sd) {
                // Set up ADC if we don't have a digital microphone.
                DEBUG_MSG("♪♫♪ Initializing ADC on Channel %u\n", moduleConfig.audio.mic_chan ? moduleConfig.audio.mic_chan : AMIC);
                mic_chan = moduleConfig.audio.mic_chan ? (adc1_channel_t)(int)moduleConfig.audio.mic_chan : (adc1_channel_t)AMIC;
                adc1_config_width(ADC_WIDTH_12Bit);
                adc1_config_channel_atten(mic_chan, ADC_ATTEN_DB_6);
                adc1_get_raw(mic_chan);
            }

            radio_state = RadioState::rx;

            if ((!moduleConfig.audio.i2s_sd) || (!moduleConfig.audio.i2s_din)) {
                // Start a timer at 8kHz to sample the ADC and play the audio on the DAC, but only if we have analogue audio to process
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
                DEBUG_MSG("♪♫♪ Timer CPU Frequency: %u MHz\n", cpufreq);
                timerAttachInterrupt(adcTimer, &am_onTimer, false);
                timerAlarmWrite(adcTimer, 20, true); // Interrupts when counter == 20, 8.000 times a second
                timerAlarmEnable(adcTimer);
            }

            // setup analogue DAC only if we don't use I2S for output. This is not available on ESP32-S3
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
            if (moduleConfig.audio.i2s_din)
                DEBUG_MSG("♪♫♪ Initializing DAC on Pin %u\n", moduleConfig.audio.amp_pin ? moduleConfig.audio.amp_pin : AAMP);
#endif
            // Configure PTT input
            DEBUG_MSG("♪♫♪ Initializing PTT on Pin %u\n", moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN);
            pinMode(moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN, INPUT);

            firstTime = false;
        } else {
            // Check if PTT is pressed. TODO hook that into Onebutton/Interrupt drive.
            if (digitalRead(moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN) == HIGH) {
                if (radio_state == RadioState::rx) {
                    DEBUG_MSG("♪♫♪ PTT pressed, switching to TX\n");
                    radio_state = RadioState::tx;
                }
            } else {
                if (radio_state == RadioState::tx) {
                    DEBUG_MSG("♪♫♪ PTT released, switching to RX\n");
                    radio_state = RadioState::rx;
                }
            }
            if ((radio_state == RadioState::tx) && moduleConfig.audio.i2s_sd) {
                // Get I2S data from the microphone and place in data buffer
                size_t bytesIn = 0;
                res = i2s_read(I2S_PORT, &adc_buffer + adc_buffer_index, ADC_BUFFER_SIZE - adc_buffer_index, &bytesIn, pdMS_TO_TICKS(40)); // wait 40ms for audio to arrive.

                if (res == ESP_OK) {
                    adc_buffer_index += bytesIn;
                    if (adc_buffer_index == ADC_BUFFER_SIZE) {
                        adc_buffer_index = 0;
                        DEBUG_MSG("♪♫♪ We have a full buffer, process it\n");
                        memcpy((void*)speech, (void*)adc_buffer, 2 * ADC_BUFFER_SIZE);
                        // Notify codec2 task that the buffer is ready.
                        codec2Thread->notify(RadioState::tx, true);
                    }
                }
            }
        }
        return 100;
    } else {
        DEBUG_MSG("♪♫♪ Audio Module Disabled\n");
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

    p->want_ack = false; // Audio is shoot&forget. TODO: Is this really suppressing retransmissions?
    p->priority = MeshPacket_Priority_MAX; // Audio is important, because realtime

    p->decoded.payload.size = codec2Thread->get_encode_frame_size();
    memcpy(p->decoded.payload.bytes, tx_encode_frame, p->decoded.payload.size);

    service.sendToMesh(p);
}

ProcessMessage AudioModule::handleReceived(const MeshPacket &mp)
{
    if ((moduleConfig.audio.codec2_enabled) && (myRegion->audioPermitted)) {
        auto &p = mp.decoded;
        if (getFrom(&mp) != nodeDB.getNodeNum()) {
            if (p.payload.size == codec2Thread->get_encode_frame_size()) {
                memcpy(rx_encode_frame, p.payload.bytes, p.payload.size);
                radio_state = RadioState::rx;
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                codec2Thread->notifyFromISR(&xHigherPriorityTaskWoken, RadioState::rx, true);
                if (xHigherPriorityTaskWoken)
                    portYIELD_FROM_ISR();
            } else {
                DEBUG_MSG("♪♫♪ Invalid payload size %u != %u\n", p.payload.size, codec2Thread->get_encode_frame_size());
            }
        }
    }

    return ProcessMessage::CONTINUE;
}

#endif