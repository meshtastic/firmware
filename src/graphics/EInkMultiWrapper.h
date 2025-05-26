// Wrapper class for GxEPD2_BW

// Generic signature at build time, allowing display model to be detected at run-time
// Workaround for issue of GxEPD2_BW objects not having a shared base class
// Only exposes methods which we are actually using
#ifndef _EINKMULTIWRAPPER_H_
#define _EINKMULTIWRAPPER_H_

#include "GxEPD2_BW.h"
#include "GxEPD2_EPD.h"

template <typename DISPLAY_MODEL_1, typename DISPLAY_MODEL_2>
class EInkMultiWrapper
{
public:
    void drawPixel(int16_t x, int16_t y, uint16_t color)
    {
        if (model == 1)
            model1->drawPixel(x, y, color);
        else
            model2->drawPixel(x, y, color);
    }
    void init(uint32_t serial_diag_bitrate = 0) // = 0 : disabled
    {
        if (model == 1)
            model1->init(serial_diag_bitrate);
        else
            model2->init(serial_diag_bitrate);
    }
    
    void init(uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration = 20, bool pulldown_rst_mode = false)
    {
        if (model == 1)
            model1->init(serial_diag_bitrate, initial, reset_duration, pulldown_rst_mode);
        else
            model2->init(serial_diag_bitrate, initial, reset_duration, pulldown_rst_mode);
    }
    void fillScreen(uint16_t color) // 0x0 black, >0x0 white, to buffer
    {
        if (model == 1)
            model1->fillScreen(color);
        else
            model2->fillScreen(color);
    }
    void display(bool partial_update_mode = false)
    {
        if (model == 1)
            model1->display(partial_update_mode);
        else
            model2->display(partial_update_mode);
    }
    void displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
    {
        if (model == 1)
            model1->displayWindow(x, y, w, h);
        else
            model2->displayWindow(x, y, w, h);
    }

    void setFullWindow()
    {
        if (model == 1)
            model1->setFullWindow();
        else
            model2->setFullWindow();
    }

    void setPartialWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
    {
        if (model == 1)
            model1->setPartialWindow(x, y, w, h);
        else
            model2->setPartialWindow(x, y, w, h);
    }

    void firstPage()
    {
        if (model == 1)
            model1->firstPage();
        else
            model2->firstPage();
    }
    void endAsyncFull()
    {
        if (model == 1)
            model1->endAsyncFull();
        else
            model2->endAsyncFull();
    }

    bool nextPage()
    {
        if (model == 1)
            return model1->nextPage();
        else
            return model2->nextPage();
    }
    void drawPaged(void (*drawCallback)(const void*), const void* pv)
    {
        if (model == 1)
            model1->drawPaged(drawCallback, pv);
        else
            model2->drawPaged(drawCallback, pv);
    }

