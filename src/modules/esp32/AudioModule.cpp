#include "configuration.h"
#if defined(ARCH_ESP32) && defined(USE_SX1280) && !MESHTASTIC_EXCLUDE_AUDIO
#include "AudioModule.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "gps/RTC.h"
#include <cmath>

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
   CODEC2_1200, CODEC2_700C, CODEC2_450)

    KNOWN PROBLEMS
        * Half Duplex
        * Will not work on NRF and the Linux device targets (yet?).
*/

namespace
{
class HighpassFilter
{
  public:
    HighpassFilter(float cutoffHz, float sampleRateHz, float damping)
    {
        constexpr float kPi = 3.14159265358979323846f;
        const float c = std::tan(kPi * cutoffHz / sampleRateHz);
        a1 = 1.0f / (1.0f + damping * c + c * c);
        a2 = -2.0f * a1;
        a3 = a1;
        b1 = 2.0f * (c * c - 1.0f) * a1;
        b2 = (1.0f - damping * c + c * c) * a1;
    }

    float update(float x0)
    {
        const float y0 = a1 * x0 + a2 * x1 + a3 * x2 - b1 * y1 - b2 * y2;
        x2 = x1;
        x1 = x0;
        y2 = y1;
        y1 = y0;
        return y0;
    }

  private:
    float a1 = 0.0f;
    float a2 = 0.0f;
    float a3 = 0.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float x1 = 0.0f;
    float x2 = 0.0f;
    float y1 = 0.0f;
    float y2 = 0.0f;
};

int audioBaudToCodec2Mode(meshtastic_ModuleConfig_AudioConfig_Audio_Baud baud)
{
    if (baud == meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_DEFAULT)
        baud = AUDIO_MODULE_MODE;

    switch (baud) {
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_3200:
        return CODEC2_MODE_3200;
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_2400:
        return CODEC2_MODE_2400;
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_1600:
        return CODEC2_MODE_1600;
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_1400:
        return CODEC2_MODE_1400;
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_1300:
        return CODEC2_MODE_1300;
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_1200:
        return CODEC2_MODE_1200;
    // Legacy 700/700B were removed upstream; fall back to 700C as the proto documents.
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_700:
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_700B:
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_700C:
        return CODEC2_MODE_700C;
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_450:
        return CODEC2_MODE_450;
    default:
        return -1;
    }
}
} // namespace

HighpassFilter hp_filter(240.0f, 8000.0f, 1.0f);

