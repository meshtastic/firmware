#include "configuration.h"
#if defined(ARCH_ESP32) && defined(USE_SX1280)
#include "AudioModule.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"

/*
    AudioModule
        A interface to send raw codec2 audio data over the mesh network. Based on the example code from the ESP32_codec2 project.
        https://github.com/deulis/ESP32_Codec2

        Codec 2 is a low-bitrate speech audio codec (speech coding)
        that is patent free and open source develop by David Grant Rowe.
        http://www.rowetel.com/ and https://github.com/drowe67/codec2

    Basic Usage:
        1) Enable the module by setting audio.codec2_enabled to 1.
        2) Set the pins for the I2S interface. Recommended on TLora is I2S_WS 13/I2S_SD 15/I2S_SIN 2/I2S_SCK 14
        3) Set audio.bitrate to the desired codec2 rate (CODEC2_3200, CODEC2_2400, CODEC2_1600, CODEC2_1400, CODEC2_1300,
   CODEC2_1200, CODEC2_700, CODEC2_700B)

    KNOWN PROBLEMS
        * Half Duplex
        * Will not work on NRF and the Linux device targets (yet?).
*/

ButterworthFilter hp_filter(240, 8000, ButterworthFilter::ButterworthFilter::Highpass, 1);

// Volume gain applied to mic input and speaker output.
// Increase to make audio louder; decrease if clipping/distortion occurs.
static constexpr int AUDIO_GAIN = 8;

TaskHandle_t codec2HandlerTask;
AudioModule *audioModule;

#ifdef ARCH_ESP32
// ESP32 doesn't use that flag
#define YIELD_FROM_ISR(x) portYIELD_FROM_ISR()
#else
#define YIELD_FROM_ISR(x) portYIELD_FROM_ISR(x)
#endif

#include "graphics/ScreenFonts.h"

void run_codec2(void *parameter)
{
    // 4 bytes of header in each frame hex c0 de c2 plus the bitrate
    memcpy(audioModule->tx_encode_frame, &audioModule->tx_header, sizeof(audioModule->tx_header));

    LOG_INFO("Start codec2 task");

    while (true) {
        uint32_t tcount = ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(10000));

        if (tcount != 0) {
            if (audioModule->radio_state == RadioState::tx) {
                for (int i = 0; i < audioModule->adc_buffer_size; i++) {
                    int32_t sample = (int32_t)hp_filter.Update((float)audioModule->speech[i]) * AUDIO_GAIN;
                    if (sample > 32767) sample = 32767;
                    if (sample < -32768) sample = -32768;
                    audioModule->speech[i] = (int16_t)sample;
                }

                codec2_encode(audioModule->codec2, audioModule->tx_encode_frame + audioModule->tx_encode_frame_index,
                              audioModule->speech);
                audioModule->tx_encode_frame_index += audioModule->encode_codec_size;

                if (audioModule->tx_encode_frame_index == (audioModule->encode_frame_size + sizeof(audioModule->tx_header))) {
                    LOG_INFO("Send %d codec2 bytes", audioModule->encode_frame_size);
                    audioModule->sendPayload();
                    audioModule->tx_encode_frame_index = sizeof(audioModule->tx_header);
                }
            }
            if (audioModule->radio_state == RadioState::rx) {
                size_t bytesOut = 0;
                if (memcmp(audioModule->rx_encode_frame, &audioModule->tx_header, sizeof(audioModule->tx_header)) == 0) {
                    for (int i = 4; i < audioModule->rx_encode_frame_index; i += audioModule->encode_codec_size) {
                        codec2_decode(audioModule->codec2, audioModule->output_buffer, audioModule->rx_encode_frame + i);
                        for (int j = 0; j < audioModule->adc_buffer_size; j++) {
                            int32_t s = (int32_t)audioModule->output_buffer[j] * AUDIO_GAIN;
                            if (s > 32767) s = 32767;
                            if (s < -32768) s = -32768;
                            audioModule->output_buffer[j] = (int16_t)s;
                        }
                        i2s_write(I2S_PORT_SPK, audioModule->output_buffer,
                                  audioModule->adc_buffer_size * sizeof(int16_t), &bytesOut, pdMS_TO_TICKS(500));
                    }
                } else {
                    // if the buffer header does not match our own codec, make a temp decoding setup.
                    CODEC2 *tmp_codec2 = codec2_create(audioModule->rx_encode_frame[3]);
                    codec2_set_lpc_post_filter(tmp_codec2, 1, 0, 0.8, 0.2);
                    int tmp_encode_codec_size = (codec2_bits_per_frame(tmp_codec2) + 7) / 8;
                    int tmp_adc_buffer_size = codec2_samples_per_frame(tmp_codec2);
                    for (int i = 4; i < audioModule->rx_encode_frame_index; i += tmp_encode_codec_size) {
                        codec2_decode(tmp_codec2, audioModule->output_buffer, audioModule->rx_encode_frame + i);
                        for (int j = 0; j < tmp_adc_buffer_size; j++) {
                            int32_t s = (int32_t)audioModule->output_buffer[j] * AUDIO_GAIN;
                            if (s > 32767) s = 32767;
                            if (s < -32768) s = -32768;
                            audioModule->output_buffer[j] = (int16_t)s;
                        }
                        i2s_write(I2S_PORT_SPK, audioModule->output_buffer,
                                  tmp_adc_buffer_size * sizeof(int16_t), &bytesOut, pdMS_TO_TICKS(500));
                    }
                    codec2_destroy(tmp_codec2);
                }
            }
        }
    }
}