    void drawInvertedBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w, int16_t h, uint16_t color)
    {
        if (model == 1)
            model1->drawInvertedBitmap(x, y, bitmap, w, h, color);
        else
            model2->drawInvertedBitmap(x, y, bitmap, w, h, color);
    }

    void clearScreen(uint8_t value = 0xFF) // init controller memory and screen (default white)
    {
        if (model == 1)
            model1->clearScreen(value);
        else
            model2->clearScreen(value);
    }
    void writeScreenBuffer(uint8_t value = 0xFF) // init controller memory (default white)
    {
        if (model == 1)
            model1->writeScreenBuffer(value);
        else
            model2->writeScreenBuffer(value);
    }
    // write to controller memory, without screen refresh; x and w should be multiple of 8
    void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false)
    {
        if (model == 1)
            model1->writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
        else
            model2->writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
    }
    void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false)
    {
        if (model == 1)
            model1->writeImagePart(x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
        else
            model2->writeImagePart(x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
    }
    void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
    {
        if (model == 1)
            model1->writeImage(black, color, x, y, w, h, invert, mirror_y, pgm);
        else
            model2->writeImage(black, color, x, y, w, h, invert, mirror_y, pgm);
    }
    void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h)
    {
        if (model == 1)
            model1->writeImage(black, color, x, y, w, h);
        else
            model2->writeImage(black, color, x, y, w, h);
    }
    void writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
    {
        if (model == 1)
            model1->writeImagePart( black,  color,  x_part,  y_part,  w_bitmap,  h_bitmap,x,  y,  w,  h,  invert,  mirror_y,  pgm);
        else
            model2->writeImagePart( black,  color,  x_part,  y_part,  w_bitmap,  h_bitmap,x,  y,  w,  h,  invert,  mirror_y,  pgm);
    }

    void writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h)
    {
        if (model == 1)
            model1->writeImagePart(black, color,  x_part,  y_part,  w_bitmap,  h_bitmap,x,  y,  w,  h);
        else
            model2->writeImagePart(black, color,  x_part,  y_part,  w_bitmap,  h_bitmap,x,  y,  w,  h);
    }
    // write sprite of native data to controller memory, without screen refresh; x and w should be multiple of 8
    void writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
    {
        if (model == 1)
            model1->writeNative(data1, data2, x, y, w, h, invert, mirror_y, pgm);
        else
            model2->writeNative(data1, data2, x, y, w, h, invert, mirror_y, pgm);
    }
    // write to controller memory, with screen refresh; x and w should be multiple of 8
    void drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false)
    {
        if (model == 1)
            model1->drawImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
        else
            model2->drawImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
    }
    void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false)
    {
        if (model == 1)
            model1->drawImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
        else
            model2->drawImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
    }
    void drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
    {
        if (model == 1)
            model1->drawImage(black, color, x, y, w, h, invert, mirror_y, pgm);
        else
            model2->drawImage(black, color, x, y, w, h, invert, mirror_y, pgm);
    }
    void drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h)
    {
        if (model == 1)
            model1->drawImage(black, color, x, y, w, h);
        else
            model2->drawImage(black, color, x, y, w, h);
    }
    void drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
    {
        if (model == 1)
            model1->drawImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
        else
            model2->drawImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
    }
    void drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h)
    {
        if (model == 1)
            model1->drawImagePart( black,  color,  x_part,  y_part,  w_bitmap,  h_bitmap,x,  y,  w,  h);
        else
            model2->drawImagePart( black,  color,  x_part,  y_part,  w_bitmap,  h_bitmap,x,  y,  w,  h);
    }
    // write sprite of native data to controller memory, with screen refresh; x and w should be multiple of 8
    void drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
    {
        if (model == 1)
            model1->drawNative(data1, data2, x, y, w, h, invert, mirror_y, pgm);
        else
            model2->drawNative(data1, data2, x, y, w, h, invert, mirror_y, pgm);
    }
    void refresh(bool partial_update_mode = false) // screen refresh from controller memory to full screen
    {
        if (model == 1)
            model1->refresh(partial_update_mode);
        else
            model2->refresh(partial_update_mode);
    }
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h) // screen refresh from controller memory, partial screen
    {
        if (model == 1)
            model1->refresh(x, y, w, h);
        else
            model2->refresh(x, y, w, h);
    }
    // turns off generation of panel driving voltages, avoids screen fading over time
    void powerOff()
    {
        if (model == 1)
            model1->powerOff();
        else
            model2->powerOff();
    }
    // turns powerOff() and sets controller to deep sleep for minimum power use, ONLY if wakeable by RST (rst >= 0)
    void hibernate()
    {
        if (model == 1)
            model1->hibernate();
        else
            model2->hibernate();
    }

    void setRotation(uint8_t x) 
    {
        if (model == 1)
            model1->setRotation(x);
        else
            model2->setRotation(x);
    }

    int16_t width()
    {
        if (model == 1)
            return model1->width();
        else
            return model2->width();
    }

    int16_t height()
    {
        if (model == 1)
            return model1->height();
        else
            return model2->height();
    }


    // Exposes methods of the GxEPD2_EPD object which is usually available as GxEPD2_BW::epd
    class Epd2Wrapper
    {
    public:
        bool isBusy() { return m_epd2->isBusy(); }
        GxEPD2_EPD *m_epd2;
    } epd2;

    // Constructor
    // Select driver by passing whichModel as 1 or 2
    EInkMultiWrapper(uint8_t whichModel, int16_t cs, int16_t dc, int16_t rst, int16_t busy, SPIClass &spi)
    {
        assert(whichModel == 1 || whichModel == 2);
        model = whichModel;
        // LOG_DEBUG("GxEPD2_BW_MultiWrapper using driver %d", model);

        if (model == 1)
        {
            model1 = new GxEPD2_BW<DISPLAY_MODEL_1, DISPLAY_MODEL_1::HEIGHT>(DISPLAY_MODEL_1(cs, dc, rst, busy, spi));
            epd2.m_epd2 = &(model1->epd2);
        }
        else if (model == 2)
        {
            model2 = new GxEPD2_BW<DISPLAY_MODEL_2, DISPLAY_MODEL_2::HEIGHT>(DISPLAY_MODEL_2(cs, dc, rst, busy, spi));
            epd2.m_epd2 = &(model2->epd2);
        }
    }

private:
    uint8_t model;
    GxEPD2_BW<DISPLAY_MODEL_1, DISPLAY_MODEL_1::HEIGHT> *model1;
    GxEPD2_BW<DISPLAY_MODEL_2, DISPLAY_MODEL_2::HEIGHT> *model2;
};

#endif