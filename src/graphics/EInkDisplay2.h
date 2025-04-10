#pragma once

#ifdef USE_EINK

#include "GxEPD2_BW.h"
#include <OLEDDisplay.h>

/**
 * An adapter class that allows using the GxEPD2 library as if it was an OLEDDisplay implementation.
 *
 * Note: EInkDynamicDisplay derives from this class.
 *
 * Remaining TODO:
 * optimize display() to only draw changed pixels (see other OLED subclasses for examples)
 * implement displayOn/displayOff to turn off the TFT device (and backlight)
 * Use the fast NRF52 SPI API rather than the slow standard arduino version
 *
 * turn radio back on - currently with both on spi bus is fucked? or are we leaving chip select asserted?
 * Suggestion: perhaps similar to HELTEC_WIRELESS_PAPER issue, which resolved with rtc_gpio_hold_dis()
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
    virtual bool forceDisplay(uint32_t msecLimit = 1000);

    /**
     * Run any code needed to complete an update, after the physical refresh has completed.
     * Split from forceDisplay(), to enable async refresh in derived EInkDynamicDisplay class.
     *
     */
    virtual void endUpdate();

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

    // AdafruitGFX display object - instantiated in connect(), variant specific
    GxEPD2_BW<EINK_DISPLAY_MODEL, EINK_DISPLAY_MODEL::HEIGHT> *adafruitDisplay = NULL;

    // If display uses HSPI
#if defined(HELTEC_WIRELESS_PAPER) || defined(HELTEC_WIRELESS_PAPER_V1_0) || defined(HELTEC_VISION_MASTER_E213) ||               \
    defined(HELTEC_VISION_MASTER_E290) || defined(TLORA_T3S3_EPAPER) || defined(CROWPANEL_ESP32S3_5_EPAPER) ||                   \
    defined(CROWPANEL_ESP32S3_4_EPAPER) || defined(CROWPANEL_ESP32S3_2_EPAPER)
    SPIClass *hspi = NULL;
#endif

#if defined(HELTEC_MESH_POCKET)
    SPIClass *spi1 = NULL;
#endif

  private:
    // FIXME quick hack to limit drawing to a very slow rate
    uint32_t lastDrawMsec = 0;
};

#endif