AudioModule::AudioModule() : SinglePortModule("Audio", meshtastic_PortNum_AUDIO_APP), concurrency::OSThread("Audio")
{
    // moduleConfig.audio.codec2_enabled = true;
    // moduleConfig.audio.i2s_ws = 13;
    // moduleConfig.audio.i2s_sd = 15;
    // moduleConfig.audio.i2s_din = 22;
    // moduleConfig.audio.i2s_sck = 14;
    // moduleConfig.audio.ptt_pin = 39;

    if ((moduleConfig.audio.codec2_enabled) && (myRegion->audioPermitted)) {
        LOG_INFO("Set up codec2 in mode %u", (moduleConfig.audio.bitrate ? moduleConfig.audio.bitrate : AUDIO_MODULE_MODE) - 1);
        codec2 = codec2_create((moduleConfig.audio.bitrate ? moduleConfig.audio.bitrate : AUDIO_MODULE_MODE) - 1);
        memcpy(tx_header.magic, c2_magic, sizeof(c2_magic));
        tx_header.mode = (moduleConfig.audio.bitrate ? moduleConfig.audio.bitrate : AUDIO_MODULE_MODE) - 1;
        codec2_set_lpc_post_filter(codec2, 1, 0, 0.8, 0.2);
        encode_codec_size = (codec2_bits_per_frame(codec2) + 7) / 8;
        encode_frame_num = (meshtastic_Constants_DATA_PAYLOAD_LEN - sizeof(tx_header)) / encode_codec_size;
        encode_frame_size = encode_frame_num * encode_codec_size; // max 233 bytes + 4 header bytes
        adc_buffer_size = codec2_samples_per_frame(codec2);
        LOG_INFO("Use %d frames of %d bytes for a total payload length of %d bytes", encode_frame_num, encode_codec_size,
                 encode_frame_size);
        xTaskCreate(&run_codec2, "codec2_task", 30000, NULL, 5, &codec2HandlerTask);
    } else {
        disable();
    }
}

void AudioModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    char buffer[50];

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->fillRect(0 + x, 0 + y, x + display->getWidth(), y + FONT_HEIGHT_SMALL);
    display->setColor(BLACK);
    display->drawStringf(0 + x, 0 + y, buffer, "Codec2 Mode %d Audio",
                         (moduleConfig.audio.bitrate ? moduleConfig.audio.bitrate : AUDIO_MODULE_MODE) - 1);
    display->setColor(WHITE);
    display->setFont(FONT_LARGE);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    switch (radio_state) {
    case RadioState::tx:
        display->drawString(display->getWidth() / 2 + x, (display->getHeight() - FONT_HEIGHT_SMALL) / 2 + y, "PTT");
        break;
    default:
        display->drawString(display->getWidth() / 2 + x, (display->getHeight() - FONT_HEIGHT_SMALL) / 2 + y, "Receive");
        break;
    }
}

