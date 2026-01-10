#include "configuration.h"

#if defined(ARCH_ESP32) && defined(HAS_I2S) && !MESHTASTIC_EXCLUDE_VOICEMEMO

#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "VoiceMemoModule.h"
#include "graphics/ScreenFonts.h"
#include "input/InputBroker.h"
#include "main.h"
#include "mesh/generated/meshtastic/module_config.pb.h"

#include <Wire.h>
#include <driver/gpio.h>
#include <soc/gpio_periph.h>

// XL9555 I/O expander for amplifier control on T-Lora Pager
#ifdef USE_XL9555
#include "ExtensionIOXL9555.hpp"
extern ExtensionIOXL9555 io;
#endif

// ES7210 I2C address (AD1=0, AD0=0)
#define ES7210_ADDR 0x40

// ES7210 Register definitions
#define ES7210_RESET_REG00 0x00
#define ES7210_CLOCK_OFF_REG01 0x01
#define ES7210_MAINCLK_REG02 0x02
#define ES7210_MASTER_CLK_REG03 0x03
#define ES7210_LRCK_DIVH_REG04 0x04
#define ES7210_LRCK_DIVL_REG05 0x05
#define ES7210_POWER_DOWN_REG06 0x06
#define ES7210_OSR_REG07 0x07
#define ES7210_MODE_CONFIG_REG08 0x08
#define ES7210_TIME_CONTROL0_REG09 0x09
#define ES7210_TIME_CONTROL1_REG0A 0x0A
#define ES7210_SDP_INTERFACE1_REG11 0x11
#define ES7210_SDP_INTERFACE2_REG12 0x12
#define ES7210_ADC34_HPF2_REG20 0x20
#define ES7210_ADC34_HPF1_REG21 0x21
#define ES7210_ADC12_HPF1_REG22 0x22
#define ES7210_ADC12_HPF2_REG23 0x23
#define ES7210_ANALOG_REG40 0x40
#define ES7210_MIC12_BIAS_REG41 0x41
#define ES7210_MIC34_BIAS_REG42 0x42
#define ES7210_MIC1_GAIN_REG43 0x43
#define ES7210_MIC2_GAIN_REG44 0x44
#define ES7210_MIC3_GAIN_REG45 0x45
#define ES7210_MIC4_GAIN_REG46 0x46
#define ES7210_MIC1_POWER_REG47 0x47
#define ES7210_MIC2_POWER_REG48 0x48
#define ES7210_MIC3_POWER_REG49 0x49
#define ES7210_MIC4_POWER_REG4A 0x4A
#define ES7210_MIC12_POWER_REG4B 0x4B
#define ES7210_MIC34_POWER_REG4C 0x4C

VoiceMemoModule *voiceMemoModule = nullptr;

// Forward declaration of codec2 task function
static void run_voicememo_codec2(void *parameter);
static void run_voicememo_playback(void *parameter);

/*
    VoiceMemoModule - Short voice message recording and playback

    This module allows users to record short voice memos using Shift+Space,
    which are then encoded with Codec2 and sent over the mesh with zero hops.

    Usage:
    1. Hold Shift+Space to start recording
    2. Release to stop and send the memo
    3. Recipients see a notification and can long-press to play

    Hardware Requirements:
    - I2S microphone (for recording)
    - I2S speaker/DAC (for playback)
    - T-Deck or T-Lora Pager hardware
*/

VoiceMemoModule::VoiceMemoModule()
    : SinglePortModule("VoiceMemo", meshtastic_PortNum_AUDIO_APP), concurrency::OSThread("VoiceMemo")
{
    // Initialize stored memos
    memset(storedMemos, 0, sizeof(storedMemos));
    storedMemoCount = 0;

    // Create Butterworth high-pass filter for audio cleanup (removes DC offset and low freq noise)
    hpFilter = new ButterworthFilter(240, 8000, ButterworthFilter::Highpass, 1);

    // Initialize Codec2
    codec2 = codec2_create(VOICEMEMO_CODEC2_MODE);
    if (codec2) {
        codec2_set_lpc_post_filter(codec2, 1, 0, 0.8, 0.2);
        encodeCodecSize = (codec2_bits_per_frame(codec2) + 7) / 8;
        adcBufferSize = codec2_samples_per_frame(codec2);
        LOG_INFO("VoiceMemo: Codec2 initialized, frame size=%d bytes, samples=%d", encodeCodecSize, adcBufferSize);

        // Create dedicated task for Codec2 encoding (needs 30KB stack for DSP operations)
        xTaskCreate(&run_voicememo_codec2, "voicememo_codec2", 30000, NULL, 5, &codec2TaskHandle);
        if (codec2TaskHandle) {
            LOG_INFO("VoiceMemo: Codec2 encode task created with 30KB stack");
        } else {
            LOG_ERROR("VoiceMemo: Failed to create Codec2 encode task");
            disable();
            return;
        }

        // Create dedicated task for Codec2 playback/decoding (also needs 30KB stack)
        xTaskCreate(&run_voicememo_playback, "voicememo_play", 30000, NULL, 5, &playbackTaskHandle);
        if (playbackTaskHandle) {
            LOG_INFO("VoiceMemo: Codec2 playback task created with 30KB stack");
        } else {
            LOG_ERROR("VoiceMemo: Failed to create Codec2 playback task");
            disable();
            return;
        }
    } else {
        LOG_ERROR("VoiceMemo: Failed to initialize Codec2");
        disable();
        return;
    }

    // Register for keyboard input events
    if (inputBroker) {
        inputObserver.observe(inputBroker);
    }

    LOG_INFO("VoiceMemo module initialized");
}

