#include "configuration.h"
#if defined(HAS_AUDIO_CODEC2) && !MESHTASTIC_EXCLUDE_AUDIO
#include "AudioCodebooks.h"
#include "AudioModule.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "gps/RTC.h"

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
                for (int i = 0; i < audioModule->frame_samples; i++)
                    audioModule->speech[i] = (int16_t)hp_filter.Update((float)audioModule->speech[i]);

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
                        audioIOWrite(audioModule->output_buffer, audioModule->frame_samples, 500);
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
        // Must happen before codec2_create() when the tables are not in the image.
        const char *bindErr = audioCodebooksBind();
        if (bindErr) {
            LOG_ERROR("Audio disabled: %s", bindErr);
            disable();
            return;
        }

        // Never derive the codec2 mode arithmetically from the config value:
        // codec2 renumbered its modes and bitrate-1 is wrong for 450 only.
        const AudioModeInfo *m = audioModeFromConfig(moduleConfig.audio.bitrate);
        if (!m) {
            LOG_ERROR("Audio disabled: bitrate %u is not a codec2 mode", moduleConfig.audio.bitrate);
            disable();
            return;
        }

        LOG_INFO("Set up codec2 at %u bit/s (mode %d)", m->bitrate, m->codec2Mode);
        codec2 = codec2_create(m->codec2Mode);
        if (!codec2) {
            LOG_ERROR("Audio disabled: codec2 mode %d not built into this firmware", m->codec2Mode);
            disable();
            return;
        }
        memcpy(tx_header.magic, c2_magic, sizeof(c2_magic));
        tx_header.mode = m->codec2Mode;
        codec2_set_lpc_post_filter(codec2, 1, 0, 0.8, 0.2);
        encode_codec_size = (codec2_bits_per_frame(codec2) + 7) / 8;
        encode_frame_num = (meshtastic_Constants_DATA_PAYLOAD_LEN - sizeof(tx_header)) / encode_codec_size;
        encode_frame_size = encode_frame_num * encode_codec_size; // max 233 bytes + 4 header bytes
        frame_samples = codec2_samples_per_frame(codec2);
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
    // Show the rate actually in use, which may be below the configured one.
    const AudioModeInfo *shown = audioModeFromConfig(moduleConfig.audio.bitrate);
    display->drawStringf(0 + x, 0 + y, buffer, "Codec2 %u bit/s", shown ? shown->bitrate : 0);
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
    if ((moduleConfig.audio.codec2_enabled) && (myRegion->profile->audioPermitted)) {
        if (firstTime) {
            if (!audioIOBegin(8000, frame_samples))
                return disable();

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
                // Accumulate until a whole codec2 frame is present; a read can be short.
                mic_buffer_index +=
                    (int)audioIORead(mic_buffer + mic_buffer_index, (size_t)(frame_samples - mic_buffer_index), 40);
                if (mic_buffer_index >= frame_samples) {
                    mic_buffer_index = 0;
                    memcpy(speech, mic_buffer, (size_t)frame_samples * sizeof(int16_t));
                    // Notify run_codec2 task that the buffer is ready.
                    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                    vTaskNotifyGiveFromISR(codec2HandlerTask, &xHigherPriorityTaskWoken);
                    if (xHigherPriorityTaskWoken == pdTRUE)
                        YIELD_FROM_ISR(xHigherPriorityTaskWoken);
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
    if ((moduleConfig.audio.codec2_enabled) && (myRegion->profile->audioPermitted)) {
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