int32_t AudioModule::runOnce()
{
    if ((moduleConfig.audio.codec2_enabled) && (myRegion->audioPermitted)) {
        esp_err_t res;
        if (firstTime) {
#ifdef AUDIO_I2S_DUAL
            // ---- Dual I2S mode (e.g. MVSR board): separate buses for mic and speaker ----
#ifdef AUDIO_I2S_MIC_PDM
            LOG_INFO("Init dual I2S — PDM Mic CLK:%d DATA:%d EN:%d | Spk SCK:%d WS:%d DIN:%d EN:%d",
                     AUDIO_I2S_MIC_CLK, AUDIO_I2S_MIC_DATA, AUDIO_I2S_MIC_EN,
                     AUDIO_I2S_SPK_SCK, AUDIO_I2S_SPK_WS, AUDIO_I2S_SPK_DIN, AUDIO_I2S_SPK_EN);

            // --- Microphone I2S in PDM RX mode on I2S_NUM_0 ---
            i2s_config_t mic_config = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
                                       .sample_rate = 8000,
                                       .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
                                       .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
                                       .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
                                       .intr_alloc_flags = 0,
                                       .dma_buf_count = 8,
                                       .dma_buf_len = adc_buffer_size,
                                       .use_apll = false,
                                       .tx_desc_auto_clear = false,
                                       .fixed_mclk = 0};
            res = i2s_driver_install(I2S_PORT_MIC, &mic_config, 0, NULL);
            if (res != ESP_OK)
                LOG_ERROR("Failed to install mic I2S PDM driver: %d", res);

            // For PDM RX on ESP32-S3: CLK output is via ws_io_num, data input via data_in_num
            const i2s_pin_config_t mic_pins = {.mck_io_num = I2S_PIN_NO_CHANGE,
                                               .bck_io_num = I2S_PIN_NO_CHANGE,
                                               .ws_io_num = AUDIO_I2S_MIC_CLK,
                                               .data_out_num = I2S_PIN_NO_CHANGE,
                                               .data_in_num = AUDIO_I2S_MIC_DATA};
#else
            LOG_INFO("Init dual I2S — Mic SCK:%d WS:%d SD:%d EN:%d | Spk SCK:%d WS:%d DIN:%d EN:%d",
                     AUDIO_I2S_MIC_SCK, AUDIO_I2S_MIC_WS, AUDIO_I2S_MIC_SD, AUDIO_I2S_MIC_EN,
                     AUDIO_I2S_SPK_SCK, AUDIO_I2S_SPK_WS, AUDIO_I2S_SPK_DIN, AUDIO_I2S_SPK_EN);

            // --- Microphone I2S (standard RX) on I2S_NUM_0 ---
            i2s_config_t mic_config = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
                                       .sample_rate = 8000,
                                       .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
                                       .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
                                       .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
                                       .intr_alloc_flags = 0,
                                       .dma_buf_count = 8,
                                       .dma_buf_len = adc_buffer_size,
                                       .use_apll = false,
                                       .tx_desc_auto_clear = false,
                                       .fixed_mclk = 0};
            res = i2s_driver_install(I2S_PORT_MIC, &mic_config, 0, NULL);
            if (res != ESP_OK)
                LOG_ERROR("Failed to install mic I2S driver: %d", res);

            const i2s_pin_config_t mic_pins = {.mck_io_num = I2S_PIN_NO_CHANGE,
                                               .bck_io_num = AUDIO_I2S_MIC_SCK,
                                               .ws_io_num = AUDIO_I2S_MIC_WS,
                                               .data_out_num = I2S_PIN_NO_CHANGE,
                                               .data_in_num = AUDIO_I2S_MIC_SD};
#endif
            res = i2s_set_pin(I2S_PORT_MIC, &mic_pins);
            if (res != ESP_OK)
                LOG_ERROR("Failed to set mic I2S pins: %d", res);

            res = i2s_start(I2S_PORT_MIC);
            if (res != ESP_OK)
                LOG_ERROR("Failed to start mic I2S: %d", res);

            // --- Speaker I2S (TX only) on I2S_NUM_1 ---
            i2s_config_t spk_config = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
                                       .sample_rate = 8000,
                                       .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
                                       .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
                                       .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
                                       .intr_alloc_flags = 0,
                                       .dma_buf_count = 8,
                                       .dma_buf_len = adc_buffer_size,
                                       .use_apll = false,
                                       .tx_desc_auto_clear = true,
                                       .fixed_mclk = 0};
            res = i2s_driver_install(I2S_PORT_SPK, &spk_config, 0, NULL);
            if (res != ESP_OK)
                LOG_ERROR("Failed to install speaker I2S driver: %d", res);

            const i2s_pin_config_t spk_pins = {.mck_io_num = I2S_PIN_NO_CHANGE,
                                               .bck_io_num = AUDIO_I2S_SPK_SCK,
                                               .ws_io_num = AUDIO_I2S_SPK_WS,
                                               .data_out_num = AUDIO_I2S_SPK_DIN,
                                               .data_in_num = I2S_PIN_NO_CHANGE};
            res = i2s_set_pin(I2S_PORT_SPK, &spk_pins);
            if (res != ESP_OK)
                LOG_ERROR("Failed to set speaker I2S pins: %d", res);

            res = i2s_start(I2S_PORT_SPK);
            if (res != ESP_OK)
                LOG_ERROR("Failed to start speaker I2S: %d", res);

            // Enable mic (active LOW) and speaker amplifier via their enable pins
            pinMode(AUDIO_I2S_MIC_EN, OUTPUT);
            digitalWrite(AUDIO_I2S_MIC_EN, LOW);
            pinMode(AUDIO_I2S_SPK_EN, OUTPUT);
            digitalWrite(AUDIO_I2S_SPK_EN, HIGH);