int VoiceMemoModule::handleInputEvent(const InputEvent *event)
{
    if (!event)
        return 0;

    // Debug: log all incoming keyboard events
    LOG_DEBUG("VoiceMemo: Received event=%d, kbchar=0x%02X (need 0x%02X)", event->inputEvent, event->kbchar,
              INPUT_BROKER_MSG_VOICEMEMO);

    // Detect voice memo key (mic key on T-Deck, Sym+V on T-Lora Pager)
    // Press to start recording, press again to stop
    if (event->kbchar == INPUT_BROKER_MSG_VOICEMEMO) {
        if (state == VoiceMemoState::IDLE) {
            LOG_INFO("VoiceMemo: Mic key pressed, starting recording");
            startRecording();
            return 1; // Consume the event
        } else if (state == VoiceMemoState::RECORDING) {
            LOG_INFO("VoiceMemo: Mic key pressed, stopping recording");
            stopRecording();
            return 1; // Consume the event
        } else if (state == VoiceMemoState::RECEIVING) {
            // Play the most recently received memo
            LOG_INFO("VoiceMemo: Key pressed in RECEIVING state, playing memo");
            playStoredMemo(0); // Play the first (most recent) memo
            return 1;
        }
    }

    // Enter key also plays when in RECEIVING state
    if (state == VoiceMemoState::RECEIVING && (event->kbchar == 0x0D || event->inputEvent == INPUT_BROKER_SELECT)) {
        LOG_INFO("VoiceMemo: Enter pressed, playing received memo");
        playStoredMemo(0);
        return 1;
    }

    // Escape/Back key dismisses the RECEIVING screen without playing
    if (state == VoiceMemoState::RECEIVING && (event->kbchar == 0x1B || event->inputEvent == INPUT_BROKER_CANCEL)) {
        LOG_INFO("VoiceMemo: Cancel pressed, dismissing");
        state = VoiceMemoState::IDLE;
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        return 1;
    }

    return 0; // Not consumed
}

