/**
 * @file XiaoRGBLedController.h
 * @brief RGB LED controller for XIAO RP2350 using WS2812 protocol via PIO
 *
 * This controller follows NASA's 10 Rules of Safe Code:
 * 1. Simple control flow - no goto, setjmp, recursion
 * 2. All loops have fixed upper bounds
 * 3. No dynamic memory allocation after initialization
 * 4. Functions kept under 60 lines
 * 5. Assertions used for runtime verification
 * 6. Data declared at smallest possible scope
 * 7. Return values checked for all non-void functions
 * 8. Preprocessor limited to includes and simple conditions
 * 9. Limited pointer use (single dereference)
 * 10. Designed for compilation with all warnings enabled
 */

#pragma once

#include <cstdint>

#ifdef ARDUINO_ARCH_RP2040
#include <hardware/pio.h>
#endif

namespace takeover {

/**
 * @brief RGB color structure with packed 32-bit representation
 */
struct RGBColor {
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    RGBColor() : red(0), green(0), blue(0) {}
    RGBColor(uint8_t r, uint8_t g, uint8_t b) : red(r), green(g), blue(b) {}

    /**
     * @brief Convert to 24-bit packed color (GRB format for WS2812)
     */
    uint32_t toGRB() const;

    /**
     * @brief Create from packed 24-bit RGB value
     */
    static RGBColor fromRGB24(uint32_t rgb);
};

/**
 * @brief Predefined color constants
 */
namespace Colors {
    static constexpr RGBColor OFF       = {0, 0, 0};
    static constexpr RGBColor RED       = {255, 0, 0};
    static constexpr RGBColor GREEN     = {0, 255, 0};
    static constexpr RGBColor BLUE      = {0, 0, 255};
    static constexpr RGBColor WHITE     = {255, 255, 255};
    static constexpr RGBColor YELLOW    = {255, 255, 0};
    static constexpr RGBColor CYAN      = {0, 255, 255};
    static constexpr RGBColor MAGENTA   = {255, 0, 255};
    static constexpr RGBColor ORANGE    = {255, 165, 0};
    static constexpr RGBColor PURPLE    = {128, 0, 128};
}

/**
 * @brief Error codes for LED controller operations
 */
enum class LedError : int8_t {
    SUCCESS = 0,
    NOT_INITIALIZED = -1,
    INVALID_PARAMETER = -2,
    PIO_INIT_FAILED = -3,
    ALREADY_INITIALIZED = -4,
    HARDWARE_ERROR = -5
};

/**
 * @brief Configuration for the LED controller
 * All fields initialized to safe defaults (Rule 3: predictable initialization)
 */
struct LedConfig {
    uint8_t ledPin;            ///< GPIO pin for WS2812 data
    uint8_t powerPin;          ///< GPIO pin for LED power control
    uint8_t numLeds;           ///< Number of LEDs in the chain (max 8)
    uint8_t defaultBrightness; ///< Default brightness (0-255)
    bool powerPinActiveHigh;   ///< True if power pin is active high

    /// Default constructor with safe initialization
    LedConfig()
        : ledPin(22)
        , powerPin(23)
        , numLeds(1)
        , defaultBrightness(25)
        , powerPinActiveHigh(true)
    {}
};

/**
 * @brief XIAO RP2350 RGB LED Controller using WS2812 protocol
 *
 * Provides complete control of WS2812-compatible RGB LEDs using PIO
 * hardware on RP2040/RP2350 microcontrollers.
 */
class XiaoRGBLedController {
public:
    /// Maximum supported LEDs in chain (prevents unbounded memory)
    static constexpr uint8_t MAX_LEDS = 8;

    /// Maximum brightness value
    static constexpr uint8_t MAX_BRIGHTNESS = 255;

    /// Minimum brightness value (non-zero for visibility)
    static constexpr uint8_t MIN_BRIGHTNESS = 1;

    /// Default XIAO RP2350 LED pin
    static constexpr uint8_t DEFAULT_LED_PIN = 22;

    /// Default XIAO RP2350 power pin
    static constexpr uint8_t DEFAULT_POWER_PIN = 23;

    /// PIO frequency for WS2812 timing (800kHz base * 10 cycles)
    static constexpr uint32_t PIO_FREQUENCY = 8000000;

    /**
     * @brief Default constructor - creates uninitialized controller
     */
    XiaoRGBLedController();

    /**
     * @brief Destructor - ensures clean shutdown
     */
    ~XiaoRGBLedController();

