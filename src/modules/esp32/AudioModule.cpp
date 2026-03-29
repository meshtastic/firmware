#include "configuration.h"
#if defined(ARCH_ESP32) && defined(USE_SX1280)
#include "AudioModule.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "graphics/Screen.h"
#ifdef HAS_I2S
#include "main.h" // for audioThread
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

// Runtime gain helpers — read once from config, cached in local vars
static int getMicGain()
{
    uint8_t g = moduleConfig.audio.mic_gain;
    return (g > 0 && g <= 30) ? g : AUDIO_DEFAULT_GAIN;
}

static int getSpeakerGain()
{
    uint8_t g = moduleConfig.audio.speaker_gain;
    return (g > 0 && g <= 30) ? g : AUDIO_DEFAULT_GAIN;
}

TaskHandle_t codec2HandlerTask;
TaskHandle_t speakerTaskHandle;
AudioModule *audioModule;

#ifdef ARCH_ESP32
// ESP32 doesn't use that flag
#define YIELD_FROM_ISR(x) portYIELD_FROM_ISR()
#else
#define YIELD_FROM_ISR(x) portYIELD_FROM_ISR(x)
#endif

#include "graphics/ScreenFonts.h"

#ifdef AUDIO_I2S_DUAL
void AudioModule::reinstallSpeakerI2S()
{
    // AudioOutputI2S::stop() calls i2s_driver_uninstall(), so we must
    // reinstall the driver before codec2 can write decoded audio.
    i2s_config_t spk_config = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
                               .sample_rate = 8000,
                               .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
                               .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
                               .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
                               .intr_alloc_flags = 0,
                               .dma_buf_count = 16,
                               .dma_buf_len = adc_buffer_size,
                               .use_apll = false,
                               .tx_desc_auto_clear = true,
                               .fixed_mclk = 0};
    esp_err_t res = i2s_driver_install(I2S_PORT_SPK, &spk_config, 0, NULL);
    if (res == ESP_OK) {
        const i2s_pin_config_t spk_pins = {.mck_io_num = I2S_PIN_NO_CHANGE,
                                           .bck_io_num = AUDIO_I2S_SPK_SCK,
                                           .ws_io_num = AUDIO_I2S_SPK_WS,
                                           .data_out_num = AUDIO_I2S_SPK_DIN,
                                           .data_in_num = I2S_PIN_NO_CHANGE};
        i2s_set_pin(I2S_PORT_SPK, &spk_pins);
        i2s_start(I2S_PORT_SPK);
        LOG_INFO("Reinstalled speaker I2S driver after AudioThread teardown");
    } else if (res == ESP_ERR_INVALID_STATE) {
        // Driver still installed (RTTTL never played or stop() wasn't called) — just reset rate
        i2s_set_sample_rates(I2S_PORT_SPK, 8000);
    } else {
        LOG_ERROR("Failed to reinstall speaker I2S driver: %d", res);
    }
}
#endif

// --- Shared ring buffer helpers (TX mic and RX jitter use the same buffer) ---

uint32_t AudioModule::ringAvailable()
{
    uint32_t h = ring_head;
    uint32_t t = ring_tail;
    return (h - t) & RING_BUF_MASK;
}

void AudioModule::ringWrite(const int16_t *data, int count)
{
    for (int i = 0; i < count; i++) {
        uint32_t nextHead = (ring_head + 1) & RING_BUF_MASK;
        if (nextHead == ring_tail) {
            // Buffer full — drop remaining samples (never modify ring_tail from writer;
            // only the reader may advance tail to keep this a safe SPSC buffer).
            uint32_t dropped = count - i;
            ring_drops += dropped;
            LOG_WARN("Ring buffer full, dropped %u samples (total drops: %u, avail: %u)",
                     dropped, ring_drops, ringAvailable());
            return;
        }
        ring_buf[ring_head] = data[i];
        ring_head = nextHead;
    }
}

int AudioModule::ringRead(int16_t *data, int maxCount)
{
    int count = 0;
    while (count < maxCount && ring_tail != ring_head) {
        data[count++] = ring_buf[ring_tail];
        ring_tail = (ring_tail + 1) & RING_BUF_MASK;
    }
    return count;
}

void AudioModule::ringReset()
{
    ring_head = ring_tail = 0;
}

