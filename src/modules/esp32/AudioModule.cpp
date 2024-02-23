
#include "configuration.h"
#if defined(ARCH_ESP32) && defined(USE_SX1280)
#include "AudioModule.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"

#ifdef OLED_RU
#include "graphics/fonts/OLEDDisplayFontsRU.h"
#endif

#ifdef OLED_UA
#include "graphics/fonts/OLEDDisplayFontsUA.h"
#endif

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

TaskHandle_t codec2HandlerTask;
AudioModule *audioModule;

#ifdef ARCH_ESP32
// ESP32 doesn't use that flag
#define YIELD_FROM_ISR(x) portYIELD_FROM_ISR()
#else
#define YIELD_FROM_ISR(x) portYIELD_FROM_ISR(x)
#endif

#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ST7735_CS) || defined(ST7789_CS)) &&                                \
    !defined(DISPLAY_FORCE_SMALL_FONTS)

// The screen is bigger so use bigger fonts
#define FONT_SMALL ArialMT_Plain_16
#define FONT_MEDIUM ArialMT_Plain_24
#define FONT_LARGE ArialMT_Plain_24
#else
#ifdef OLED_RU
#define FONT_SMALL ArialMT_Plain_10_RU
#else
#ifdef OLED_UA
#define FONT_SMALL ArialMT_Plain_10_UA
#else
#define FONT_SMALL ArialMT_Plain_10
#endif
#endif
#define FONT_MEDIUM ArialMT_Plain_16
#define FONT_LARGE ArialMT_Plain_24
#endif

#define fontHeight(font) ((font)[1] + 1) // height is position 1

#define FONT_HEIGHT_SMALL fontHeight(FONT_SMALL)
#define FONT_HEIGHT_MEDIUM fontHeight(FONT_MEDIUM)
#define FONT_HEIGHT_LARGE fontHeight(FONT_LARGE)

void run_codec2(void *parameter)
{
    // 4 bytes of header in each frame hex c0 de c2 plus the bitrate
    memcpy(audioModule->tx_encode_frame, &audioModule->tx_header, sizeof(audioModule->tx_header));

    LOG_INFO("Starting codec2 task\n");

    while (true) {
        uint32_t tcount = ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(10000));

        if (tcount != 0) {
            if (audioModule->radio_state == RadioState::tx) {
                for (int i = 0; i < audioModule->adc_buffer_size; i++)
                    audioModule->speech[i] = (int16_t)hp_filter.Update((float)audioModule->speech[i]);

                codec2_encode(audioModule->codec2, audioModule->tx_encode_frame + audioModule->tx_encode_frame_index,
                              audioModule->speech);
                audioModule->tx_encode_frame_index += audioModule->encode_codec_size;

                if (audioModule->tx_encode_frame_index == (audioModule->encode_frame_size + sizeof(audioModule->tx_header))) {
                    LOG_INFO("Sending %d codec2 bytes\n", audioModule->encode_frame_size);
                    audioModule->sendPayload();
                    audioModule->tx_encode_frame_index = sizeof(audioModule->tx_header);
                }
            }
            if (audioModule->radio_state == RadioState::rx) {
                size_t bytesOut = 0;
                if (memcmp(audioModule->rx_encode_frame, &audioModule->tx_header, sizeof(audioModule->tx_header)) == 0) {
                    for (int i = 4; i < audioModule->rx_encode_frame_index; i += audioModule->encode_codec_size) {
                        codec2_decode(audioModule->codec2, audioModule->output_buffer, audioModule->rx_encode_frame + i);
                        i2s_write(I2S_PORT, &audioModule->output_buffer, audioModule->adc_buffer_size, &bytesOut,
                                  pdMS_TO_TICKS(500));
                    }
                } else {
                    // if the buffer header does not match our own codec, make a temp decoding setup.
                    CODEC2 *tmp_codec2 = codec2_create(audioModule->rx_encode_frame[3]);
                    codec2_set_lpc_post_filter(tmp_codec2, 1, 0, 0.8, 0.2);
                    int tmp_encode_codec_size = (codec2_bits_per_frame(tmp_codec2) + 7) / 8;
                    int tmp_adc_buffer_size = codec2_samples_per_frame(tmp_codec2);
                    for (int i = 4; i < audioModule->rx_encode_frame_index; i += tmp_encode_codec_size) {
                        codec2_decode(tmp_codec2, audioModule->output_buffer, audioModule->rx_encode_frame + i);
                        i2s_write(I2S_PORT, &audioModule->output_buffer, tmp_adc_buffer_size, &bytesOut, pdMS_TO_TICKS(500));
                    }
                    codec2_destroy(tmp_codec2);
                }
            }
        }
    }
}

