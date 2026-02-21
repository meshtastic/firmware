/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 by ThingPulse, Daniel Eichhorn
 * Copyright (c) 2018 by Fabrice Weinberg
 * Copyright (c) 2024 by Heltec AutoMation
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * ThingPulse invests considerable time and money to develop these open source libraries.
 * Please support us by buying our products (and not the clones) from
 * https://thingpulse.com
 *
 */

#ifndef ST7789Spi_h
#define ST7789Spi_h

#include "OLEDDisplay.h"
#include "graphics/TFTColorRegions.h"
#include <SPI.h>


#define ST_CMD_DELAY 0x80 // special signifier for command lists

#define ST77XX_NOP 0x00
#define ST77XX_SWRESET 0x01
#define ST77XX_RDDID 0x04
#define ST77XX_RDDST 0x09

#define ST77XX_SLPIN 0x10
#define ST77XX_SLPOUT 0x11
#define ST77XX_PTLON 0x12
#define ST77XX_NORON 0x13

#define ST77XX_INVOFF 0x20
#define ST77XX_INVON 0x21
#define ST77XX_DISPOFF 0x28
#define ST77XX_DISPON 0x29
#define ST77XX_CASET 0x2A
#define ST77XX_RASET 0x2B
#define ST77XX_RAMWR 0x2C
#define ST77XX_RAMRD 0x2E

#define ST77XX_PTLAR 0x30
#define ST77XX_TEOFF 0x34
#define ST77XX_TEON 0x35
#define ST77XX_MADCTL 0x36
#define ST77XX_COLMOD 0x3A

#define ST77XX_MADCTL_MY 0x80
#define ST77XX_MADCTL_MX 0x40
#define ST77XX_MADCTL_MV 0x20
#define ST77XX_MADCTL_ML 0x10
#define ST77XX_MADCTL_RGB 0x00

#define ST77XX_RDID1 0xDA
#define ST77XX_RDID2 0xDB
#define ST77XX_RDID3 0xDC
#define ST77XX_RDID4 0xDD

// Some ready-made 16-bit ('565') color settings:
#define ST77XX_BLACK 0x0000
#define ST77XX_WHITE 0xFFFF
#define ST77XX_RED 0xF800
#define ST77XX_GREEN 0x07E0
#define ST77XX_BLUE 0x001F
#define ST77XX_CYAN 0x07FF
#define ST77XX_MAGENTA 0xF81F
#define ST77XX_YELLOW 0xFFE0
#define ST77XX_ORANGE 0xFC00

#define LED_A_ON LOW

#ifdef ESP_PLATFORM
#undef LED_A_ON
#define LED_A_ON HIGH
#define rtos_free free
#define rtos_malloc malloc
//SPIClass SPI1(HSPI);
#endif
class ST7789Spi : public OLEDDisplay {
  private:
      uint8_t             _rst;
      uint8_t             _dc;
      uint8_t             _cs;
      uint8_t             _ledA;
      int             _miso;
      int             _mosi;
      int             _clk;
      SPIClass * _spi;
      SPISettings 		    _spiSettings;
      uint16_t            _RGB=0xFFFF;
      uint8_t             _buffheight;

      // Memory Data Access Control
      // Meshtastic firmware flips displays by default (legacy of the T-Beam)
      // Our default config here is "flipped" (relative to the bootloader screen), to counter this convention
      uint8_t _MADCTL=ST77XX_MADCTL_RGB|ST77XX_MADCTL_MV|ST77XX_MADCTL_MY;