// ES7210 I2C helper functions
#if defined(ES7210_SCK) && defined(ES7210_DIN) && defined(ES7210_LRCK)
static bool es7210_write_reg(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(ES7210_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static uint8_t es7210_read_reg(uint8_t reg)
{
    Wire.beginTransmission(ES7210_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(ES7210_ADDR, (uint8_t)1);
    return Wire.read();
}

static bool es7210_update_reg_bit(uint8_t reg, uint8_t mask, uint8_t val)
{
    uint8_t regv = es7210_read_reg(reg);
    regv = (regv & (~mask)) | (mask & val);
    return es7210_write_reg(reg, regv);
}
#endif

bool VoiceMemoModule::initES7210()
{
#if defined(ES7210_SCK) && defined(ES7210_DIN) && defined(ES7210_LRCK)
    LOG_INFO("VoiceMemo: Initializing ES7210 ADC via I2C...");

    // Check if ES7210 is present
    Wire.beginTransmission(ES7210_ADDR);
    if (Wire.endTransmission() != 0) {
        LOG_ERROR("VoiceMemo: ES7210 not found at I2C address 0x%02X", ES7210_ADDR);
        return false;
    }
    LOG_INFO("VoiceMemo: ES7210 found at I2C address 0x%02X", ES7210_ADDR);

    // Reset ES7210 (matching official T-Deck example init sequence)
    es7210_write_reg(ES7210_RESET_REG00, 0xFF);
    delay(10);
    es7210_write_reg(ES7210_RESET_REG00, 0x41);

    // Clock setup - disable clocks initially
    es7210_write_reg(ES7210_CLOCK_OFF_REG01, 0x1F);

    // Timing control for state cycles
    es7210_write_reg(ES7210_TIME_CONTROL0_REG09, 0x30);
    es7210_write_reg(ES7210_TIME_CONTROL1_REG0A, 0x30);

    // Analog configuration - VDDA 3.3V, VMID 5K start
    es7210_write_reg(ES7210_ANALOG_REG40, 0xC3);

    // Mic bias voltage 2.87V
    es7210_write_reg(ES7210_MIC12_BIAS_REG41, 0x70);
    es7210_write_reg(ES7210_MIC34_BIAS_REG42, 0x70);

    // OSR setting
    es7210_write_reg(ES7210_OSR_REG07, 0x20);

    // Main clock divider - set DLL, clear doubler
    es7210_write_reg(ES7210_MAINCLK_REG02, 0xC1);

    // Set slave mode (ESP32 is I2S master)
    es7210_update_reg_bit(ES7210_MODE_CONFIG_REG08, 0x01, 0x00);

    // SDP interface: I2S format, 16-bit
    es7210_write_reg(ES7210_SDP_INTERFACE1_REG11, 0x60); // 16-bit, I2S
    es7210_write_reg(ES7210_SDP_INTERFACE2_REG12, 0x00); // Normal mode (not TDM)

    // Configure for 8kHz sample rate with MCLK = 256 * 8000 = 2.048MHz
    // LRCK divider: MCLK/LRCK = 256, so LRCK_DIV = 0x0100
    es7210_write_reg(ES7210_LRCK_DIVH_REG04, 0x01);
    es7210_write_reg(ES7210_LRCK_DIVL_REG05, 0x00);

    // Power up MIC1 and MIC2 channels (disable all gains first)
    for (int i = 0; i < 4; i++) {
        es7210_update_reg_bit(ES7210_MIC1_GAIN_REG43 + i, 0x10, 0x00);
    }
    es7210_write_reg(ES7210_MIC12_POWER_REG4B, 0xFF); // Power off initially
    es7210_write_reg(ES7210_MIC34_POWER_REG4C, 0xFF);

    // Enable MIC1 clocks and power
    es7210_update_reg_bit(ES7210_CLOCK_OFF_REG01, 0x0B, 0x00); // Enable clocks
    es7210_write_reg(ES7210_MIC12_POWER_REG4B, 0x00);          // Power on MIC1/2
    es7210_update_reg_bit(ES7210_MIC1_GAIN_REG43, 0x10, 0x10); // Enable MIC1

    // Enable MIC2 as well (for better stereo capture, though we'll use mono)
    es7210_update_reg_bit(ES7210_MIC2_GAIN_REG44, 0x10, 0x10); // Enable MIC2

    // Set gain to 24dB (0x0C) for better signal level
    // ES7210 gain values: 0x00=0dB, 0x0C=24dB, 0x0D=37.5dB
    es7210_update_reg_bit(ES7210_MIC1_GAIN_REG43, 0x0F, 0x0C);
    es7210_update_reg_bit(ES7210_MIC2_GAIN_REG44, 0x0F, 0x0C);

    // Start ADC: power on and enable clocks
    es7210_write_reg(ES7210_CLOCK_OFF_REG01, 0x00);  // Enable all clocks
    es7210_write_reg(ES7210_POWER_DOWN_REG06, 0x00); // Power on ADC
    es7210_write_reg(ES7210_MIC1_POWER_REG47, 0x00); // Power on MIC1
    es7210_write_reg(ES7210_MIC2_POWER_REG48, 0x00); // Power on MIC2

    LOG_INFO("VoiceMemo: ES7210 initialized successfully (MIC1+MIC2 enabled, 24dB gain)");
    ;
    return true;
#else
    return false;
#endif
}

bool VoiceMemoModule::initI2S()
{
    if (i2sInitialized)
        return true;

// T-Deck uses ES7210 ADC for microphone on separate pins
#if defined(ES7210_SCK) && defined(ES7210_DIN) && defined(ES7210_LRCK)
    LOG_INFO("VoiceMemo: Initializing I2S for ES7210 microphone...");

    // Initialize ES7210 ADC codec via I2C first
    if (!initES7210()) {
        LOG_ERROR("VoiceMemo: Failed to initialize ES7210 ADC");
        return false;
    }

    // Note: MCLK pin is configured via i2s_set_pin with mck_io_num
    // The I2S driver will generate MCLK when mclk_multiple is set

    // I2S config matching T-Deck official Microphone example
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 8000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ALL_LEFT, // Use ALL_LEFT to get mono from MIC1
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64, // Match official example
        .use_apll = false, // Match official example
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,                        // Use mclk_multiple instead
        .mclk_multiple = I2S_MCLK_MULTIPLE_256, // MCLK = 256 * sample_rate
        .bits_per_chan = I2S_BITS_PER_CHAN_16BIT,
        .chan_mask = (i2s_channel_t)(I2S_TDM_ACTIVE_CH0 | I2S_TDM_ACTIVE_CH1), // MIC1 and MIC2 channels
        .total_chan = 2,
    };

    esp_err_t res = i2s_driver_install(VOICEMEMO_I2S_PORT, &i2s_config, 0, NULL);
    if (res != ESP_OK) {
        LOG_ERROR("VoiceMemo: Failed to install I2S driver: %d", res);
        return false;
    }

    i2s_pin_config_t pin_config = {
        .mck_io_num = ES7210_MCLK,
        .bck_io_num = ES7210_SCK,
        .ws_io_num = ES7210_LRCK,
        .data_out_num = I2S_PIN_NO_CHANGE, // We're only receiving (recording)
        .data_in_num = ES7210_DIN,
    };

    res = i2s_set_pin(VOICEMEMO_I2S_PORT, &pin_config);
    if (res != ESP_OK) {
        LOG_ERROR("VoiceMemo: Failed to set I2S pins: %d", res);
        i2s_driver_uninstall(VOICEMEMO_I2S_PORT);
        return false;
    }

    res = i2s_start(VOICEMEMO_I2S_PORT);
    if (res != ESP_OK) {
        LOG_ERROR("VoiceMemo: Failed to start I2S: %d", res);
        i2s_driver_uninstall(VOICEMEMO_I2S_PORT);
        return false;
    }

    // Allow I2S DMA to stabilize before reading
    delay(50);

    // Clear any stale data in the I2S DMA buffer
    i2s_zero_dma_buffer(VOICEMEMO_I2S_PORT);

    i2sInitialized = true;
    LOG_INFO("VoiceMemo: I2S initialized for ES7210 (SCK=%d, DIN=%d, LRCK=%d, MCLK=%d)", ES7210_SCK, ES7210_DIN, ES7210_LRCK,
             ES7210_MCLK);
    LOG_INFO("VoiceMemo: adcBufferSize=%d, VOICEMEMO_ADC_BUFFER_SIZE=%d", adcBufferSize, VOICEMEMO_ADC_BUFFER_SIZE);
    return true;

#elif defined(DAC_I2S_BCK) && defined(DAC_I2S_WS) && defined(DAC_I2S_DIN)
    // For devices with DAC pins (e.g., T-Lora Pager with ES8311)
    // Install our own legacy I2S driver for i2s_write() compatibility
    LOG_INFO("VoiceMemo: Initializing I2S for DAC pins...");

    // Use 44100Hz to match codec configuration
    // We'll upsample our 8kHz Codec2 output to 44.1kHz
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // Stereo for ES8311
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 256 * 44100,
    };

    esp_err_t res = i2s_driver_install(VOICEMEMO_I2S_PORT, &i2s_config, 0, NULL);
    if (res != ESP_OK) {
        LOG_ERROR("VoiceMemo: Failed to install I2S driver: %d", res);
        return false;
    }

    i2s_pin_config_t pin_config = {
        .mck_io_num = DAC_I2S_MCLK,
        .bck_io_num = DAC_I2S_BCK,
        .ws_io_num = DAC_I2S_WS,
        .data_out_num = DAC_I2S_DOUT,
        .data_in_num = DAC_I2S_DIN,
    };

    res = i2s_set_pin(VOICEMEMO_I2S_PORT, &pin_config);
    if (res != ESP_OK) {
        LOG_ERROR("VoiceMemo: Failed to set I2S pins: %d", res);
        i2s_driver_uninstall(VOICEMEMO_I2S_PORT);
        return false;
    }

    res = i2s_start(VOICEMEMO_I2S_PORT);
    if (res != ESP_OK) {
        LOG_ERROR("VoiceMemo: Failed to start I2S: %d", res);
        i2s_driver_uninstall(VOICEMEMO_I2S_PORT);
        return false;
    }

    i2sInitialized = true;
    LOG_INFO("VoiceMemo: I2S initialized successfully");
    return true;
#else
    LOG_WARN("VoiceMemo: No I2S microphone pins defined (need ES7210_* or DAC_I2S_* with DIN)");
    return false;
#endif
}

void VoiceMemoModule::deinitI2S()
{
    if (!i2sInitialized)
        return;

    i2s_stop(VOICEMEMO_I2S_PORT);
    i2s_driver_uninstall(VOICEMEMO_I2S_PORT);
    i2sInitialized = false;
    LOG_INFO("VoiceMemo: I2S deinitialized");
}

void VoiceMemoModule::startRecording()
{
    if (state != VoiceMemoState::IDLE)
        return;

    if (!initI2S()) {
        LOG_ERROR("VoiceMemo: Cannot start recording, I2S init failed");
        return;
    }

    state = VoiceMemoState::RECORDING;
    recordingStartMs = millis();
    encodedFrameIndex = sizeof(VoiceMemoHeader); // Leave room for header
    currentMemoId++;                             // New unique ID for this recording session
    currentSequence = 0;                         // Reset sequence counter

    // Initialize header (will be updated with sequence before each send)
    VoiceMemoHeader *header = (VoiceMemoHeader *)encodedFrame;
    memcpy(header->magic, VOICEMEMO_MAGIC, sizeof(VOICEMEMO_MAGIC));
    header->mode = VOICEMEMO_CODEC2_MODE;
    header->sequence = 0;
    header->totalParts = 0; // 0 = streaming/unknown total
    header->memoId = currentMemoId;

    LOG_INFO("VoiceMemo: Recording started (memoId=%d)", currentMemoId);

    // Request focus and update UI to show recording screen
    requestFocus();
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);
}

