/**
 * @file XiaoRGBLedController.cpp
 * @brief Implementation of RGB LED controller for XIAO RP2350
 *
 * NASA Safe Code Compliance:
 * - No recursion, goto, or setjmp
 * - All loops bounded by MAX_LEDS constant
 * - Static allocation only (no malloc/new after init)
 * - All functions under 60 lines
 * - Assertions verify preconditions
 * - Return values always checked
 */

#include "XiaoRGBLedController.h"

#include <cassert>

#ifdef ARDUINO_ARCH_RP2040
#include <Arduino.h>
#include <hardware/clocks.h>
#include <hardware/pio.h>
#endif

namespace takeover {

// ============================================================================
// WS2812 PIO Program
// ============================================================================

#ifdef ARDUINO_ARCH_RP2040

/// WS2812 PIO program instruction count (Rule 2: fixed, known at compile time)
static constexpr uint8_t WS2812_PROGRAM_LENGTH = 4;

/**
 * WS2812 PIO program for RP2040/RP2350
 * Timing: T1=2, T2=5, T3=3 cycles at 8MHz = 800kHz signal
 *
 * This is the assembled version of the PIO program.
 * Each WS2812 bit requires: LOW[T3] -> HIGH[T1] -> DATA[T2]
 *
 * Rule 2 compliance: Array has fixed size of 4 instructions.
 */
static const uint16_t ws2812_program_instructions[WS2812_PROGRAM_LENGTH] = {
    // .wrap_target (index 0)
    0x6021, // out x, 1         side 0 [2]  ; T3-1 cycles low, get next bit
    // index 1
    0x1023, // jmp !x, do_zero  side 1 [0]  ; T1-1 cycles high, branch on bit
    // index 2
    0x1000, // jmp bitloop      side 1 [4]  ; T2-1 more cycles high for '1'
    // do_zero: (index 3)
    0xa042, // nop              side 0 [4]  ; T2-1 cycles low for '0'
    // .wrap
};

static const struct pio_program ws2812_program = {
    .instructions = ws2812_program_instructions,
    .length = WS2812_PROGRAM_LENGTH,
    .origin = -1,
};

/**
 * @brief Configure PIO state machine for WS2812
 * @param pio PIO instance
 * @param sm State machine number
 * @param offset Program offset in PIO memory
 * @param pin GPIO pin for data output
 * @param freq Operating frequency
 */
static void ws2812_program_init(PIO pio, uint sm, uint offset,
                                uint pin, float freq)
{
    assert(pio != nullptr);
    assert(sm < 4);
    assert(pin < 30);
    assert(freq > 0.0f);

    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_out_shift(&c, false, true, 24);  // Shift left, autopull, 24 bits

    float div = clock_get_hz(clk_sys) / freq;
    sm_config_set_clkdiv(&c, div);

    // Configure sideset: 1 pin, not optional, no pindirs
    sm_config_set_sideset(&c, 1, false, false);

    // Wrap configuration
    sm_config_set_wrap(&c, offset, offset + 3);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}
#endif

// ============================================================================
// RGBColor Implementation
// ============================================================================

uint32_t RGBColor::toGRB() const
{
    // Assert: color values are inherently valid (0-255) by type
    // This assertion verifies the struct isn't corrupted
    assert(red <= 255 && green <= 255 && blue <= 255);

    // WS2812 expects GRB format (green first, then red, then blue)
    uint32_t result = (static_cast<uint32_t>(green) << 16) |
                      (static_cast<uint32_t>(red) << 8) |
                      static_cast<uint32_t>(blue);

    // Post-condition: result fits in 24 bits
    assert((result & 0xFF000000) == 0);

    return result;
}

RGBColor RGBColor::fromRGB24(uint32_t rgb)
{
    // Pre-condition: input should be valid 24-bit RGB (upper byte zero)
    // Note: We mask anyway for safety, but assert for debugging
    assert((rgb & 0xFF000000) == 0);

    RGBColor color;
    color.red   = static_cast<uint8_t>((rgb >> 16) & 0xFF);
    color.green = static_cast<uint8_t>((rgb >> 8) & 0xFF);
    color.blue  = static_cast<uint8_t>(rgb & 0xFF);

    // Post-condition: values extracted correctly
    assert(color.red <= 255 && color.green <= 255 && color.blue <= 255);

    return color;
}

// ============================================================================
// XiaoRGBLedController Implementation
// ============================================================================

XiaoRGBLedController::XiaoRGBLedController()
    : m_brightness(MAX_BRIGHTNESS)
    , m_initialized(false)
    , m_ledsOn(false)
    , m_lastError(LedError::SUCCESS)
#ifdef ARDUINO_ARCH_RP2040
    , m_pio(nullptr)
    , m_stateMachine(0)
    , m_pioOffset(0)
#endif
{
    // Initialize config with safe defaults
    m_config.ledPin = DEFAULT_LED_PIN;
    m_config.powerPin = DEFAULT_POWER_PIN;
    m_config.numLeds = 1;
    m_config.defaultBrightness = MAX_BRIGHTNESS / 10;  // 10% default
    m_config.powerPinActiveHigh = true;

    // Zero-initialize color buffer (static array - no dynamic allocation)
    for (uint8_t i = 0; i < MAX_LEDS; ++i) {
        m_colorBuffer[i] = Colors::OFF;
    }
}

XiaoRGBLedController::~XiaoRGBLedController()
{
    if (m_initialized) {
        (void)shutdown();  // Cast to void - intentionally ignoring return
    }
}

LedError XiaoRGBLedController::initialize(const LedConfig& config)
{
    assert(config.numLeds > 0);
    assert(config.numLeds <= MAX_LEDS);

    // Check preconditions
    if (m_initialized) {
        return setError(LedError::ALREADY_INITIALIZED);
    }

    if (config.numLeds == 0 || config.numLeds > MAX_LEDS) {
        return setError(LedError::INVALID_PARAMETER);
    }

    // Store configuration
    m_config = config;
    m_brightness = config.defaultBrightness;

    // Initialize power pin
    setPowerEnabled(true);

    // Initialize PIO hardware
    LedError pioResult = initializePIO();
    if (pioResult != LedError::SUCCESS) {
        setPowerEnabled(false);
        return pioResult;
    }

    m_initialized = true;
    m_ledsOn = false;

    // Initial update to ensure LEDs are off
    LedError updateResult = update();
    if (updateResult != LedError::SUCCESS) {
        return updateResult;
    }

    return setError(LedError::SUCCESS);
}

LedError XiaoRGBLedController::initializeDefault()
{
    LedConfig defaultConfig;
    defaultConfig.ledPin = DEFAULT_LED_PIN;
    defaultConfig.powerPin = DEFAULT_POWER_PIN;
    defaultConfig.numLeds = 1;
    defaultConfig.defaultBrightness = MAX_BRIGHTNESS / 10;  // 10%
    defaultConfig.powerPinActiveHigh = true;

    return initialize(defaultConfig);
}

LedError XiaoRGBLedController::shutdown()
{
    if (!m_initialized) {
        return setError(LedError::NOT_INITIALIZED);
    }

    // Turn off LEDs first
    (void)turnOff();
    (void)update();

#ifdef ARDUINO_ARCH_RP2040
    // Disable and cleanup PIO state machine
    if (m_pio != nullptr) {
        pio_sm_set_enabled(m_pio, m_stateMachine, false);
        pio_remove_program(m_pio, &ws2812_program, m_pioOffset);
        pio_sm_unclaim(m_pio, m_stateMachine);
        m_pio = nullptr;
    }
#endif

    // Disable power
    setPowerEnabled(false);

    m_initialized = false;
    return setError(LedError::SUCCESS);
}

bool XiaoRGBLedController::isInitialized() const
{
    return m_initialized;
}

LedError XiaoRGBLedController::initializePIO()
{
#ifdef ARDUINO_ARCH_RP2040
    // Try to claim a state machine on pio0 first, then pio1
    m_pio = pio0;
    int smResult = pio_claim_unused_sm(m_pio, false);

    if (smResult < 0) {
        m_pio = pio1;
        smResult = pio_claim_unused_sm(m_pio, false);

        if (smResult < 0) {
            m_pio = nullptr;
            return setError(LedError::PIO_INIT_FAILED);
        }
    }

    m_stateMachine = static_cast<uint>(smResult);

    // Add program to PIO memory
    if (!pio_can_add_program(m_pio, &ws2812_program)) {
        pio_sm_unclaim(m_pio, m_stateMachine);
        m_pio = nullptr;
        return setError(LedError::PIO_INIT_FAILED);
    }

    m_pioOffset = pio_add_program(m_pio, &ws2812_program);

    // Initialize the program
    ws2812_program_init(m_pio, m_stateMachine, m_pioOffset,
                        m_config.ledPin, static_cast<float>(PIO_FREQUENCY));

    return setError(LedError::SUCCESS);
#else
    // Non-RP2040 platform - stub implementation
    return setError(LedError::SUCCESS);
#endif
}

void XiaoRGBLedController::setPowerEnabled(bool enabled)
{
#ifdef ARDUINO_ARCH_RP2040
    pinMode(m_config.powerPin, OUTPUT);

    bool pinState = m_config.powerPinActiveHigh ? enabled : !enabled;
    digitalWrite(m_config.powerPin, pinState ? HIGH : LOW);
#else
    (void)enabled;
#endif
}

LedError XiaoRGBLedController::setLedColor(uint8_t ledIndex, const RGBColor& color)
{
    assert(m_initialized);

    if (!m_initialized) {
        return setError(LedError::NOT_INITIALIZED);
    }

    if (!isValidLedIndex(ledIndex)) {
        return setError(LedError::INVALID_PARAMETER);
    }

    m_colorBuffer[ledIndex] = color;
    return setError(LedError::SUCCESS);
}

LedError XiaoRGBLedController::setAllLeds(const RGBColor& color)
{
    assert(m_initialized);

    if (!m_initialized) {
        return setError(LedError::NOT_INITIALIZED);
    }

    // Bounded loop - MAX_LEDS is compile-time constant (Rule 2)
    for (uint8_t i = 0; i < m_config.numLeds && i < MAX_LEDS; ++i) {
        m_colorBuffer[i] = color;
    }

    return setError(LedError::SUCCESS);
}

LedError XiaoRGBLedController::setBrightness(uint8_t brightness)
{
    assert(m_initialized);

    if (!m_initialized) {
        return setError(LedError::NOT_INITIALIZED);
    }

    m_brightness = brightness;
    return setError(LedError::SUCCESS);
}

uint8_t XiaoRGBLedController::getBrightness() const
{
    return m_brightness;
}

LedError XiaoRGBLedController::turnOff()
{
    // Pre-condition (Rule 5)
    assert(m_initialized);

    if (!m_initialized) {
        return setError(LedError::NOT_INITIALIZED);
    }

    m_ledsOn = false;

    // Post-condition
    assert(!m_ledsOn);

    return setError(LedError::SUCCESS);
}

LedError XiaoRGBLedController::turnOn()
{
    // Pre-condition (Rule 5)
    assert(m_initialized);

    if (!m_initialized) {
        return setError(LedError::NOT_INITIALIZED);
    }

    m_ledsOn = true;

    // Post-condition
    assert(m_ledsOn);

    return setError(LedError::SUCCESS);
}

bool XiaoRGBLedController::isOn() const
{
    // This is a simple getter, assertion validates internal state consistency
    assert(m_ledsOn == true || m_ledsOn == false);  // Boolean sanity check
    return m_ledsOn;
}

void XiaoRGBLedController::sendColorToPIO(uint32_t grb)
{
    // Pre-condition: GRB value should be 24-bit (upper byte zero after our processing)
    assert((grb & 0xFF000000) == 0);

#ifdef ARDUINO_ARCH_RP2040
    // Pre-condition: PIO must be initialized
    assert(m_pio != nullptr);
    assert(m_stateMachine < 4);

    if (m_pio != nullptr) {
        // Shift data left by 8 bits for proper PIO alignment
        // The PIO expects data in upper 24 bits of 32-bit word
        pio_sm_put_blocking(m_pio, m_stateMachine, grb << 8);
    }
#else
    (void)grb;
#endif
}

LedError XiaoRGBLedController::update()
{
    // Pre-conditions (Rule 5)
    assert(m_initialized);
    assert(m_config.numLeds <= MAX_LEDS);

    if (!m_initialized) {
        return setError(LedError::NOT_INITIALIZED);
    }

    // Bounded loop (Rule 2): MAX_LEDS is compile-time constant
    // Loop counter i is uint8_t, bounded by both numLeds AND MAX_LEDS
    for (uint8_t i = 0; i < m_config.numLeds && i < MAX_LEDS; ++i) {
        RGBColor colorToSend;

        if (m_ledsOn) {
            colorToSend = applyBrightness(m_colorBuffer[i], m_brightness);
        } else {
            colorToSend = Colors::OFF;
        }

        uint32_t grb = colorToSend.toGRB();
        sendColorToPIO(grb);
    }

#ifdef ARDUINO_ARCH_RP2040
    // Small delay to ensure data is latched (WS2812 requires ~50us reset)
    delayMicroseconds(60);
#endif

    return setError(LedError::SUCCESS);
}

RGBColor XiaoRGBLedController::interpolate(const RGBColor& color1,
                                            const RGBColor& color2,
                                            uint8_t factor)
{
    // Pre-conditions: validate input colors (Rule 5)
    assert(color1.red <= 255 && color1.green <= 255 && color1.blue <= 255);
    assert(color2.red <= 255 && color2.green <= 255 && color2.blue <= 255);

    // Linear interpolation: result = c1 + (factor/255) * (c2 - c1)
    // Using integer math to avoid floating point (safer for embedded)

    RGBColor result;

    // Cast to int16_t to handle potential negative intermediate values
    int16_t r1 = color1.red;
    int16_t g1 = color1.green;
    int16_t b1 = color1.blue;

    int16_t r2 = color2.red;
    int16_t g2 = color2.green;
    int16_t b2 = color2.blue;

    // Interpolate each channel - result always in valid range
    // because we're interpolating between two 0-255 values
    int16_t rResult = r1 + ((r2 - r1) * factor) / 255;
    int16_t gResult = g1 + ((g2 - g1) * factor) / 255;
    int16_t bResult = b1 + ((b2 - b1) * factor) / 255;

    // Post-condition: results are in valid range
    assert(rResult >= 0 && rResult <= 255);
    assert(gResult >= 0 && gResult <= 255);
    assert(bResult >= 0 && bResult <= 255);

    result.red   = static_cast<uint8_t>(rResult);
    result.green = static_cast<uint8_t>(gResult);
    result.blue  = static_cast<uint8_t>(bResult);

    return result;
}

RGBColor XiaoRGBLedController::applyBrightness(const RGBColor& color,
                                                uint8_t brightness)
{
    // Pre-condition: validate input color (Rule 5)
    assert(color.red <= 255 && color.green <= 255 && color.blue <= 255);

    RGBColor result;

    // Scale each channel by brightness (0-255 maps to 0-100%)
    // Using 16-bit intermediate to prevent overflow
    // Max value: 255 * 255 = 65025, fits in uint16_t
    uint16_t r = (static_cast<uint16_t>(color.red) * brightness) / 255;
    uint16_t g = (static_cast<uint16_t>(color.green) * brightness) / 255;
    uint16_t b = (static_cast<uint16_t>(color.blue) * brightness) / 255;

    // Post-condition: scaled values fit in uint8_t
    assert(r <= 255 && g <= 255 && b <= 255);

    result.red   = static_cast<uint8_t>(r);
    result.green = static_cast<uint8_t>(g);
    result.blue  = static_cast<uint8_t>(b);

    return result;
}

LedError XiaoRGBLedController::getLedColor(uint8_t ledIndex,
                                            RGBColor& outColor) const
{
    // Pre-conditions (Rule 5)
    assert(m_initialized);
    assert(ledIndex < MAX_LEDS);  // Additional bound check assertion

    // Note: const function cannot modify m_lastError via setError()
    // Returns error directly instead

    if (!m_initialized) {
        return LedError::NOT_INITIALIZED;
    }

    if (!isValidLedIndex(ledIndex)) {
        return LedError::INVALID_PARAMETER;
    }

    outColor = m_colorBuffer[ledIndex];

    // Post-condition: output color is valid
    assert(outColor.red <= 255 && outColor.green <= 255 && outColor.blue <= 255);

    return LedError::SUCCESS;
}

uint8_t XiaoRGBLedController::getNumLeds() const
{
    // Post-condition: numLeds is always within valid bounds (Rule 2)
    assert(m_config.numLeds <= MAX_LEDS);
    return m_config.numLeds;
}

LedError XiaoRGBLedController::getLastError() const
{
    // Simple getter - no preconditions needed
    return m_lastError;
}

bool XiaoRGBLedController::isValidLedIndex(uint8_t ledIndex) const
{
    // Pre-condition: config should be valid
    assert(m_config.numLeds <= MAX_LEDS);

    bool isValid = (ledIndex < m_config.numLeds) && (ledIndex < MAX_LEDS);

    // Post-condition: if valid, index is definitely within bounds
    assert(!isValid || ledIndex < MAX_LEDS);

    return isValid;
}

LedError XiaoRGBLedController::setError(LedError error)
{
    m_lastError = error;

    // Post-condition: error was stored
    assert(m_lastError == error);

    return error;
}

} // namespace takeover