// === Speaker drain task ===
// Runs independently of codec2 decode at priority 19 on core 1.
// Continuously pulls decoded PCM from the ring buffer and writes to I2S.
// Uses portMAX_DELAY so it blocks on the I2S DMA semaphore and wakes
// the instant DMA space opens — natural hardware flow control.
static void speaker_task(void *parameter)
{
    while (audioModule == nullptr)
        vTaskDelay(pdMS_TO_TICKS(10));

    int16_t playBuf[ADC_BUFFER_SIZE_MAX];
    uint32_t lastDataTime = 0;

    LOG_INFO("Speaker drain task started");

    while (true) {
        // Wait for playback to become active
        if (!audioModule->rx_playback_active) {
            // Check if jitter buffer is filled enough to start
            if (audioModule->rx_draining && audioModule->ringAvailable() >= RX_JITTER_SAMPLES) {
                audioModule->rx_playback_active = true;
                lastDataTime = millis();
                LOG_INFO("RX jitter buffer filled (%u samples), starting playback", audioModule->ringAvailable());
            } else {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
        }

        // Drain one frame to I2S (blocks until DMA accepts it)
        if (audioModule->ringAvailable() >= (uint32_t)audioModule->adc_buffer_size) {
            audioModule->ringRead(playBuf, audioModule->adc_buffer_size);
            size_t bytesOut = 0;
            i2s_write(I2S_PORT_SPK, playBuf, audioModule->adc_buffer_size * sizeof(int16_t),
                      &bytesOut, portMAX_DELAY);
            lastDataTime = millis();
        } else {
            // Ring buffer empty — check for silence timeout
            if (millis() - lastDataTime > RX_SILENCE_TIMEOUT) {
                audioModule->rx_playback_active = false;
                audioModule->rx_draining = false;
                audioModule->i2s_reclaimed_for_codec2 = false;
                audioModule->rx_seq_initialized = false;
                LOG_INFO("RX playback stopped (buffer drained, no new data)");
            } else {
                // Brief sleep while waiting for more decoded data
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }
    }
}

void run_codec2(void *parameter)
{
    // Wait for the global audioModule pointer to be assigned.
    // The task is created inside the AudioModule constructor, but the global
    // pointer isn't set until after the constructor returns (audioModule = new AudioModule()).
    // On dual-core ESP32-S3 the task can start on core 1 immediately.
    while (audioModule == nullptr) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // 4 bytes of c2_header (magic + mode) then 1 byte sequence number
    memcpy(audioModule->tx_encode_frame, &audioModule->tx_header, sizeof(audioModule->tx_header));
    audioModule->tx_encode_frame[sizeof(c2_header)] = 0; // initial seq = 0

    LOG_INFO("Start codec2 task");

    while (true) {
        // Block until notified by handleReceived (new packet) or PTT press.
        // Short timeout during TX so we re-enter the mic read loop quickly
        // if we ever exit it (e.g. partial i2s_read). Long timeout during RX
        // since speaker_task handles playback independently.
        bool inTx = (audioModule->radio_state == RadioState::tx);
        uint32_t tcount = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(inTx ? 5 : 10000));

        // === TX path: read mic I2S directly, filter, encode, send ===
        // codec2 task runs at priority 1 (same as main loop) so that
        // xTaskNotifyGive from the PTT handler does NOT preempt — the main
        // loop finishes its iteration (including screen update) first.
        // i2s_read blocks on DMA, naturally yielding CPU between frames.
        if (audioModule->radio_state == RadioState::tx) {
            size_t bytesIn = 0;
            while (audioModule->radio_state == RadioState::tx) {
                // Read one full codec2 frame directly from mic I2S
                esp_err_t res = i2s_read(I2S_PORT_MIC, audioModule->speech,
                                         audioModule->adc_buffer_size * sizeof(int16_t),
                                         &bytesIn, pdMS_TO_TICKS(80));
                if (res != ESP_OK || (int)(bytesIn / sizeof(int16_t)) < audioModule->adc_buffer_size)
                    continue; // Partial read — try again (don't break out of TX loop!)

                for (int i = 0; i < audioModule->adc_buffer_size; i++) {
                    int32_t sample = (int32_t)hp_filter.Update((float)audioModule->speech[i]) * getMicGain();
                    if (sample > 32767) sample = 32767;
                    if (sample < -32768) sample = -32768;
                    audioModule->speech[i] = (int16_t)sample;
                }

                codec2_encode(audioModule->codec2, audioModule->tx_encode_frame + audioModule->tx_encode_frame_index,
                              audioModule->speech);
                audioModule->tx_encode_frame_index += audioModule->encode_codec_size;

                if (audioModule->tx_encode_frame_index == (audioModule->encode_frame_size + (int)AUDIO_HEADER_SIZE)) {
                    LOG_DEBUG("Send %d codec2 bytes (seq=%u)", audioModule->encode_frame_size, audioModule->tx_seq);
                    audioModule->tx_encode_frame[sizeof(c2_header)] = audioModule->tx_seq++;
                    NodeNum dest = moduleConfig.audio.audio_target ? moduleConfig.audio.audio_target : NODENUM_BROADCAST;
                    audioModule->sendPayload(dest);
                    audioModule->tx_encode_frame_index = AUDIO_HEADER_SIZE;
                    taskYIELD();
                }
            }
        }

        // === RX path: decode queued packets into ring buffer ===
        if (audioModule->radio_state != RadioState::tx && audioModule->rxPacketQueue) {
            AudioRxPacket pkt;
            while (xQueueReceive(audioModule->rxPacketQueue, &pkt, 0) == pdTRUE) {
                if (pkt.size < AUDIO_HEADER_SIZE)
                    continue;

                // Extract and check sequence number (byte after c2_header)
                uint8_t rxSeq = pkt.data[sizeof(c2_header)];
                if (!audioModule->rx_seq_initialized) {
                    audioModule->rx_seq_expected = rxSeq;
                    audioModule->rx_seq_initialized = true;
                }
                if (rxSeq != audioModule->rx_seq_expected) {
                    uint8_t gap = (uint8_t)(rxSeq - audioModule->rx_seq_expected);
                    LOG_WARN("RX audio seq gap: expected %u got %u (lost %u packets)",
                             audioModule->rx_seq_expected, rxSeq, gap);

                    // Packet Loss Concealment: insert silence for each missing packet
                    // so playback timing stays aligned. Without this, the ring buffer
                    // drains early and the listener hears a jarring click/pop.
                    // Cap at 3 packets to avoid flooding the ring buffer on session restart
                    // or large sequence jumps (e.g. wrap from 255 → 0 after a gap).
                    uint8_t plcGap = (gap > 3) ? 3 : gap;
                    memset(audioModule->output_buffer, 0, audioModule->adc_buffer_size * sizeof(int16_t));
                    for (uint8_t g = 0; g < plcGap; g++) {
                        for (int f = 0; f < audioModule->encode_frame_num; f++) {
                            audioModule->ringWrite(audioModule->output_buffer, audioModule->adc_buffer_size);
                        }
                    }
                    LOG_DEBUG("PLC: inserted %d samples of silence for %u missing packets (gap=%u)",
                              audioModule->encode_frame_num * audioModule->adc_buffer_size * plcGap, plcGap, gap);
                }
                audioModule->rx_seq_expected = rxSeq + 1;

                // Determine codec from packet header
                bool headerMatch = (memcmp(pkt.data, &audioModule->tx_header, sizeof(c2_header)) == 0);
                CODEC2 *dec_codec;
                int dec_frame_bytes, dec_samples;
                bool tmpCodec = false;

                if (headerMatch) {
                    dec_codec = audioModule->codec2;
                    dec_frame_bytes = audioModule->encode_codec_size;
                    dec_samples = audioModule->adc_buffer_size;
                } else if (memcmp(pkt.data, c2_magic, sizeof(c2_magic)) == 0) {
                    // Mismatched codec mode — create temporary decoder
                    dec_codec = codec2_create(pkt.data[3]);
                    codec2_set_lpc_post_filter(dec_codec, 1, 0, 0.8, 0.2);
                    dec_frame_bytes = (codec2_bits_per_frame(dec_codec) + 7) / 8;
                    dec_samples = codec2_samples_per_frame(dec_codec);
                    tmpCodec = true;
                } else {
                    continue; // Unknown header, skip
                }

                // Decode all frames in the packet into the ring buffer.
                // Speaker_task is higher priority and preempts us whenever DMA
                // needs data, so no yield needed here — just decode as fast
                // as possible to keep the ring buffer ahead of playback.
                for (int i = AUDIO_HEADER_SIZE; i + dec_frame_bytes <= (int)pkt.size; i += dec_frame_bytes) {
                    codec2_decode(dec_codec, audioModule->output_buffer, pkt.data + i);
                    for (int j = 0; j < dec_samples; j++) {
                        int32_t s = (int32_t)audioModule->output_buffer[j] * getSpeakerGain();
                        if (s > 32767) s = 32767;
                        if (s < -32768) s = -32768;
                        audioModule->output_buffer[j] = (int16_t)s;
                    }
                    audioModule->ringWrite(audioModule->output_buffer, dec_samples);
                    // Signal speaker to start as soon as first frame is decoded,
                    // not after the whole packet. Critical for slow codecs (700bps).
                    audioModule->rx_draining = true;
                }

                if (tmpCodec)
                    codec2_destroy(dec_codec);

                LOG_DEBUG("RX decoded seq=%u: %d frames, ring=%u/%d samples",
                          rxSeq, (int)(pkt.size - AUDIO_HEADER_SIZE) / dec_frame_bytes,
                          audioModule->ringAvailable(), RING_BUF_SAMPLES);
            }
        }

        // Nothing else to do here — speaker_task handles playback independently.
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
        adc_buffer_size = codec2_samples_per_frame(codec2);

        // Fill as much of the payload as possible for the slowest packet rate.
        // The radio frame limit is MAX_LORA_PAYLOAD_LEN(255) - MESHTASTIC_HEADER_LENGTH(16)
        // = 239 bytes for the protobuf-encoded Data struct. Protobuf adds ~6 bytes
        // overhead (field tags, varint lengths), so max audio payload ≈ 233 - 6 = 227.
        // We use 224 as a safe max for the codec data portion (leaves headroom).
        static const int MAX_AUDIO_DATA = 224;
        encode_frame_num = MAX_AUDIO_DATA / encode_codec_size;
        encode_frame_size = encode_frame_num * encode_codec_size;
        int frameDurationMs = (adc_buffer_size * 1000) / 8000;
        LOG_INFO("Use %d frames of %d bytes (%dms per packet, total %d+%d=%d bytes)",
                 encode_frame_num, encode_codec_size,
                 encode_frame_num * frameDurationMs,
                 encode_frame_size, (int)AUDIO_HEADER_SIZE,
                 encode_frame_size + (int)AUDIO_HEADER_SIZE);
        LOG_INFO("Audio gain: mic=%d speaker=%d, target=%s",
                 getMicGain(), getSpeakerGain(),
                 moduleConfig.audio.audio_target ? "DM" : "broadcast");
        // Speaker task: HIGHEST priority (22) on core 1.
        // Must never be starved — DMA underruns cause audible glitches.
        // Blocks on i2s_write(portMAX_DELAY) when DMA is full, freeing CPU.
        xTaskCreatePinnedToCore(&speaker_task, "speaker_task", 8192, NULL, 2, &speakerTaskHandle, 1);
        // Codec2 task: priority 20, core 1 — handles encode/decode only.
        // Lower than speaker so decode bursts never starve DMA refills.
        xTaskCreatePinnedToCore(&run_codec2, "codec2_task", 30000, NULL, 1, &codec2HandlerTask, 1);
        rxPacketQueue = xQueueCreate(16, sizeof(AudioRxPacket));
        if (!rxPacketQueue)
            LOG_ERROR("Failed to create RX audio packet queue");
    } else {
        disable();
    }
}

void AudioModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
#if HAS_SCREEN
    lastDrawMs = millis();

    // Suppress frame content while a sub-menu is pending or the overlay
    // banner is active.  Without this, "Receive" flashes for one frame
    // between the main menu closing and the sub-menu opening.
    if (pendingMenu != 0)
        return;
#endif
    char buffer[50];

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->fillRect(0 + x, 0 + y, x + display->getWidth(), y + FONT_HEIGHT_SMALL);
    display->setColor(BLACK);
    if (moduleConfig.audio.audio_target) {
        display->drawStringf(0 + x, 0 + y, buffer, "Audio DM !%08x",
                             moduleConfig.audio.audio_target);
    } else {
        display->drawStringf(0 + x, 0 + y, buffer, "Codec2 Mode %d Audio",
                             (moduleConfig.audio.bitrate ? moduleConfig.audio.bitrate : AUDIO_MODULE_MODE) - 1);
    }
    display->setColor(WHITE);
    display->setFont(FONT_LARGE);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    if (radio_state == RadioState::tx) {
        display->drawString(display->getWidth() / 2 + x, (display->getHeight() - FONT_HEIGHT_SMALL) / 2 + y, "PTT");
    } else {
        // Map numeric gain to human-readable label
        auto gainLabel = [](uint8_t g) -> const char * {
            if (g == 0)
                g = AUDIO_DEFAULT_GAIN;
            if (g <= 1)
                return "Quiet";
            if (g <= 4)
                return "Low";
            if (g <= 8)
                return "Default";
            if (g <= 16)
                return "High";
            if (g <= 24)
                return "Loud";
            return "Max";
        };

        display->setFont(FONT_SMALL);
        display->drawStringf(display->getWidth() / 2 + x, 16 + y, buffer, "Mic: %s   Spk: %s",
                             gainLabel(moduleConfig.audio.mic_gain), gainLabel(moduleConfig.audio.speaker_gain));

        // Target line
        if (moduleConfig.audio.audio_target) {
            meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(moduleConfig.audio.audio_target);
            const char *name = (node && node->user.short_name[0]) ? node->user.short_name : "????";
            display->drawStringf(display->getWidth() / 2 + x, 30 + y, buffer, "To: %.4s", name);
        } else {
            display->drawString(display->getWidth() / 2 + x, 30 + y, "To: Broadcast");
        }
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
                                       .dma_buf_count = 16,
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
                                       .dma_buf_count = 16,
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
            // AudioModule always installs the speaker I2S driver itself.
            // When HAS_I2S is defined, AudioThread (ESP8266Audio) will later try to
            // i2s_driver_install in its begin() — that call will harmlessly fail
            // (ESP_ERR_INVALID_STATE) because the driver is already installed.
            // AudioThread then re-uses this driver and adjusts sample rate as needed.
            i2s_config_t spk_config = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
                                       .sample_rate = 8000,
                                       .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
                                       .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
                                       .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
                                       .intr_alloc_flags = 0,
                                       .dma_buf_count = 16,
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
                                       .dma_buf_count = 16,
                                       .dma_buf_len = adc_buffer_size,
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
#if HAS_SCREEN
            this->inputObserver.observe(inputBroker);
#endif
        } else {
#if HAS_SCREEN
            // Process pending sub-menu from previous banner callback
            if (pendingMenu != 0 && screen) {
                uint8_t menu = pendingMenu;
                pendingMenu = 0;
                switch (menu) {
                case 1:
                    showGainMenu(true);
                    break;
                case 2:
                    showGainMenu(false);
                    break;
                case 3:
                    showTargetMenu();
                    break;
                }
            }
#endif
            // Check if PTT is pressed (active LOW). TODO hook that into Onebutton/Interrupt drive.
            if (digitalRead(moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN) == LOW) {
                if (radio_state == RadioState::rx) {
                    LOG_INFO("PTT pressed, switching to TX");
                    powerFSM.trigger(EVENT_INPUT); // Wake screen on PTT press
                    radio_state = RadioState::tx;
                    // Update screen FIRST — show "PTT" before any blocking I/O
                    requestFocus();
                    UIFrameEvent e;
                    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                    this->notifyObservers(&e);
                    // Reset audio state for new TX session
                    rx_playback_active = false;
                    rx_draining = false;
                    i2s_reclaimed_for_codec2 = false;
                    rx_seq_initialized = false;
                    tx_seq = 0;
                    ring_drops = 0;
                    ringReset();
                    adc_buffer_index = 0;
                    if (rxPacketQueue)
                        xQueueReset(rxPacketQueue);
                    // Wake the codec2 task — it may be sleeping for up to 10s
                    xTaskNotifyGive(codec2HandlerTask);
                }
            } else {
                if (radio_state == RadioState::tx) {
                    LOG_INFO("PTT released, switching to RX");
                    if (tx_encode_frame_index > (int)AUDIO_HEADER_SIZE) {
                        // Send the incomplete frame
                        LOG_DEBUG("Send %d codec2 bytes (incomplete, seq=%u)", tx_encode_frame_index, tx_seq);
                        tx_encode_frame[sizeof(c2_header)] = tx_seq++;
                        NodeNum dest = moduleConfig.audio.audio_target ? moduleConfig.audio.audio_target : NODENUM_BROADCAST;
                        sendPayload(dest);
                    }
                    tx_encode_frame_index = AUDIO_HEADER_SIZE;
                    // Flush stale RX audio that may have arrived during TX
                    if (rxPacketQueue)
                        xQueueReset(rxPacketQueue);
                    ringReset();
                    adc_buffer_index = 0;
                    rx_playback_active = false;
                    rx_draining = false;
                    i2s_reclaimed_for_codec2 = false;
                    rx_seq_initialized = false; // reset for next RX session
                    ring_drops = 0; // reset drop counter
                    radio_state = RadioState::rx;
                    UIFrameEvent e;
                    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET_BACKGROUND;
                    this->notifyObservers(&e);
                }
            }
            if (radio_state == RadioState::tx) {
                // Mic reading is handled by the codec2 task directly.
                // Nothing to do here — just let runOnce cycle for PTT polling.
            }
        }
        return 50;
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
#ifdef AUDIO_I2S_DUAL
            // Stop any RTTTL/TTS playback and reclaim the speaker I2S port,
            // but only ONCE per audio session.  Doing this on every packet
            // would call i2s_driver_uninstall() and destroy all DMA-buffered
            // audio that is still playing back.
            if (!i2s_reclaimed_for_codec2 && audioThread) {
                audioThread->stop();
                reinstallSpeakerI2S();
                i2s_reclaimed_for_codec2 = true;
            }
#endif
            // Queue the packet for the codec2 task to decode into the jitter buffer.
            // This avoids blocking the mesh thread and prevents data loss if
            // multiple packets arrive before the previous one is decoded.
            if (rxPacketQueue) {
                AudioRxPacket pkt;
                memcpy(pkt.data, p.payload.bytes, p.payload.size);
                pkt.size = p.payload.size;
                if (xQueueSend(rxPacketQueue, &pkt, 0) != pdTRUE) {
                    LOG_WARN("RX audio packet queue full, dropped packet");
                }
                // Wake codec2 task to process the queued packet
                xTaskNotifyGive(codec2HandlerTask);
            }
            radio_state = RadioState::rx;
        }
    }

    return ProcessMessage::CONTINUE;
}