#else
            // ---- Single I2S mode: shared bus for mic + speaker ----
            // Set up I2S Processor configuration. This will produce 16bit samples at 8 kHz instead of 12 from the ADC
            LOG_INFO("Init I2S SD: %d DIN: %d WS: %d SCK: %d", moduleConfig.audio.i2s_sd, moduleConfig.audio.i2s_din,
                     moduleConfig.audio.i2s_ws, moduleConfig.audio.i2s_sck);
            i2s_config_t i2s_config = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | (moduleConfig.audio.i2s_sd ? I2S_MODE_RX : 0) |
                                                            (moduleConfig.audio.i2s_din ? I2S_MODE_TX : 0)),
                                       .sample_rate = 8000,
                                       .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
                                       .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
                                       .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
                                       .intr_alloc_flags = 0,
                                       .dma_buf_count = 8,
                                       .dma_buf_len = adc_buffer_size, // 320 * 2 bytes
                                       .use_apll = false,
                                       .tx_desc_auto_clear = true,
                                       .fixed_mclk = 0};
            res = i2s_driver_install(I2S_PORT_MIC, &i2s_config, 0, NULL);
            if (res != ESP_OK) {
                LOG_ERROR("Failed to install I2S driver: %d", res);
            }

            const i2s_pin_config_t pin_config = {
                .mck_io_num = I2S_PIN_NO_CHANGE,
                .bck_io_num = moduleConfig.audio.i2s_sck,
                .ws_io_num = moduleConfig.audio.i2s_ws,
                .data_out_num = moduleConfig.audio.i2s_din ? moduleConfig.audio.i2s_din : I2S_PIN_NO_CHANGE,
                .data_in_num = moduleConfig.audio.i2s_sd ? moduleConfig.audio.i2s_sd : I2S_PIN_NO_CHANGE};
            res = i2s_set_pin(I2S_PORT_MIC, &pin_config);
            if (res != ESP_OK) {
                LOG_ERROR("Failed to set I2S pin config: %d", res);
            }

            res = i2s_start(I2S_PORT_MIC);
            if (res != ESP_OK) {
                LOG_ERROR("Failed to start I2S: %d", res);
            }
