#pragma once

#include <OLEDDisplay.h>

#if defined(HELTEC_WIRELESS_PAPER_V1_0)
// Re-enable SPI after deep sleep: rtc_gpio_hold_dis()
#include "driver/rtc_io.h"
#endif

/**
 * An adapter class that allows using the GxEPD2 library as if it was an OLEDDisplay implementation.
 *
 * Remaining TODO:
 * optimize display() to only draw changed pixels (see other OLED subclasses for examples)
 * implement displayOn/displayOff to turn off the TFT device (and backlight)
 * Use the fast NRF52 SPI API rather than the slow standard arduino version
 *
 * turn radio back on - currently with both on spi bus is fucked? or are we leaving chip select asserted?
 */
class EInkDisplay : public OLEDDisplay
{
    /// How often should we update the display
    /// thereafter we do once per 5 minutes
    uint32_t slowUpdateMsec = 5 * 60 * 1000;

  public:
    /* constructor
    FIXME - the parameters are not used, just a temporary hack to keep working like the old displays
    */
    EInkDisplay(uint8_t, int, int, OLEDDISPLAY_GEOMETRY, HW_I2C);

    // Write the buffer to the display memory (for eink we only do this occasionally)
    virtual void display(void) override;

    /**
     * Force a display update if we haven't drawn within the specified msecLimit
     *
     * @return true if we did draw the screen
     */
    bool forceDisplay(uint32_t msecLimit = 1000);

    /**
     * shim to make the abstraction happy
     *
     */
    void setDetected(uint8_t detected);

  protected:
    // the header size of the buffer used, e.g. for the SPI command header
    virtual int getBufferOffset(void) override { return 0; }

    // Send a command to the display (low level function)
    virtual void sendCommand(uint8_t com) override;

    // Connect to the display
    virtual bool connect() override;

#if defined(USE_EINK_DYNAMIC_PARTIAL)
    // Full, partial, or skip: balance urgency with display health

    // Use partial refresh if EITHER:
    // * highPriority() was set
    // * a highPriority() update was previously skipped, for rate-limiting - (EINK_HIGHPRIORITY_LIMIT_SECONDS)

    // Use full refresh if EITHER:
    // * lowPriority() was set
    // * too many partial updates in a row: protect display - (EINK_PARTIAL_REPEAT_LIMIT)
    // * no recent updates, and last update was partial: redraw for image quality (EINK_LOWPRIORITY_LIMIT_SECONDS)

    // Rate limit if:
    // * lowPriority() - (EINK_LOWPRIORITY_LIMIT_SECONDS)
    // * highPriority(), if multiple partials have run back-to-back - (EINK_HIGHPRIORITY_LIMIT_SECONDS)

    // Skip update entirely if ALL criteria met:
    // * new image matches old image
    // * lowPriority()
    // * not redrawing for image quality
    // * not refreshing for display health

    // ------------------------------------

    // To implement for your E-Ink display:
    // * edit configForPartialRefresh()
    // * edit configForFullRefresh()
    // * add macros to variant.h, and adjust to taste:

    /*
        #define USE_EINK_DYNAMIC_PARTIAL
        #define EINK_LOWPRIORITY_LIMIT_SECONDS 30
        #define EINK_HIGHPRIORITY_LIMIT_SECONDS 1
        #define EINK_PARTIAL_REPEAT_LIMIT 5
    */

  public:
    void highPriority(); // Suggest partial refresh
    void lowPriority();  // Suggest full refresh

  protected:
    void configForPartialRefresh(); // Display specific code to select partial refresh mode
    void configForFullRefresh();    // Display specific code to return to full refresh mode
    bool newImageMatchesOld();      // Is the new update actually different to the last image?
    bool determineRefreshMode();    // Called immediately before data written to display - choose refresh mode, or abort update

    bool isHighPriority = true;            // Does the method calling update believe that this is urgent?
    bool needsFull = false;                // Is a full refresh forced? (display health)
    bool missedHighPriorityUpdate = false; // Was a high priority update skipped for rate-limiting?
    uint16_t partialRefreshCount = 0;      // How many partials have occurred since last full refresh?
    uint32_t lastUpdateMsec = 0;           // When did the last update occur?
    uint32_t prevImageHash = 0;            // Used to check if update will change screen image (skippable or not)

    // Set in variant.h
    const uint32_t lowPriorityLimitMsec = (uint32_t)1000 * EINK_LOWPRIORITY_LIMIT_SECONDS;   // Max rate for partial refreshes
    const uint32_t highPriorityLimitMsec = (uint32_t)1000 * EINK_HIGHPRIORITY_LIMIT_SECONDS; // Max rate for full refreshes
    const uint32_t partialRefreshLimit = EINK_PARTIAL_REPEAT_LIMIT; // Max consecutive partials, before full is triggered

#else // !USE_EINK_DYNAMIC_PARTIAL
    // Tolerate calls to these methods anywhere, just to be safe
    void highPriority() {}
    void lowPriority() {}
#endif
};