    // Disable copy operations (Rule 9: limit pointer complexity)
    XiaoRGBLedController(const XiaoRGBLedController&) = delete;
    XiaoRGBLedController& operator=(const XiaoRGBLedController&) = delete;

    /**
     * @brief Initialize the LED controller with configuration
     * @param config Configuration structure
     * @return LedError::SUCCESS on success, error code otherwise
     */
    LedError initialize(const LedConfig& config);

    /**
     * @brief Initialize with default XIAO RP2350 settings
     * @return LedError::SUCCESS on success, error code otherwise
     */
    LedError initializeDefault();

    /**
     * @brief Shutdown the LED controller and release resources
     * @return LedError::SUCCESS on success
     */
    LedError shutdown();

    /**
     * @brief Check if controller is initialized
     * @return true if initialized, false otherwise
     */
    bool isInitialized() const;

    /**
     * @brief Set color of a specific LED
     * @param ledIndex Index of LED (0-based)
     * @param color RGB color to set
     * @return LedError::SUCCESS on success, error code otherwise
     */
    LedError setLedColor(uint8_t ledIndex, const RGBColor& color);

    /**
     * @brief Set all LEDs to the same color
     * @param color RGB color to set
     * @return LedError::SUCCESS on success, error code otherwise
     */
    LedError setAllLeds(const RGBColor& color);

    /**
     * @brief Set brightness level (applied to all colors)
     * @param brightness Brightness value (0-255)
     * @return LedError::SUCCESS on success, error code otherwise
     */
    LedError setBrightness(uint8_t brightness);

    /**
     * @brief Get current brightness level
     * @return Current brightness (0-255)
     */
    uint8_t getBrightness() const;

    /**
     * @brief Turn off all LEDs (preserves color settings)
     * @return LedError::SUCCESS on success
     */
    LedError turnOff();

    /**
     * @brief Turn on all LEDs with current colors
     * @return LedError::SUCCESS on success
     */
    LedError turnOn();

    /**
     * @brief Check if LEDs are currently on
     * @return true if on, false if off
     */
    bool isOn() const;

    /**
     * @brief Update LED hardware with current color buffer
     * @return LedError::SUCCESS on success, error code otherwise
     *
     * Call this after making color changes to push them to hardware.
     */
    LedError update();

    /**
     * @brief Interpolate between two colors
     * @param color1 Starting color
     * @param color2 Ending color
     * @param factor Interpolation factor (0-255, where 0=color1, 255=color2)
     * @return Interpolated color
     */
    static RGBColor interpolate(const RGBColor& color1,
                                const RGBColor& color2,
                                uint8_t factor);

    /**
     * @brief Apply brightness to a color
     * @param color Input color
     * @param brightness Brightness level (0-255)
     * @return Color with brightness applied
     */
    static RGBColor applyBrightness(const RGBColor& color, uint8_t brightness);

    /**
     * @brief Get color of specific LED
     * @param ledIndex Index of LED
     * @param outColor Pointer to receive color value
     * @return LedError::SUCCESS on success, error code otherwise
     */
    LedError getLedColor(uint8_t ledIndex, RGBColor& outColor) const;

    /**
     * @brief Get number of configured LEDs
     * @return Number of LEDs
     */
    uint8_t getNumLeds() const;

    /**
     * @brief Get last error that occurred
     * @return Last error code
     */
    LedError getLastError() const;

private:
    // Configuration storage
    LedConfig m_config;

    // LED color buffer (static allocation - Rule 3)
    RGBColor m_colorBuffer[MAX_LEDS];

    // Current brightness
    uint8_t m_brightness;

    // State flags
    bool m_initialized;
    bool m_ledsOn;

    // Last error
    LedError m_lastError;

#ifdef ARDUINO_ARCH_RP2040
    // PIO state machine
    PIO m_pio;
    uint m_stateMachine;
    uint m_pioOffset;
#endif

    /**
     * @brief Initialize PIO hardware for WS2812 protocol
     * @return LedError::SUCCESS on success, error code otherwise
     */
    LedError initializePIO();

    /**
     * @brief Set power pin state
     * @param enabled true to enable power
     */
    void setPowerEnabled(bool enabled);

    /**
     * @brief Send color data to WS2812 via PIO
     * @param grb 24-bit GRB color value
     */
    void sendColorToPIO(uint32_t grb);

    /**
     * @brief Validate LED index
     * @param ledIndex Index to validate
     * @return true if valid, false otherwise
     */
    bool isValidLedIndex(uint8_t ledIndex) const;

    /**
     * @brief Set and return error code
     * @param error Error to set
     * @return The error code passed in
     */
    LedError setError(LedError error);
};

} // namespace takeover
