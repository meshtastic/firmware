#pragma once

#include "configuration.h"

#if defined(ARCH_ESP32) && defined(HAS_I2S)

#include <ESP32I2SAudio.h>

/**
 * BackgroundAudio's ESP32I2SAudio, corrected for Meshtastic's hardware and driven
 * synchronously from AudioThread's feeder task (or the main loop as fallback) instead
 * of from upstream's IRQ-notified background task.
 *
 * Three things upstream gets wrong for this firmware, none of which has a public
 * API workaround - hence the subclass:
 *
 *  1. Frame format. Upstream uses I2S_STD_MSB_SLOT_DEFAULT_CONFIG (bit_shift = false,
 *     MSB/left-justified). ESP8266Audio's AudioOutputI2S, which this replaces, forced
 *     Philips (bit_shift = true). That one-BCLK shift corrupts the sign bit into the
 *     ES8311 codecs (tlora-pager, cardputer, meshnology-w10) and the MAX98357A-class
 *     amps (t-deck, t-watch-s3, dreamcatcher) - loud distortion on every board.
 *
 *  2. Stale DMA replay. Upstream leaves auto_clear false, and its _silenceSample is
 *     dead code on ESP32, so once the producer stops the circular DMA re-transmits its
 *     last contents forever. Four of the six HAS_I2S boards have no amp enable, so the
 *     speaker is permanently connected and that is a continuous audible buzz.
 *
 *  3. Port selection. Upstream passes I2S_NUM_AUTO. The path this replaces pinned port
 *     1 (`AudioOutputI2S(1, EXTERNAL_I2S)`), and on T-LoRa Pager the codec2 AudioModule
 *     claims port 0 through the legacy driver (see src/modules/esp32/AudioModule.h,
 *     `#define I2S_PORT I2S_NUM_0`). AUTO would take port 0 when it is free and then
 *     codec2's i2s_driver_install fails. Do not "simplify" this back to AUTO.
 *
 * It also declines to start upstream's priority-2 FreeRTOS task. AudioThread runs its
 * own feeder task, but that is not the same thing: upstream's task exists to maintain
 * availableForWrite() from ISR notifications, which is the machinery this subclass
 * avoids - our feeder uses the same ask-the-IDF-directly zero-timeout write. That
 * matters for correctness, not just simplicity - see writeFrames().
 */
class MeshtasticI2SOut : public ESP32I2SAudio
{
  public:
    // 16 buffers x 256 frames = 4096 frames = 16KB = 185ms at 22050Hz. The ring is the
    // only slack between the cooperative main loop and an audible dropout: pump() runs
    // from OSThread ticks, and a 320x240 TFT frame push under spiLock is 30-60ms,
    // transitions run those back-to-back at 30fps, and a screen wake can hog the loop
    // for ~200ms - the ESP8266Audio-era 8 x 128 ring (46ms) underran on all of those,
    // which was audible as stuttering exactly when a notification redraws the screen.
    // The DMA RAM is only held while a melody is playing, and startup latency does not
    // scale with the depth: AudioThread preloads the ring with real samples via
    // preloadFrames() before start(), so the first note is audible after the short amp
    // lead-in rather than after a ring's worth of silence.
    static constexpr size_t kDmaBuffers = 16;
    static constexpr size_t kDmaFrames = 256;

    // The ESP8266Audio-era geometry, kept as a fallback when internal DMA-capable RAM
    // is too fragmented at playback time for the full ring: degraded jitter tolerance
    // beats no audio at all.
    static constexpr size_t kDmaBuffersFallback = 8;
    static constexpr size_t kDmaFramesFallback = 128;

    MeshtasticI2SOut(int8_t bclk, int8_t ws, int8_t dout, int8_t mclk) : ESP32I2SAudio(bclk, ws, dout, mclk)
    {
        // The base constructor leaves _tx_handle uninitialized.
        _tx_handle = nullptr;
        setBuffers(kDmaBuffers, kDmaFrames);
    }

    virtual ~MeshtasticI2SOut() { end(); }

