#pragma once

#include <OLEDDisplay.h>

/**
 * An adapter class that allows using the TFT_eSPI library as if it was an OLEDDisplay implementation.
 *
 * Remaining TODO:
 * optimize display() to only draw changed pixels (see other OLED subclasses for examples)
 * implement displayOn/displayOff to turn off the TFT device (and backlight)
 * Use the fast NRF52 SPI API rather than the slow standard arduino version
 *
 * turn radio back on - currently with both on spi bus is fucked? or are we leaving chip select asserted?
 */
class TFTDisplay : public OLEDDisplay
{
  public:
    /* constructor
    FIXME - the parameters are not used, just a temporary hack to keep working like the old displays
    */
    TFTDisplay(uint8_t address, int sda, int scl);

    // Write the buffer to the display memory
    virtual void display(void);

  protected:
    // the header size of the buffer used, e.g. for the SPI command header
    virtual int getBufferOffset(void) { return 0; }

    // Send a command to the display (low level function)
    virtual void sendCommand(uint8_t com);

    // Connect to the display
    virtual bool connect();
};