  public:
    /* pass _cs as -1 to indicate "do not use CS pin", for cases where it is hard wired low */
    ST7789Spi(SPIClass *spiClass,uint8_t _rst, uint8_t _dc, uint8_t _cs, OLEDDISPLAY_GEOMETRY g = GEOMETRY_RAWMODE,uint16_t width=240,uint16_t height=135,int mosi=-1,int miso=-1,int clk=-1) {
      this->_spi = spiClass;
      this->_rst = _rst;
      this->_dc  = _dc;
      this->_cs  = _cs;
      this->_mosi=mosi;
      this->_miso=miso;
      this->_clk=clk;
      //this->_ledA  = _ledA;
      _spiSettings = SPISettings(40000000, MSBFIRST, SPI_MODE0);
      setGeometry(g,width,height);
      setRGB(ST77XX_GREEN); // Default to Green, if color not explicity specified by Meshtastic firmware
    }

    bool connect(){
      this->_buffheight=displayHeight / 8;
      this->_buffheight+=displayHeight % 8 ? 1:0;
      pinMode(_cs, OUTPUT);
      pinMode(_dc, OUTPUT);
      //pinMode(_ledA, OUTPUT);
      if (_cs != (uint8_t) -1) {
        pinMode(_cs, OUTPUT);
      }  
      pinMode(_rst, OUTPUT);

#ifdef ESP_PLATFORM
      _spi->begin(_clk,_miso,_mosi,-1);
#else
      _spi->begin();
#endif
      _spi->setClockDivider (SPI_CLOCK_DIV2);

      // Pulse Reset low for 10ms
      digitalWrite(_rst, HIGH);
      delay(1);
      digitalWrite(_rst, LOW);
      delay(10);
      digitalWrite(_rst, HIGH);
      _spi->begin ();
      //digitalWrite(_ledA, LED_A_ON);
      return true;
    }

    void display(void) {
    const uint16_t fallbackOnColorBe = _RGB;
    const uint16_t fallbackOffColorBe = 0x0000;
    #ifdef OLEDDISPLAY_DOUBLE_BUFFER

       uint16_t minBoundY = UINT16_MAX;
       uint16_t maxBoundY = 0;

       uint16_t minBoundX = UINT16_MAX;
       uint16_t maxBoundX = 0;

       uint16_t x, y;
        
       // Calculate the Y bounding box of changes
       // and copy buffer[pos] to buffer_back[pos];
       for (y = 0; y < _buffheight; y++) {
         for (x = 0; x < displayWidth; x++) {
          //Serial.printf("x  %d y %d\r\n",x,y);
          uint16_t pos = x + y * displayWidth;
          if (buffer[pos] != buffer_back[pos]) {
            minBoundY = min(minBoundY, y);
            maxBoundY = max(maxBoundY, y);
            minBoundX = min(minBoundX, x);
            maxBoundX = max(maxBoundX, x);
          }
          buffer_back[pos] = buffer[pos];
        }
        yield();
       }

       // If the minBoundY wasn't updated
       // we can savely assume that buffer_back[pos] == buffer[pos]
       // holdes true for all values of pos
       if (minBoundY == UINT16_MAX) {
         graphics::clearTFTColorRegions();
         return;
       }

		  set_CS(LOW);
		  _spi->beginTransaction(_spiSettings);

          for (y = minBoundY; y <= maxBoundY; y++)
          {
            for(int temp = 0; temp<8;temp++)
            {
              //setAddrWindow(minBoundX,y*8+temp,maxBoundX-minBoundX+1,1);
              setAddrWindow(minBoundX,y*8+temp,maxBoundX-minBoundX+1,1);
              //setAddrWindow(y*8+temp,minBoundX,1,maxBoundX-minBoundX+1);
              uint32_t const pixbufcount = maxBoundX-minBoundX+1;
              uint16_t *pixbuf = (uint16_t *)rtos_malloc(2 * pixbufcount);
              const int16_t pixelY = static_cast<int16_t>(y * 8 + temp);
              for (x = minBoundX; x <= maxBoundX; x++)
              {
                const bool pixelSet = ((buffer[x + y * displayWidth] >> temp) & 0x01) == 1;
                pixbuf[x - minBoundX] = graphics::resolveTFTColorPixel(static_cast<int16_t>(x), pixelY, pixelSet,
                                                                        fallbackOnColorBe, fallbackOffColorBe);
              }
#ifdef ESP_PLATFORM
              _spi->transferBytes((uint8_t *)pixbuf, NULL, 2 * pixbufcount);
#else
              _spi->transfer(pixbuf, NULL, 2 * pixbufcount);
#endif
              rtos_free(pixbuf);
            }
          }
	  _spi->endTransaction();
	  set_CS(HIGH);

     #else
		  set_CS(LOW);
		  _spi->beginTransaction(_spiSettings);
		uint8_t x, y;
          for (y = 0; y < _buffheight; y++)
          {
            for(int temp = 0; temp<8;temp++)
            {
              //setAddrWindow(minBoundX,y*8+temp,maxBoundX-minBoundX+1,1);
              //setAddrWindow(minBoundX,y*8+temp,maxBoundX-minBoundX+1,1);
              setAddrWindow(y*8+temp,0,1,displayWidth);
              uint32_t const pixbufcount = displayWidth;
              uint16_t *pixbuf = (uint16_t *)rtos_malloc(2 * pixbufcount);
              const int16_t pixelY = static_cast<int16_t>(y * 8 + temp);
              for (x = 0; x < displayWidth; x++)
              {
                const bool pixelSet = ((buffer[x + y * displayWidth] >> temp) & 0x01) == 1;
                pixbuf[x] = graphics::resolveTFTColorPixel(static_cast<int16_t>(x), pixelY, pixelSet,
                                                           fallbackOnColorBe, fallbackOffColorBe);
              }
#ifdef ESP_PLATFORM
              _spi->transferBytes((uint8_t *)pixbuf, NULL, 2 * pixbufcount);
#else
              _spi->transfer(pixbuf, NULL, 2 * pixbufcount);
#endif
              rtos_free(pixbuf);
            }
          }
	  _spi->endTransaction();
	  set_CS(HIGH);

     #endif
      graphics::clearTFTColorRegions();
    }