// --- On-screen audio settings menus (long-press SELECT on audio frame) ---

#if HAS_SCREEN

int AudioModule::handleInputEvent(const InputEvent *event)
{
    if (event->inputEvent != INPUT_BROKER_SELECT)
        return 0;

    // If a banner callback just fired (e.g. user picked an option), suppress
    // this SELECT so we don't immediately reopen the main menu.
    if (suppressNextSelect) {
        suppressNextSelect = false;
        return 0;
    }

    // Only act when the audio frame was drawn recently (i.e. it's visible).
    // Use 1000ms to cover the 500ms long-press hold time plus margin for
    // auto-frame-advance that may stop drawFrame calls mid-press.
    if ((millis() - lastDrawMs) > 1000)
        return 0;

    // Don't intercept while a banner overlay is already showing
    if (screen && screen->isOverlayBannerShowing())
        return 0;

    // Don't open menu during active TX (PTT held)
    if (radio_state == RadioState::tx)
        return 0;

    showAudioMenu();
    return 1; // consumed — stop observer chain
}

void AudioModule::showAudioMenu()
{
    if (!screen)
        return;
    static const char *opts[] = {"Back", "Mic Gain", "Speaker Gain", "Target"};
    graphics::BannerOverlayOptions banner;
    banner.message = "Audio Settings";
    banner.optionsArrayPtr = opts;
    banner.optionsCount = 4;
    banner.bannerCallback = [](int selected) {
        if (!audioModule)
            return;
        audioModule->suppressNextSelect = true;
        switch (selected) {
        case 1:
            audioModule->pendingMenu = 1;
            break; // mic gain
        case 2:
            audioModule->pendingMenu = 2;
            break; // speaker gain
        case 3:
            audioModule->pendingMenu = 3;
            break; // target
        default:
            break; // Back
        }
    };
    screen->showOverlayBanner(banner);
}