    /**
     * Allocate and initialize the I2S channel, leaving it disabled so the caller can
     * preload the DMA ring with real samples (preloadFrames()) before start() clocks it
     * out. Reimplements ESP32I2SAudio::begin() rather than delegating to it: upstream
     * wraps four ESP-IDF calls in assert(), and nothing in the PlatformIO build defines
     * NDEBUG (the sdkconfig CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_DISABLE only reaches
     * the IDF component build), so those are live abort() calls on a device with no
     * console.
     */
    bool begin() override
    {
        if (_running || _tx_handle)
            return false;

        i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
        chanCfg.dma_desc_num = _buffers;
        chanCfg.dma_frame_num = _bufferWords;
        chanCfg.auto_clear = true; // send zeros, not the last buffer, when we stop feeding

        esp_err_t err = i2s_new_channel(&chanCfg, &_tx_handle, nullptr);
        if (err == ESP_ERR_NO_MEM && (_buffers != kDmaBuffersFallback || _bufferWords != kDmaFramesFallback)) {
            LOG_WARN("I2S: no DMA memory for %ux%u ring, falling back to %ux%u", (unsigned)_buffers, (unsigned)_bufferWords,
                     (unsigned)kDmaBuffersFallback, (unsigned)kDmaFramesFallback);
            _buffers = kDmaBuffersFallback;
            _bufferWords = kDmaFramesFallback;
            chanCfg.dma_desc_num = _buffers;
            chanCfg.dma_frame_num = _bufferWords;
            _tx_handle = nullptr;
            err = i2s_new_channel(&chanCfg, &_tx_handle, nullptr);
        }
        if (err != ESP_OK) {
            LOG_ERROR("I2S: i2s_new_channel failed: %d", err);
            _tx_handle = nullptr;
            return false;
        }

        i2s_std_config_t stdCfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)_sampleRate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg =
                {
                    .mclk = _mclk < 0 ? I2S_GPIO_UNUSED : (gpio_num_t)_mclk,
                    .bclk = (gpio_num_t)_bclk,
                    .ws = (gpio_num_t)_ws,
                    .dout = (gpio_num_t)_dout,
                    .din = I2S_GPIO_UNUSED,
                    .invert_flags =
                        {
                            .mclk_inv = _mclkInv,
                            .bclk_inv = _bclkInv,
                            .ws_inv = _wsInv,
                        },
                },
        };
        err = i2s_channel_init_std_mode(_tx_handle, &stdCfg);
        if (err != ESP_OK) {
            LOG_ERROR("I2S: i2s_channel_init_std_mode failed: %d", err);
            releaseChannel();
            return false;
        }

        // Upstream asserts the IDF honoured the requested geometry. Warn instead: a
        // mismatch costs us some drain-time accuracy, not correctness.
        i2s_chan_info_t info;
        _totalAvailable = _buffers * _bufferWords * 4;
        if (i2s_channel_get_info(_tx_handle, &info) == ESP_OK) {
            if (info.total_dma_buf_size != _totalAvailable)
                LOG_WARN("I2S: IDF gave %u DMA bytes, asked for %u", (unsigned)info.total_dma_buf_size,
                         (unsigned)_totalAvailable);
            _totalAvailable = info.total_dma_buf_size;
        }

        return true;
    }

    /**
     * Load frames into the still-disabled DMA ring, between begin() and start(). Returns
     * the number of frames accepted; zero once the ring is full (callers carry the
     * remainder into their first writeFrames()). Starting playback from a preloaded ring
     * means the first note is audible almost immediately AND the ring starts at maximum
     * depth, so a main-loop stall right at notification time - the common case, since the
     * same event kicks off the screen redraw - cannot underrun it.
     */
    size_t preloadFrames(const int16_t *interleavedLR, size_t frames)
    {
        if (_running || !_tx_handle || !interleavedLR || !frames)
            return 0;

        const uint8_t *buffer = (const uint8_t *)interleavedLR;
        size_t remaining = frames * kBytesPerFrame;
        size_t total = 0;

        // Like i2s_channel_write, preload stops at a descriptor boundary; loop until it
        // makes no further progress. Descriptors are frame-aligned so 'loaded' is too.
        while (remaining) {
            size_t loaded = 0;
            if (i2s_channel_preload_data(_tx_handle, (void *)buffer, remaining, &loaded) != ESP_OK || !loaded)
                break;
            buffer += loaded;
            remaining -= loaded;
            total += loaded;
        }
        return total / kBytesPerFrame;
    }

    /**
     * Enable the channel after begin()/preloadFrames(). Whatever the caller did not
     * preload is padded with silence first, so enabling never clocks out uninitialized
     * DMA (the tail of a short melody, or the whole ring if nothing was preloaded).
     */
    bool start()
    {
        if (_running || !_tx_handle)
            return false;

        int16_t silence[2] = {0, 0};
        size_t loaded = 0;
        do {
            if (i2s_channel_preload_data(_tx_handle, silence, sizeof(silence), &loaded) != ESP_OK)
                break;
        } while (loaded);

        esp_err_t err = i2s_channel_enable(_tx_handle);
        if (err != ESP_OK) {
            LOG_ERROR("I2S: i2s_channel_enable failed: %d", err);
            releaseChannel();
            return false;
        }

        _running = true;
        return true;
    }

    /**
     * Stop and release the channel. Must not delegate to ESP32I2SAudio::end(), which
     * calls vTaskDelete(_taskHandle) - and since we never start that task _taskHandle
     * is still 0, so the base implementation would delete the *calling* task.
     */
    bool end() override
    {
        if (_running || _tx_handle) {
            if (_running)
                i2s_channel_disable(_tx_handle);
            releaseChannel();
        }
        return true;
    }

    /**
     * Hand interleaved 16-bit stereo frames to the DMA. Never blocks; returns the number
     * of frames accepted, which may be fewer than asked (or zero when the ring is full).
     * Callers must carry the remainder to the next pump tick.
     *
     * This deliberately does not consult availableForWrite(). That counter is maintained
     * only by upstream's background task, fed from an ISR that posts with
     * eSetValueWithoutOverwrite - which silently discards the value whenever one is
     * already pending. Under a display or flash stall it therefore under-reports free
     * space by whole buffers, with no diagnostic, exactly when the ring must not starve.
     * A zero-timeout write asks the IDF directly, which is also what ESP8266Audio did.
     */
    size_t writeFrames(const int16_t *interleavedLR, size_t frames)
    {
        if (!_running || !_tx_handle || !interleavedLR || !frames)
            return 0;

        const uint8_t *buffer = (const uint8_t *)interleavedLR;
        size_t remaining = frames * kBytesPerFrame;
        size_t total = 0;

        // i2s_channel_write stops at a DMA buffer boundary even when later buffers are
        // free, so loop until it makes no further progress.
        while (remaining) {
            size_t written = 0;
            if (i2s_channel_write(_tx_handle, buffer, remaining, &written, 0) != ESP_OK && !written)
                break;
            if (!written)
                break;
            buffer += written;
            remaining -= written;
            total += written;
        }

        // Keep the base class's counter roughly honest for anything that reads it.
        if (total)
            _saturating_sub_available((uint32_t)total);

        return total / kBytesPerFrame;
    }

    /// Milliseconds of audio a full DMA ring holds - how long to keep the amp on after
    /// the last sample is queued.
    uint32_t dmaDrainMs() const
    {
        const size_t rate = _sampleRate ? _sampleRate : 22050;
        return (uint32_t)((_totalAvailable ? _totalAvailable / kBytesPerFrame : kDmaBuffers * kDmaFrames) * 1000 / rate);
    }

  private:
    static constexpr size_t kBytesPerFrame = 4; // 16-bit stereo

    /// Delete the channel and reset the state upstream forgets to, so a later begin()
    /// starts clean. Upstream never nulls _tx_handle and never resets these counters,
    /// which is why re-begin() misbehaves there.
    void releaseChannel()
    {
        if (_tx_handle) {
            i2s_del_channel(_tx_handle);
            _tx_handle = nullptr;
        }
        _running = false;
        _available.store(0, std::memory_order_release);
        _underflows.store(0, std::memory_order_release);
        _underflowed.store(false, std::memory_order_release);
        _totalAvailable = 0;
        _frames = 0;
        _irqs = 0;
        // If this playback ran on the fallback geometry, let the next one try the full
        // ring again - the DMA RAM shortage may have been transient.
        _buffers = kDmaBuffers;
        _bufferWords = kDmaFrames;
    }
};

#endif // ARCH_ESP32 && HAS_I2S