TaskHandle_t codec2HandlerTask = nullptr;
AudioModule *audioModule = nullptr;

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
                for (int i = 0; i < audioModule->adc_buffer_size; i++)
                    audioModule->speech[i] = (int16_t)hp_filter.update((float)audioModule->speech[i]);

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
                // Reject frames not carrying our own header (magic + mode); an invalid mode byte
                // would else NULL-deref via codec2_create. The bound keeps reads inside the buffer.
                const int headerLen = sizeof(audioModule->tx_header);
                const int frameSize = audioModule->encode_codec_size;
                int limit = audioModule->rx_encode_frame_index;
                if (limit > (int)sizeof(audioModule->rx_encode_frame))
                    limit = sizeof(audioModule->rx_encode_frame);
                if (frameSize > 0 && limit >= headerLen &&
                    memcmp(audioModule->rx_encode_frame, &audioModule->tx_header, headerLen) == 0) {
                    for (int i = headerLen; i + frameSize <= limit; i += frameSize) {
                        codec2_decode(audioModule->codec2, audioModule->output_buffer, audioModule->rx_encode_frame + i);
                        // adc_buffer_size is a sample count; i2s_write wants bytes (int16_t samples).
                        i2s_write(I2S_PORT, &audioModule->output_buffer, audioModule->adc_buffer_size * sizeof(int16_t),
                                  &bytesOut, pdMS_TO_TICKS(500));
                    }
                } else {
                    LOG_WARN("Audio: dropping frame with mismatched or short codec2 header");
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

    if ((moduleConfig.audio.codec2_enabled) && (myRegion->profile->audioPermitted)) {
        const int mode = audioBaudToCodec2Mode(moduleConfig.audio.bitrate);
        if (mode >= 0) {
            LOG_INFO("Set up codec2 in mode %d", mode);
            codec2 = codec2_create(mode);
        }
        if (codec2 == nullptr) {
            LOG_ERROR("Unsupported or unavailable codec2 bitrate %d (mode %d); disabling AudioModule",
                      (int)moduleConfig.audio.bitrate, mode);
            disable();
            return;
        }
        memcpy(tx_header.magic, c2_magic, sizeof(c2_magic));
        tx_header.mode = static_cast<char>(mode);
        codec2_set_lpc_post_filter(codec2, 1, 0, 0.8f, 0.2f);
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
    display->drawStringf(0 + x, 0 + y, buffer, "Codec2 Mode %d Audio", (int)tx_header.mode);
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
    if ((moduleConfig.audio.codec2_enabled) && (myRegion->profile->audioPermitted) && (codec2 != nullptr) &&
        (codec2HandlerTask != nullptr)) {
        esp_err_t res;
        if (firstTime) {
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
            res = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
            if (res != ESP_OK) {
                LOG_ERROR("Failed to install I2S driver: %d", res);
            }

            const i2s_pin_config_t pin_config = {
                .bck_io_num = moduleConfig.audio.i2s_sck,
                .ws_io_num = moduleConfig.audio.i2s_ws,
                .data_out_num = moduleConfig.audio.i2s_din ? moduleConfig.audio.i2s_din : I2S_PIN_NO_CHANGE,
                .data_in_num = moduleConfig.audio.i2s_sd ? moduleConfig.audio.i2s_sd : I2S_PIN_NO_CHANGE};
            res = i2s_set_pin(I2S_PORT, &pin_config);
            if (res != ESP_OK) {
                LOG_ERROR("Failed to set I2S pin config: %d", res);
            }

            res = i2s_start(I2S_PORT);
            if (res != ESP_OK) {
                LOG_ERROR("Failed to start I2S: %d", res);
            }

            radio_state = RadioState::rx;

            // Configure PTT input
            LOG_INFO("Init PTT on Pin %u", moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN);
            pinMode(moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN, INPUT);

            firstTime = false;
        } else {
            UIFrameEvent e;
            // Check if PTT is pressed. TODO hook that into Onebutton/Interrupt drive.
            if (digitalRead(moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN) == HIGH) {
                if (radio_state == RadioState::rx) {
                    LOG_INFO("PTT pressed, switching to TX");
                    radio_state = RadioState::tx;
                    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET; // We want to change the list of frames shown on-screen
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
                    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET; // We want to change the list of frames shown on-screen
                    this->notifyObservers(&e);
                }
            }
            if (radio_state == RadioState::tx) {
                // Get I2S data from the microphone and place in data buffer. adc_buffer_size is a
                // sample count; i2s_read and adc_buffer_index are in bytes, so scale by int16_t.
                size_t bytesIn = 0;
                const size_t frameBytes = adc_buffer_size * sizeof(uint16_t);
                res = i2s_read(I2S_PORT, (uint8_t *)adc_buffer + adc_buffer_index, frameBytes - adc_buffer_index, &bytesIn,
                               pdMS_TO_TICKS(40)); // wait 40ms for audio to arrive.

                if (res == ESP_OK) {
                    adc_buffer_index += bytesIn;
                    if (adc_buffer_index >= frameBytes) {
                        adc_buffer_index = 0;
                        memcpy((void *)speech, (void *)adc_buffer, frameBytes);
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
    if (!moduleConfig.audio.codec2_enabled || codec2 == nullptr) {
        return false;
    }
    return (radio_state == RadioState::tx);
}

void AudioModule::sendPayload(NodeNum dest, bool wantReplies)
{
    meshtastic_MeshPacket *p = allocReply();
    if (!p)
        return;
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
    if ((moduleConfig.audio.codec2_enabled) && (myRegion->profile->audioPermitted) && (codec2 != nullptr) &&
        (codec2HandlerTask != nullptr)) {
        auto &p = mp.decoded;
        if (!isFromUs(&mp)) {
            size_t n = p.payload.size;
            if (n > sizeof(rx_encode_frame)) // bound a crafted payload to the RX buffer
                n = sizeof(rx_encode_frame);
            memcpy(rx_encode_frame, p.payload.bytes, n);
            radio_state = RadioState::rx;
            rx_encode_frame_index = n;
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