void AudioModule::showGainMenu(bool isMic)
{
    if (!screen)
        return;

    static const char *micOpts[] = {"Back", "1 Quiet", "4 Low", "8 Default", "16 High", "24 Loud", "30 Max"};
    static const char *spkOpts[] = {"Back", "1 Quiet", "4 Low", "8 Default", "16 High", "24 Loud", "30 Max"};
    static const uint8_t gainValues[] = {0, 1, 4, 8, 16, 24, 30};

    graphics::BannerOverlayOptions banner;
    banner.message = isMic ? "Mic Gain" : "Speaker Gain";
    banner.optionsArrayPtr = isMic ? micOpts : spkOpts;
    banner.optionsCount = 7;
    banner.bannerCallback = [isMic](int selected) {
        if (!audioModule)
            return;
        audioModule->suppressNextSelect = true;
        if (selected == 0)
            return; // Back
        uint8_t gain = gainValues[selected];
        if (isMic)
            moduleConfig.audio.mic_gain = gain;
        else
            moduleConfig.audio.speaker_gain = gain;
        nodeDB->saveToDisk(SEGMENT_MODULECONFIG);
        LOG_INFO("Audio %s gain set to %u", isMic ? "mic" : "speaker", gain);
    };

    // Pre-select the current value
    uint8_t current = isMic ? moduleConfig.audio.mic_gain : moduleConfig.audio.speaker_gain;
    if (current == 0)
        current = AUDIO_DEFAULT_GAIN;
    for (int i = 1; i < 7; i++) {
        if (gainValues[i] >= current) {
            banner.InitialSelected = i;
            break;
        }
    }

    screen->showOverlayBanner(banner);
}