 virtual void resetOrientation() {
	_MADCTL = ST77XX_MADCTL_RGB|ST77XX_MADCTL_MV|ST77XX_MADCTL_MY;
	sendCommand(ST77XX_MADCTL);
	WriteData(_MADCTL);
	delay(10);
  }
  
 virtual void flipScreenVertically() {
	_MADCTL = ST77XX_MADCTL_RGB|ST77XX_MADCTL_MV|ST77XX_MADCTL_MX;
	sendCommand(ST77XX_MADCTL);
	WriteData(_MADCTL);
	delay(10);
  }
  
 virtual void mirrorScreen() {
	_MADCTL = ST77XX_MADCTL_RGB|ST77XX_MADCTL_MV|ST77XX_MADCTL_MX|ST77XX_MADCTL_MY;
	sendCommand(ST77XX_MADCTL);
	WriteData(_MADCTL);
	delay(10);
  }

  void setRGB(uint16_t c)
  {

    this->_RGB=0x00|c>>8|(c<<8&0xFF00);
  }
  
  void displayOn(void) {
  //sendCommand(DISPLAYON);
  }

  void displayOff(void) {
  //sendCommand(DISPLAYOFF);
  }
  
//#define ST77XX_MADCTL_MY 0x80
//#define ST77XX_MADCTL_MX 0x40
//#define ST77XX_MADCTL_MV 0x20
//#define ST77XX_MADCTL_ML 0x10
  protected:
    // Send all the init commands
    virtual void sendInitCommands()
    {
        sendCommand(ST77XX_SWRESET); //  1: Software reset, no args, w/delay
        delay(150);

        sendCommand(ST77XX_SLPOUT); //  2: Out of sleep mode, no args, w/delay
        delay(10);

        sendCommand(ST77XX_COLMOD); //  3: Set color mode, 16-bit color
        WriteData(0x55); 
        delay(10);
        
        sendCommand(ST77XX_MADCTL); //  4: Mem access ctrl (directions)
        WriteData(_MADCTL); 
        
        sendCommand(ST77XX_CASET); //   5: Column addr set, 
        WriteData(0x00); 
        WriteData(0x00);         //    XSTART = 0
        WriteData(0x00); 
        WriteData(240);          //     XEND = 240
        
        sendCommand(ST77XX_RASET); //   6: Row addr set, 
        WriteData(0x00); 
        WriteData(0x00);         //    YSTART = 0
        WriteData(320>>8); 
        WriteData(320&0xFF);          //    YSTART = 320
        
        sendCommand(ST77XX_SLPOUT); //  7: hack
        delay(10);
        
        sendCommand(ST77XX_NORON); //  8: Normal display on, no args, w/delay
        delay(10);
        
        sendCommand(ST77XX_DISPON); //  9: Main screen turn on, no args, delay
        delay(10);

        sendCommand(ST77XX_INVON); //  10: invert
        delay(10);
    }