void VoiceMemoModule::stopRecording()
{
    if (state != VoiceMemoState::RECORDING)
        return;

    LOG_INFO("VoiceMemo: Recording stopped, duration=%dms", millis() - recordingStartMs);

    // Transition to SENDING state for visual feedback
    state = VoiceMemoState::SENDING;

    // Keep focus and update UI to show sending state
    requestFocus();
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);

    // Send any remaining encoded data
    if (encodedFrameIndex > sizeof(VoiceMemoHeader)) {
        sendEncodedPayload();
    }

    // Small delay so user can see the "Sent" message
    sendingCompleteMs = millis();

    deinitI2S();
}

// Codec2 task function - runs with 30KB stack for DSP operations
static void run_voicememo_codec2(void *parameter)
{
    LOG_INFO("VoiceMemo: Codec2 task started");

    while (true) {
        // Wait for notification that audio is ready to encode
        uint32_t count = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));

        if (count != 0 && voiceMemoModule) {
            voiceMemoModule->doCodec2Encode();
        }
    }
}

// Playback task function - runs with 30KB stack for Codec2 decoding
static void run_voicememo_playback(void *parameter)
{
    LOG_INFO("VoiceMemo: Playback task started");

    while (true) {
        // Wait for notification that playback is requested
        uint32_t count = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));

        if (count != 0 && voiceMemoModule) {
            voiceMemoModule->doCodec2Playback();
        }
    }
}

void VoiceMemoModule::processRecordingBuffer()
{
    if (!codec2 || state != VoiceMemoState::RECORDING)
        return;

    LOG_DEBUG("VoiceMemo: processRecordingBuffer start, adcBufferSize=%d", adcBufferSize);

    // Diagnostic: Log audio sample statistics BEFORE filtering
    int16_t minVal = speechBuffer[0], maxVal = speechBuffer[0];
    int32_t sumAbs = 0;
    for (int i = 0; i < adcBufferSize; i++) {
        if (speechBuffer[i] < minVal)
            minVal = speechBuffer[i];
        if (speechBuffer[i] > maxVal)
            maxVal = speechBuffer[i];
        sumAbs += abs(speechBuffer[i]);
    }
    int16_t avgAbs = sumAbs / adcBufferSize;
    LOG_INFO("VoiceMemo: RAW audio min=%d max=%d avgAbs=%d (range: %d)", minVal, maxVal, avgAbs, maxVal - minVal);

    // Apply high-pass filter to remove DC offset (can stay on main thread - lightweight)
    // NOTE: HP filter disabled - was collapsing audio to near-zero. RAW audio from ES7210 is clean.
    // if (hpFilter) {
    //     for (int i = 0; i < adcBufferSize; i++) {
    //         speechBuffer[i] = (int16_t)hpFilter->Update((float)speechBuffer[i]);
    //     }
    //     LOG_DEBUG("VoiceMemo: HP filter applied");
    // } else {
    //     LOG_WARN("VoiceMemo: hpFilter is null, skipping filter");
    // }

    // Diagnostic: Log audio sample statistics (filter disabled)
    minVal = speechBuffer[0];
    maxVal = speechBuffer[0];
    sumAbs = 0;
    for (int i = 0; i < adcBufferSize; i++) {
        if (speechBuffer[i] < minVal)
            minVal = speechBuffer[i];
        if (speechBuffer[i] > maxVal)
            maxVal = speechBuffer[i];
        sumAbs += abs(speechBuffer[i]);
    }
    avgAbs = sumAbs / adcBufferSize;
    LOG_INFO("VoiceMemo: Audio stats for encoding min=%d max=%d avgAbs=%d", minVal, maxVal, avgAbs);

    // Signal the codec2 task to do the encoding (on its own large stack)
    if (codec2TaskHandle) {
        audioReady = true;
        xTaskNotifyGive(codec2TaskHandle);
        LOG_DEBUG("VoiceMemo: Signaled codec2 task");
    } else {
        LOG_ERROR("VoiceMemo: No codec2 task handle!");
    }
}