void AudioModule::showTargetMenu()
{
    if (!screen)
        return;

    // Build a dynamic list: Back, Broadcast, then up to 5 known nodes
    static char nodeLabels[8][20];
    static const char *opts[8];
    static uint32_t nodeNums[8];
    int idx = 0;

    strncpy(nodeLabels[idx], "Back", sizeof(nodeLabels[0]));
    opts[idx] = nodeLabels[idx];
    nodeNums[idx] = 0;
    idx++;

    strncpy(nodeLabels[idx], "Broadcast", sizeof(nodeLabels[0]));
    opts[idx] = nodeLabels[idx];
    nodeNums[idx] = 0;
    idx++;

    // Add up to 5 known nodes (excluding self)
    for (int i = 0; i < nodeDB->getNumMeshNodes() && idx < 7; i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (!node || node->num == nodeDB->getNodeNum())
            continue;
        const char *shortName = node->user.short_name[0] ? node->user.short_name : "????";
        snprintf(nodeLabels[idx], sizeof(nodeLabels[0]), "%.4s !%04x", shortName, (uint16_t)(node->num & 0xFFFF));
        nodeNums[idx] = node->num;
        opts[idx] = nodeLabels[idx];
        idx++;
    }

    graphics::BannerOverlayOptions banner;
    banner.message = moduleConfig.audio.audio_target ? "Target: DM" : "Target: Bcast";
    banner.optionsArrayPtr = opts;
    banner.optionsCount = idx;
    banner.bannerCallback = [](int selected) {
        if (!audioModule)
            return;
        audioModule->suppressNextSelect = true;
        if (selected == 0)
            return; // Back
        moduleConfig.audio.audio_target = nodeNums[selected]; // 0 for Broadcast, nodeNum for DM
        nodeDB->saveToDisk(SEGMENT_MODULECONFIG);
        LOG_INFO("Audio target set to %s (0x%08x)",
                 moduleConfig.audio.audio_target ? "DM" : "broadcast",
                 moduleConfig.audio.audio_target);
    };

    // If a DM target is set, pre-select it in the list
    if (moduleConfig.audio.audio_target) {
        for (int i = 2; i < idx; i++) {
            if (nodeNums[i] == moduleConfig.audio.audio_target) {
                banner.InitialSelected = i;
                break;
            }
        }
    }

    screen->showOverlayBanner(banner);
}

#endif // HAS_SCREEN

#endif