  private:

   void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    x += (320-displayWidth)/2;
    y += (240-displayHeight)/2;
    uint32_t xa = ((uint32_t)x << 16) | (x + w - 1);
    uint32_t ya = ((uint32_t)y << 16) | (y + h - 1);

    writeCommand(ST77XX_CASET); // Column addr set
    SPI_WRITE32(xa);

    writeCommand(ST77XX_RASET); // Row addr set
    SPI_WRITE32(ya);

    writeCommand(ST77XX_RAMWR); // write to RAM
  }
	int getBufferOffset(void) {
		return 0;
	}
    inline void set_CS(bool level) {
      if (_cs != (uint8_t) -1) {
        digitalWrite(_cs, level);
      }
    };
    inline void sendCommand(uint8_t com) __attribute__((always_inline)){
      set_CS(HIGH);
      digitalWrite(_dc, LOW);
      set_CS(LOW);
      _spi->beginTransaction(_spiSettings);
      _spi->transfer(com);
      _spi->endTransaction();
      set_CS(HIGH);
      digitalWrite(_dc, HIGH);
    }
    
    inline void WriteData(uint8_t data) __attribute__((always_inline)){
        digitalWrite(_cs, LOW);
        _spi->beginTransaction(_spiSettings);
        _spi->transfer(data);
        _spi->endTransaction();
        digitalWrite(_cs, HIGH);
    }
   void SPI_WRITE32(uint32_t l)
   {
      _spi->transfer(l >> 24);
      _spi->transfer(l >> 16);
      _spi->transfer(l >> 8);
      _spi->transfer(l);
   }
  void writeCommand(uint8_t cmd) {
    digitalWrite(_dc, LOW);
    _spi->transfer(cmd);
    digitalWrite(_dc, HIGH);
  }

// Private functions
  void setGeometry(OLEDDISPLAY_GEOMETRY g, uint16_t width, uint16_t height) {
    this->geometry = g;

    switch (g) {
      case GEOMETRY_128_128:
        this->displayWidth = 128;
        this->displayHeight = 128;
        break;
      case GEOMETRY_128_64:
        this->displayWidth = 128;
        this->displayHeight = 64;
        break;
      case GEOMETRY_128_32:
        this->displayWidth = 128;
        this->displayHeight = 32;
        break;
      case GEOMETRY_64_48:
        this->displayWidth = 64;
        this->displayHeight = 48;
        break;
      case GEOMETRY_64_32:
        this->displayWidth = 64;
        this->displayHeight = 32;
        break;
      case GEOMETRY_RAWMODE:
        this->displayWidth = width > 0 ? width : 128;
        this->displayHeight = height > 0 ? height : 64;
        break;
    }
    uint8_t tmp=displayHeight % 8;
    uint8_t _buffheight=displayHeight / 8;

    if(tmp!=0)
      _buffheight++;
    this->displayBufferSize = displayWidth * _buffheight ;
  }
  


};

#endif