void VoiceMemoModule::doCodec2Encode()
{
    if (!codec2 || !audioReady)
        return;

    audioReady = false;

    LOG_DEBUG("VoiceMemo: doCodec2Encode start, encodedFrameIndex=%d, encodeCodecSize=%d", encodedFrameIndex, encodeCodecSize);

    // Safety checks before encoding
    size_t maxPayload = meshtastic_Constants_DATA_PAYLOAD_LEN;
    if (encodedFrameIndex + encodeCodecSize > maxPayload) {
        LOG_WARN("VoiceMemo: Buffer would overflow, sending early");
        sendEncodedPayload();
        encodedFrameIndex = sizeof(VoiceMemoHeader);
    }

    LOG_DEBUG("VoiceMemo: About to call codec2_encode on task...");

    codec2_encode(codec2, encodedFrame + encodedFrameIndex, speechBuffer);

    LOG_DEBUG("VoiceMemo: Codec2 encode complete");
    encodedFrameIndex += encodeCodecSize;

    // Check if we have a full packet to send
    if (encodedFrameIndex >= maxPayload - sizeof(VoiceMemoHeader)) {
        sendEncodedPayload();
        encodedFrameIndex = sizeof(VoiceMemoHeader);
    }
}

void VoiceMemoModule::sendEncodedPayload()
{
    if (encodedFrameIndex <= sizeof(VoiceMemoHeader))
        return;

    // Update header with current sequence number
    VoiceMemoHeader *header = (VoiceMemoHeader *)encodedFrame;
    header->sequence = currentSequence++;
    header->memoId = currentMemoId;

    meshtastic_MeshPacket *p = allocReply();
    if (!p)
        return;

    p->to = NODENUM_BROADCAST;
    p->hop_limit = 0; // Zero hops - local only as requested
    p->want_ack = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;

    p->decoded.payload.size = encodedFrameIndex;
    memcpy(p->decoded.payload.bytes, encodedFrame, encodedFrameIndex);

    LOG_INFO("VoiceMemo: Sending %d bytes of encoded audio (memoId=%d, seq=%d)", encodedFrameIndex, currentMemoId,
             header->sequence);
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}

void VoiceMemoModule::storeMemo(const meshtastic_MeshPacket &mp)
{
    NodeNum sender = getFrom(&mp);

    // Parse the header to get memo ID and sequence
    if (mp.decoded.payload.size < sizeof(VoiceMemoHeader)) {
        LOG_WARN("VoiceMemo: Packet too small to contain header");
        return;
    }

    VoiceMemoHeader *header = (VoiceMemoHeader *)mp.decoded.payload.bytes;
    uint8_t memoId = header->memoId;
    uint8_t sequence = header->sequence;

    LOG_DEBUG("VoiceMemo: Processing packet from %08x, memoId=%d, seq=%d, size=%d", sender, memoId, sequence,
              mp.decoded.payload.size);

    // First, try to find an existing memo from this sender with the same memoId
    int slot = -1;
    for (int i = 0; i < storedMemoCount; i++) {
        if (storedMemos[i].from == sender && storedMemos[i].memoId == memoId) {
            slot = i;
            LOG_DEBUG("VoiceMemo: Found existing slot %d for memoId=%d", slot, memoId);
            break;
        }
    }

    // If no existing slot, find an empty slot
    if (slot < 0) {
        for (int i = 0; i < VOICEMEMO_MAX_STORED; i++) {
            if (storedMemos[i].dataLen == 0) {
                slot = i;
                LOG_DEBUG("VoiceMemo: Using empty slot %d", slot);
                break;
            }
        }
    }

    // If no empty slot, replace the oldest played memo
    if (slot < 0) {
        uint32_t oldestTime = UINT32_MAX;
        for (int i = 0; i < VOICEMEMO_MAX_STORED; i++) {
            if (storedMemos[i].played && storedMemos[i].timestamp < oldestTime) {
                oldestTime = storedMemos[i].timestamp;
                slot = i;
            }
        }
    }

    // If still no slot, replace the oldest memo
    if (slot < 0) {
        uint32_t oldestTime = UINT32_MAX;
        for (int i = 0; i < VOICEMEMO_MAX_STORED; i++) {
            if (storedMemos[i].timestamp < oldestTime) {
                oldestTime = storedMemos[i].timestamp;
                slot = i;
            }
        }
    }

    if (slot < 0)
        slot = 0; // Fallback

    StoredVoiceMemo &memo = storedMemos[slot];

    // Check if this is a new memo or continuation of existing
    bool isNewMemo = (memo.from != sender || memo.memoId != memoId || memo.dataLen == 0);

    if (isNewMemo) {
        // Initialize new memo - zero the data buffer for clean silence if packets are missing
        memset(&memo, 0, sizeof(StoredVoiceMemo));
        memo.from = sender;
        memo.timestamp = getValidTime(RTCQualityFromNet);
        memo.dataLen = 0;
        memo.memoId = memoId;
        memo.receivedParts = 0;
        memo.expectedParts = header->totalParts;
        memo.codec2Mode = header->mode;
        memo.played = false;
        LOG_DEBUG("VoiceMemo: Starting new memo in slot %d", slot);
    }

    // Check if we already received this sequence
    if (memo.receivedParts & (1 << sequence)) {
        LOG_WARN("VoiceMemo: Duplicate sequence %d, ignoring", sequence);
        return;
    }

    // Calculate storage position based on sequence number to handle out-of-order packets
    // Sequence 0 contains header + data, subsequent sequences contain header + data but we only store data
    size_t maxPayloadData = meshtastic_Constants_DATA_PAYLOAD_LEN - sizeof(VoiceMemoHeader);
    size_t maxStorage = sizeof(memo.data);

    size_t destOffset;
    const uint8_t *srcData;
    size_t srcSize;

    if (sequence == 0) {
        // First packet: store at beginning, include full packet with header
        destOffset = 0;
        srcData = mp.decoded.payload.bytes;
        srcSize = mp.decoded.payload.size;
    } else {
        // Subsequent packets: calculate position based on sequence
        // Position = header size + (sequence * max_data_per_packet)
        // But we need to account for seq 0 having full packet and others just data
        destOffset = sizeof(VoiceMemoHeader) + (sequence * maxPayloadData);
        // Skip header in source, only copy data portion
        srcData = mp.decoded.payload.bytes + sizeof(VoiceMemoHeader);
        srcSize = mp.decoded.payload.size - sizeof(VoiceMemoHeader);
    }

    // Check bounds
    if (destOffset + srcSize > maxStorage) {
        LOG_WARN("VoiceMemo: Storage overflow at seq=%d, truncating", sequence);
        if (destOffset >= maxStorage) {
            return; // Can't store this packet at all
        }
        srcSize = maxStorage - destOffset;
    }

    // Copy data to the correct position
    memcpy(memo.data + destOffset, srcData, srcSize);
    memo.receivedParts |= (1 << sequence);
    memo.timestamp = getValidTime(RTCQualityFromNet);

    // Update dataLen to be the highest offset we've written to
    size_t endOffset = destOffset + srcSize;
    if (endOffset > memo.dataLen) {
        memo.dataLen = endOffset;
    }

    if (slot >= storedMemoCount)
        storedMemoCount = slot + 1;

    LOG_INFO("VoiceMemo: Stored memo from %08x in slot %d (memoId=%d, seq=%d, offset=%d, totalBytes=%d)", memo.from, slot, memoId,
             sequence, destOffset, memo.dataLen);
}