AudioModule::AudioModule() : SinglePortModule("AudioModule", meshtastic_PortNum_AUDIO_APP), concurrency::OSThread("AudioModule")
{
    // moduleConfig.audio.codec2_enabled = true;
    // moduleConfig.audio.i2s_ws = 13;
    // moduleConfig.audio.i2s_sd = 15;
    // moduleConfig.audio.i2s_din = 22;
    // moduleConfig.audio.i2s_sck = 14;
    // moduleConfig.audio.ptt_pin = 39;

    if ((moduleConfig.audio.codec2_enabled) && (myRegion->audioPermitted)) {
        LOG_INFO("Setting up codec2 in mode %u",
                 (moduleConfig.audio.bitrate ? moduleConfig.audio.bitrate : AUDIO_MODULE_MODE) - 1);
        codec2 = codec2_create((moduleConfig.audio.bitrate ? moduleConfig.audio.bitrate : AUDIO_MODULE_MODE) - 1);
        memcpy(tx_header.magic, c2_magic, sizeof(c2_magic));
        tx_header.mode = (moduleConfig.audio.bitrate ? moduleConfig.audio.bitrate : AUDIO_MODULE_MODE) - 1;
        codec2_set_lpc_post_filter(codec2, 1, 0, 0.8, 0.2);
        encode_codec_size = (codec2_bits_per_frame(codec2) + 7) / 8;
        encode_frame_num = (meshtastic_Constants_DATA_PAYLOAD_LEN - sizeof(tx_header)) / encode_codec_size;
        encode_frame_size = encode_frame_num * encode_codec_size; // max 233 bytes + 4 header bytes
        adc_buffer_size = codec2_samples_per_frame(codec2);
        LOG_INFO("using %d frames of %d bytes for a total payload length of %d bytes\n", encode_frame_num, encode_codec_size,
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
            // Set up I2S Processor configuration. This will produce 16bit samples at 8 kHz instead of 12 from the ADC
            LOG_INFO("Initializing I2S SD: %d DIN: %d WS: %d SCK: %d\n", moduleConfig.audio.i2s_sd, moduleConfig.audio.i2s_din,
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
            res = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
            if (res != ESP_OK) {
                LOG_ERROR("Failed to install I2S driver: %d\n", res);
            }

            const i2s_pin_config_t pin_config = {
                .bck_io_num = moduleConfig.audio.i2s_sck,
                .ws_io_num = moduleConfig.audio.i2s_ws,
                .data_out_num = moduleConfig.audio.i2s_din ? moduleConfig.audio.i2s_din : I2S_PIN_NO_CHANGE,
                .data_in_num = moduleConfig.audio.i2s_sd ? moduleConfig.audio.i2s_sd : I2S_PIN_NO_CHANGE};
            res = i2s_set_pin(I2S_PORT, &pin_config);
            if (res != ESP_OK) {
                LOG_ERROR("Failed to set I2S pin config: %d\n", res);
            }

            res = i2s_start(I2S_PORT);
            if (res != ESP_OK) {
                LOG_ERROR("Failed to start I2S: %d\n", res);
            }

            radio_state = RadioState::rx;

            // Configure PTT input
            LOG_INFO("Initializing PTT on Pin %u\n", moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN);
            pinMode(moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN, INPUT);

            firstTime = false;
        } else {
            UIFrameEvent e = {false, true};
            // Check if PTT is pressed. TODO hook that into Onebutton/Interrupt drive.
            if (digitalRead(moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN) == HIGH) {
                if (radio_state == RadioState::rx) {
                    LOG_INFO("PTT pressed, switching to TX\n");
                    radio_state = RadioState::tx;
                    e.frameChanged = true;
                    this->notifyObservers(&e);
                }
            } else {
                if (radio_state == RadioState::tx) {
                    LOG_INFO("PTT released, switching to RX\n");
                    if (tx_encode_frame_index > sizeof(tx_header)) {
                        // Send the incomplete frame
                        LOG_INFO("Sending %d codec2 bytes (incomplete)\n", tx_encode_frame_index);
                        sendPayload();
                    }
                    tx_encode_frame_index = sizeof(tx_header);
                    radio_state = RadioState::rx;
                    e.frameChanged = true;
                    this->notifyObservers(&e);
                }
            }
            if (radio_state == RadioState::tx) {
                // Get I2S data from the microphone and place in data buffer
                size_t bytesIn = 0;
                res = i2s_read(I2S_PORT, adc_buffer + adc_buffer_index, adc_buffer_size - adc_buffer_index, &bytesIn,
                               pdMS_TO_TICKS(40)); // wait 40ms for audio to arrive.

                if (res == ESP_OK) {
                    adc_buffer_index += bytesIn;
                    if (adc_buffer_index == adc_buffer_size) {
                        adc_buffer_index = 0;
                        memcpy((void *)speech, (void *)adc_buffer, 2 * adc_buffer_size);
                        // Notify run_codec2 task that the buffer is ready.
                        radio_state = RadioState::tx;
                        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                        vTaskNotifyGiveFromISR(codec2HandlerTask, &xHigherPriorityTaskWoken);
                        if (xHigherPriorityTaskWoken == pdTRUE)
                            YIELD_FROM_ISR(xHigherPriorityTaskWoken);
                    }
                }
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

    service.sendToMesh(p);
}

ProcessMessage AudioModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if ((moduleConfig.audio.codec2_enabled) && (myRegion->audioPermitted)) {
        auto &p = mp.decoded;
        if (getFrom(&mp) != nodeDB.getNodeNum()) {
            memcpy(rx_encode_frame, p.payload.bytes, p.payload.size);
            radio_state = RadioState::rx;
            rx_encode_frame_index = p.payload.size;
            // Notify run_codec2 task that the buffer is ready.
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            vTaskNotifyGiveFromISR(codec2HandlerTask, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken == pdTRUE)
                YIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }

    return ProcessMessage::CONTINUE;
}

#endif