#endif

            radio_state = RadioState::rx;

            // Configure PTT input with pull-up; PTT is active LOW (shorted to GND when pressed).
            // This prevents a floating pin from causing spurious REGENERATE_FRAMESET calls
            // that would reset the screen to frame 0 and break user button navigation.
            uint8_t pttPin = moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN;
            LOG_INFO("Init PTT on Pin %u", pttPin);
            pinMode(pttPin, INPUT_PULLUP);

            // Log the initial PTT state to help diagnose hardware that holds the pin LOW
            int initialPttState = digitalRead(pttPin);
            LOG_WARN("PTT pin %u initial state after INPUT_PULLUP: %s",
                     pttPin, initialPttState == LOW ? "LOW (PTT active!)" : "HIGH (idle)");

            firstTime = false;
        } else {
            UIFrameEvent e;
            // Periodic debug to verify cooperative scheduler is alive
            static uint32_t lastAudioDebug = 0;
            if (millis() - lastAudioDebug > 5000) {
                lastAudioDebug = millis();
                uint8_t pttPin = moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN;
                int pttVal = digitalRead(pttPin);
                LOG_WARN("Audio runOnce: state=%s ptt_pin=%d val=%d",
                         radio_state == RadioState::tx ? "TX" : "RX", pttPin, pttVal);
            }
            // Check if PTT is pressed (active LOW). TODO hook that into Onebutton/Interrupt drive.
            if (digitalRead(moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN) == LOW) {
                if (radio_state == RadioState::rx) {
                    LOG_INFO("PTT pressed, switching to TX");
                    powerFSM.trigger(EVENT_INPUT); // Wake screen on PTT press
                    radio_state = RadioState::tx;
                    requestFocus(); // Focus on the audio frame when PTT is pressed
                    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET; // Add audio frame + focus on it
                    this->notifyObservers(&e);
                }
            } else {
                if (radio_state == RadioState::tx) {
                    LOG_INFO("PTT released, switching to RX");
                    if (tx_encode_frame_index > sizeof(tx_header)) {
                        // Send the incomplete frame
                        LOG_INFO("Send %d codec2 bytes (incomplete)", tx_encode_frame_index);
                        sendPayload();
                    }
                    tx_encode_frame_index = sizeof(tx_header);
                    radio_state = RadioState::rx;
                    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET_BACKGROUND; // Remove audio frame, preserve user's position
                    this->notifyObservers(&e);
                }
            }
            if (radio_state == RadioState::tx) {
                // Drain all available I2S DMA data in a loop to avoid falling behind.
                // At 8kHz/16-bit/mono = 16KB/s, each codec2 frame is 320 samples (640 bytes, 40ms).
                // We must read faster than data arrives to avoid DMA overflow.
                size_t bytesIn = 0;
                int framesEncoded = 0;
                const int maxFramesPerCall = 4; // cap to avoid blocking too long

                do {
                    size_t readSize = (adc_buffer_size - adc_buffer_index) * sizeof(uint16_t);
                    res = i2s_read(I2S_PORT_MIC, (uint8_t *)adc_buffer + adc_buffer_index * sizeof(uint16_t), readSize, &bytesIn,
                                   pdMS_TO_TICKS(20));

                    if (res == ESP_OK && bytesIn > 0) {
                        adc_buffer_index += bytesIn / sizeof(uint16_t);
                        if (adc_buffer_index >= (uint16_t)adc_buffer_size) {
                            adc_buffer_index = 0;
                            memcpy((void *)speech, (void *)adc_buffer, adc_buffer_size * sizeof(int16_t));
                            // Notify run_codec2 task that the buffer is ready.
                            xTaskNotifyGive(codec2HandlerTask);
                            framesEncoded++;
                        }
                    }
                } while (res == ESP_OK && bytesIn > 0 && framesEncoded < maxFramesPerCall);
            }
        }
        return 100;
    } else {
        return disable();
    }
}

meshtastic_MeshPacket *AudioModule::allocReply()
{
    auto reply = allocDataPacket();
    return reply;
}

bool AudioModule::shouldDraw()
{
    if (!moduleConfig.audio.codec2_enabled) {
        return false;
    }
    return (radio_state == RadioState::tx);
}

void AudioModule::sendPayload(NodeNum dest, bool wantReplies)
{
    meshtastic_MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;

    p->want_ack = false;                              // Audio is shoot&forget. No need to wait for ACKs.
    p->priority = meshtastic_MeshPacket_Priority_MAX; // Audio is important, because realtime

    p->decoded.payload.size = tx_encode_frame_index;
    memcpy(p->decoded.payload.bytes, tx_encode_frame, p->decoded.payload.size);

    service->sendToMesh(p);
}

ProcessMessage AudioModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if ((moduleConfig.audio.codec2_enabled) && (myRegion->audioPermitted)) {
        auto &p = mp.decoded;
        if (!isFromUs(&mp)) {
            memcpy(rx_encode_frame, p.payload.bytes, p.payload.size);
            radio_state = RadioState::rx;
            rx_encode_frame_index = p.payload.size;
            // Notify run_codec2 task that the buffer is ready.
            xTaskNotifyGive(codec2HandlerTask);
        }
    }

    return ProcessMessage::CONTINUE;
}

#endif