void VoiceMemoModule::playStoredMemo(int index)
{
    if (index < 0 || index >= storedMemoCount)
        return;

    if (storedMemos[index].dataLen == 0)
        return;

    playMemo(storedMemos[index]);
    storedMemos[index].played = true;
}

void VoiceMemoModule::playMemo(const StoredVoiceMemo &memo)
{
    if (!initI2S()) {
        LOG_ERROR("VoiceMemo: Cannot play, I2S init failed");
        return;
    }

    state = VoiceMemoState::PLAYING;
    LOG_INFO("VoiceMemo: Playing memo, %d bytes", memo.dataLen);

    // Request focus and update UI to show playing state
    requestFocus();
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);

    // Store pointer to memo and signal the playback task
    currentPlaybackMemo = &memo;
    playbackReady = true;

    if (playbackTaskHandle) {
        xTaskNotifyGive(playbackTaskHandle);
        LOG_DEBUG("VoiceMemo: Signaled playback task");
    } else {
        LOG_ERROR("VoiceMemo: No playback task handle!");
        state = VoiceMemoState::IDLE;
        deinitI2S();
    }
}

void VoiceMemoModule::doCodec2Playback()
{
    if (!playbackReady || !currentPlaybackMemo) {
        LOG_WARN("VoiceMemo: doCodec2Playback called but not ready");
        return;
    }

    playbackReady = false;
    const StoredVoiceMemo &memo = *currentPlaybackMemo;

    LOG_INFO("VoiceMemo: doCodec2Playback starting, %d bytes", memo.dataLen);

    // Enable amplifier on T-Lora Pager
#if defined(USE_XL9555) && defined(EXPANDS_AMP_EN)
    io.digitalWrite(EXPANDS_AMP_EN, HIGH);
    LOG_DEBUG("VoiceMemo: Amplifier enabled");
#endif

    // Set ES8311 DAC volume (0xFF = 0dB, 0x00 = -95.5dB)
    // ES8311 is at I2C address 0x18, volume register is 0x32
#ifdef T_LORA_PAGER
    Wire.beginTransmission(0x18);
    Wire.write(0x32); // DAC volume register
    Wire.write(0x80); // Reduced volume (~-40dB)
    if (Wire.endTransmission() == 0) {
        LOG_DEBUG("VoiceMemo: ES8311 DAC volume set");
    } else {
        LOG_WARN("VoiceMemo: Failed to set ES8311 volume");
    }
#endif

    // Create a temporary Codec2 decoder if mode differs
    CODEC2 *decoder = codec2;
    bool tempDecoder = false;

    if (memo.codec2Mode != VOICEMEMO_CODEC2_MODE) {
        decoder = codec2_create(memo.codec2Mode);
        if (decoder) {
            codec2_set_lpc_post_filter(decoder, 1, 0, 0.8, 0.2);
            tempDecoder = true;
        } else {
            decoder = codec2;
        }
    }

    int decodeSize = (codec2_bits_per_frame(decoder) + 7) / 8;
    int samplesPerFrame = codec2_samples_per_frame(decoder);

    LOG_DEBUG("VoiceMemo: Decode frame size=%d, samples=%d", decodeSize, samplesPerFrame);

    // Upsampling ratio: 44100 / 8000 = 5.5125
    // We'll use fixed-point math: ratio = 44100/8000 = 5.5125 ≈ 5 + 0.5125
    // For simple upsampling, repeat each sample ~5-6 times with linear interpolation

    // Decode and play each frame
    size_t offset = sizeof(VoiceMemoHeader);
    int frameNum = 0;
    while (offset + decodeSize <= memo.dataLen) {
        codec2_decode(decoder, outputBuffer, memo.data + offset);

        // Diagnostic: Log decoded audio sample statistics
        if (frameNum < 3) { // Log first 3 frames
            int16_t minVal = outputBuffer[0], maxVal = outputBuffer[0];
            int32_t sumAbs = 0;
            for (int i = 0; i < samplesPerFrame; i++) {
                if (outputBuffer[i] < minVal)
                    minVal = outputBuffer[i];
                if (outputBuffer[i] > maxVal)
                    maxVal = outputBuffer[i];
                sumAbs += abs(outputBuffer[i]);
            }
            int16_t avgAbs = sumAbs / samplesPerFrame;
            LOG_INFO("VoiceMemo: DECODED frame[%d] min=%d max=%d avgAbs=%d", frameNum, minVal, maxVal, avgAbs);
        }
        frameNum++;

        // Upsample from 8kHz to 44.1kHz using linear interpolation
        // Ratio = 44100/8000 = 5.5125
        // Output as stereo (L+R interleaved) for ES8311 compatibility
        int upsampleIdx = 0;
        for (int i = 0; i < samplesPerFrame - 1 && upsampleIdx < VOICEMEMO_UPSAMPLE_BUFFER_SIZE - 12; i++) {
            int16_t s0 = outputBuffer[i];
            int16_t s1 = outputBuffer[i + 1];

            // Output ~5.5 samples per input sample using linear interpolation
            // Duplicate each sample for L and R channels (stereo)
            int16_t interp[6];
            interp[0] = s0;
            interp[1] = s0 + ((s1 - s0) * 18) / 100;
            interp[2] = s0 + ((s1 - s0) * 36) / 100;
            interp[3] = s0 + ((s1 - s0) * 55) / 100;
            interp[4] = s0 + ((s1 - s0) * 73) / 100;
            interp[5] = s0 + ((s1 - s0) * 91) / 100;

            // Always output 5 samples, plus 6th every other sample
            int numSamples = (i % 2 == 0) ? 6 : 5;
            for (int j = 0; j < numSamples; j++) {
                upsampleBuffer[upsampleIdx++] = interp[j]; // Left channel
                upsampleBuffer[upsampleIdx++] = interp[j]; // Right channel
            }
        }
        // Handle last sample (stereo)
        if (upsampleIdx < VOICEMEMO_UPSAMPLE_BUFFER_SIZE - 1) {
            int16_t lastSample = outputBuffer[samplesPerFrame - 1];
            upsampleBuffer[upsampleIdx++] = lastSample; // Left
            upsampleBuffer[upsampleIdx++] = lastSample; // Right
        }

        size_t bytesWritten = 0;
        i2s_write(VOICEMEMO_I2S_PORT, upsampleBuffer, upsampleIdx * sizeof(int16_t), &bytesWritten, pdMS_TO_TICKS(500));

        offset += decodeSize;
    }

    if (tempDecoder) {
        codec2_destroy(decoder);
    }

    // Disable amplifier on T-Lora Pager
#if defined(USE_XL9555) && defined(EXPANDS_AMP_EN)
    io.digitalWrite(EXPANDS_AMP_EN, LOW);
    LOG_DEBUG("VoiceMemo: Amplifier disabled");
#endif

    currentPlaybackMemo = nullptr;
    state = VoiceMemoState::IDLE;
    deinitI2S();

    LOG_INFO("VoiceMemo: Playback complete");

    // Update UI
    {
        UIFrameEvent evt;
        evt.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&evt);
    }
}

