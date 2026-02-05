// Wrapper class for GxEPD2_BW

// Generic signature at build-time, so that we can detect display model at run-time
// Workaround for issue of GxEPD2_BW objects not having a shared base class
// Only exposes methods which we are actually using

template <typename Driver0, typename Driver1> class GxEPD2_Multi
{
  public:
    void drawPixel(int16_t x, int16_t y, uint16_t color)
    {
        if (which == 0)
            driver0->drawPixel(x, y, color);
        else
            driver1->drawPixel(x, y, color);
    }

    bool nextPage()
    {
        if (which == 0)
            return driver0->nextPage();
        else
            return driver1->nextPage();
    }

    void hibernate()
    {
        if (which == 0)
            driver0->hibernate();
        else
            driver1->hibernate();
    }

    void init(uint32_t serial_diag_bitrate = 0)
    {
        if (which == 0)
            driver0->init(serial_diag_bitrate);
        else
            driver1->init(serial_diag_bitrate);
    }

    void init(uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration = 20, bool pulldown_rst_mode = false)
    {
        if (which == 0)
            driver0->init(serial_diag_bitrate, initial, reset_duration, pulldown_rst_mode);
        else
            driver1->init(serial_diag_bitrate, initial, reset_duration, pulldown_rst_mode);
    }

    void setRotation(uint8_t x)
    {
        if (which == 0)
            driver0->setRotation(x);
        else
            driver1->setRotation(x);
    }

    void setPartialWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
    {
        if (which == 0)
            driver0->setPartialWindow(x, y, w, h);
        else
            driver1->setPartialWindow(x, y, w, h);
    }

    void setFullWindow()
    {
        if (which == 0)
            driver0->setFullWindow();
        else
            driver1->setFullWindow();
    }

    int16_t width()
    {
        if (which == 0)
            return driver0->width();
        else
            return driver1->width();
    }

    int16_t height()
    {
        if (which == 0)
            return driver0->height();
        else
            return driver1->height();
    }

    void clearScreen(uint8_t value = 0xFF)
    {
        if (which == 0)
            driver0->clearScreen();
        else
            driver1->clearScreen();
    }

    void endAsyncFull()
    {
        if (which == 0)
            driver0->endAsyncFull();
        else
            driver1->endAsyncFull();
    }

    // Exposes methods of the GxEPD2_EPD object which is usually available as GxEPD2_BW::epd
    class Epd2Wrapper
    {
      public:
        bool isBusy() { return m_epd2->isBusy(); }
        GxEPD2_EPD *m_epd2;
    } epd2;

    // Constructor
    // Select driver by passing whichDriver as 0 or 1
    GxEPD2_Multi(uint8_t whichDriver, int16_t cs, int16_t dc, int16_t rst, int16_t busy, SPIClass &spi)
    {
        assert(whichDriver == 0 || whichDriver == 1);
        which = whichDriver;
        LOG_DEBUG("GxEPD2_Multi driver: %d", which);

        if (which == 0) {
            driver0 = new GxEPD2_BW<Driver0, Driver0::HEIGHT>(Driver0(cs, dc, rst, busy, spi));
            epd2.m_epd2 = &(driver0->epd2);
        } else if (which == 1) {
            driver1 = new GxEPD2_BW<Driver1, Driver1::HEIGHT>(Driver1(cs, dc, rst, busy, spi));
            epd2.m_epd2 = &(driver1->epd2);
        }
    }

  private:
    uint8_t which;
    GxEPD2_BW<Driver0, Driver0::HEIGHT> *driver0;
    GxEPD2_BW<Driver1, Driver1::HEIGHT> *driver1;
};