int VoiceMemoModule::getUnplayedCount()
{
    int count = 0;
    for (int i = 0; i < storedMemoCount; i++) {
        if (storedMemos[i].dataLen > 0 && !storedMemos[i].played) {
            count++;
        }
    }
    return count;
}

const StoredVoiceMemo *VoiceMemoModule::getStoredMemo(int index)
{
    if (index < 0 || index >= storedMemoCount)
        return nullptr;
    return &storedMemos[index];
}

bool VoiceMemoModule::shouldDraw()
{
    return (state == VoiceMemoState::RECORDING || state == VoiceMemoState::SENDING || state == VoiceMemoState::RECEIVING ||
            state == VoiceMemoState::PLAYING);
}

#if HAS_SCREEN
void VoiceMemoModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Keep focus on this frame while we're in an active state
    if (this->state != VoiceMemoState::IDLE) {
        requestFocus();
    }

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    // Draw header bar
    display->fillRect(0 + x, 0 + y, display->getWidth(), FONT_HEIGHT_SMALL);
    display->setColor(BLACK);
    display->drawString(x + 2, y, "Voice Memo");
    display->setColor(WHITE);

    display->setFont(FONT_LARGE);
    display->setTextAlignment(TEXT_ALIGN_CENTER);

    int centerX = display->getWidth() / 2 + x;
    int centerY = (display->getHeight() - FONT_HEIGHT_SMALL) / 2 + y;

    switch (this->state) {
    case VoiceMemoState::RECORDING: {
        uint32_t elapsed = (millis() - recordingStartMs) / 1000;
        char buf[32];
        snprintf(buf, sizeof(buf), "REC %ds", elapsed);
        display->drawString(centerX, centerY, buf);

        // Draw animated recording indicator (blinking circle)
        if ((millis() / 500) % 2 == 0) {
            display->fillCircle(x + 15, y + display->getHeight() / 2, 6);
        } else {
            display->drawCircle(x + 15, y + display->getHeight() / 2, 6);
        }

        // Draw progress bar for max recording time
        int barWidth = display->getWidth() - 40;
        int barX = x + 20;
        int barY = y + display->getHeight() - 12;
        int progress = (elapsed * barWidth) / VOICEMEMO_MAX_RECORD_SECS;
        display->drawRect(barX, barY, barWidth, 6);
        display->fillRect(barX, barY, min(progress, barWidth), 6);
        break;
    }
    case VoiceMemoState::SENDING: {
        display->drawString(centerX, centerY, "Sent!");

        // Draw checkmark
        int checkX = x + 15;
        int checkY = y + display->getHeight() / 2;
        display->drawLine(checkX - 4, checkY, checkX - 1, checkY + 3);
        display->drawLine(checkX - 1, checkY + 3, checkX + 5, checkY - 4);
        break;
    }
    case VoiceMemoState::PLAYING: {
        display->drawString(centerX, centerY, "Playing...");

        // Draw speaker icon (simple representation)
        int spkX = x + 15;
        int spkY = y + display->getHeight() / 2;
        display->fillRect(spkX - 3, spkY - 3, 4, 6);
        display->drawLine(spkX + 1, spkY - 5, spkX + 5, spkY - 8);
        display->drawLine(spkX + 1, spkY + 5, spkX + 5, spkY + 8);
        break;
    }
    case VoiceMemoState::RECEIVING: {
        display->drawString(centerX, centerY, "Received!");

        // Draw envelope/message icon
        int envX = x + 15;
        int envY = y + display->getHeight() / 2;
        display->drawRect(envX - 6, envY - 4, 12, 8);
        display->drawLine(envX - 6, envY - 4, envX, envY + 1);
        display->drawLine(envX + 6, envY - 4, envX, envY + 1);
        break;
    }
    default:
        break;
    }
}
#endif

int32_t VoiceMemoModule::runOnce()
{
    // Handle SENDING state timeout - show "Sent!" for 1.5 seconds
    if (state == VoiceMemoState::SENDING) {
        if (millis() - sendingCompleteMs > 1500) {
            state = VoiceMemoState::IDLE;
            // Update UI
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
        } else {
            // Keep refreshing UI to maintain focus
            requestFocus();
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REDRAW_ONLY;
            notifyObservers(&e);
        }
        return 50; // Very fast polling to maintain focus
    }

    // Handle RECEIVING state - stay on screen until user dismisses
    if (state == VoiceMemoState::RECEIVING) {
        // Keep refreshing UI to maintain focus
        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REDRAW_ONLY;
        notifyObservers(&e);
        return 50; // Very fast polling to maintain focus
    }

    // Check recording timeout
    if (state == VoiceMemoState::RECORDING) {
        uint32_t elapsed = millis() - recordingStartMs;

        // Auto-stop after max recording time
        if (elapsed > VOICEMEMO_MAX_RECORD_SECS * 1000) {
            LOG_INFO("VoiceMemo: Max recording time reached");
            stopRecording();
            return 100;
        }

        // Read audio from I2S
        if (i2sInitialized && codec2) {
            // With TDM mode (2 channels), we read interleaved stereo data (L,R,L,R...)
            // We need adcBufferSize mono samples, so read 2x that for stereo
            size_t stereoReadSize = min((size_t)adcBufferSize, (size_t)VOICEMEMO_ADC_BUFFER_SIZE) * sizeof(int16_t) * 2;
            static int16_t stereoBuffer[VOICEMEMO_ADC_BUFFER_SIZE * 2]; // Temporary stereo buffer
            size_t bytesRead = 0;

            LOG_DEBUG("VoiceMemo: Attempting I2S read, stereo size=%d", stereoReadSize);

            esp_err_t res = i2s_read(VOICEMEMO_I2S_PORT, stereoBuffer, stereoReadSize, &bytesRead, pdMS_TO_TICKS(100));

            if (res != ESP_OK) {
                LOG_ERROR("VoiceMemo: I2S read failed with error %d", res);
            } else if (bytesRead > 0) {
                LOG_DEBUG("VoiceMemo: I2S read %d bytes (stereo)", bytesRead);
                if (bytesRead == stereoReadSize) {
                    // Extract left channel (every other sample) into speechBuffer
                    int numStereoSamples = bytesRead / sizeof(int16_t);
                    int numMonoSamples = numStereoSamples / 2;
                    for (int i = 0; i < numMonoSamples && i < adcBufferSize; i++) {
                        speechBuffer[i] = stereoBuffer[i * 2]; // Left channel is at even indices
                    }
                    LOG_DEBUG("VoiceMemo: Extracted %d mono samples from stereo", numMonoSamples);
                    processRecordingBuffer();
                }
            }
        } else {
            LOG_WARN("VoiceMemo: Recording state but I2S not initialized or codec2 null");
        }

        // Keep refreshing UI to maintain focus and update timer/blinking indicator
        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REDRAW_ONLY;
        notifyObservers(&e);

        return 50; // Fast polling during recording
    }

    return 500; // Slower polling when idle
}

meshtastic_MeshPacket *VoiceMemoModule::allocReply()
{
    return allocDataPacket();
}

ProcessMessage VoiceMemoModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    auto &p = mp.decoded;

    // Only process if we're not the sender
    if (isFromUs(&mp)) {
        return ProcessMessage::CONTINUE;
    }

    // Verify this is a voice memo packet
    if (p.payload.size < sizeof(VoiceMemoHeader)) {
        return ProcessMessage::CONTINUE;
    }

    VoiceMemoHeader *header = (VoiceMemoHeader *)p.payload.bytes;
    if (memcmp(header->magic, VOICEMEMO_MAGIC, sizeof(VOICEMEMO_MAGIC)) != 0) {
        // Not a voice memo packet (might be regular audio)
        return ProcessMessage::CONTINUE;
    }

    LOG_INFO("VoiceMemo: Received voice memo from %08x, %d bytes", getFrom(&mp), p.payload.size);

    // Store the memo for later playback
    storeMemo(mp);

    // Update timestamp for timeout (reset on each packet)
    sendingCompleteMs = millis();

    // Only regenerate frameset if not already showing received state
    if (state != VoiceMemoState::RECEIVING) {
        state = VoiceMemoState::RECEIVING;
        // Request focus and update UI to show receiving notification
        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
    } else {
        // Already in receiving state, just refresh
        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REDRAW_ONLY;
        notifyObservers(&e);
    }

    return ProcessMessage::CONTINUE; // Allow other handlers to process too
}

#endif // ARCH_ESP32 && HAS_I2S && !MESHTASTIC_EXCLUDE_VOICEMEMO
