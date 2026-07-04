/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 by ThingPulse, Daniel Eichhorn
 * Copyright (c) 2018 by Fabrice Weinberg
 *
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

#ifndef SSD1306Wire_h
#define SSD1306Wire_h

#include "OLEDDisplay.h"
#include <Wire.h>
#include <algorithm>

#if defined(ARDUINO_ARCH_AVR) || defined(ARDUINO_ARCH_STM32)
#define _min	min
#define _max	max
#endif
#if defined(ARDUINO_ARCH_ESP32)
#define I2C_MAX_TRANSFER_BYTE 128 /** ESP32 can Transfer 128 bytes */
#else
#define I2C_MAX_TRANSFER_BYTE 17
#endif
//--------------------------------------

class SSD1306Wire : public OLEDDisplay {
  private:
      uint8_t             _address;
      int                 _sda;
      int                 _scl;
      bool                _doI2cAutoInit = false;
      TwoWire*            _wire = NULL;
      long                _frequency;

  public:

    /**
     * Create and initialize the Display using Wire library
     *
     * Beware for retro-compatibility default values are provided for all parameters see below.
     * Please note that if you don't wan't SD1306Wire to initialize and change frequency speed ot need to
     * ensure -1 value are specified for all 3 parameters. This can be usefull to control TwoWire with multiple
     * device on the same bus.
     *
     * @param address I2C Display address
     * @param sda I2C SDA pin number, default to -1 to skip Wire begin call
     * @param scl I2C SCL pin number, default to -1 (only SDA = -1 is considered to skip Wire begin call)
     * @param g display geometry dafault to generic GEOMETRY_128_64, see OLEDDISPLAY_GEOMETRY definition for other options
     * @param i2cBus on ESP32 with 2 I2C HW buses, I2C_ONE for 1st Bus, I2C_TWO fot 2nd bus, default I2C_ONE
     * @param frequency for Frequency by default Let's use ~700khz if ESP8266 is in 160Mhz mode, this will be limited to ~400khz if the ESP8266 in 80Mhz mode
     */
    SSD1306Wire(uint8_t address, int sda = -1, int scl = -1, OLEDDISPLAY_GEOMETRY g = GEOMETRY_128_64, HW_I2C i2cBus = I2C_ONE, long  frequency = 700000) {
      setGeometry(g);

      this->_address = address;
      this->_sda = sda;
      this->_scl = scl;
#if !defined(ARDUINO_ARCH_ESP32) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
      this->_wire = &Wire;
#else
      this->_wire = (i2cBus == I2C_ONE) ? &Wire : &Wire1;
#endif
      this->_frequency = frequency;
    }

    bool connect() {
#if !defined(ARDUINO_ARCH_ESP32) && !defined(ARDUINO_ARCH_ESP8266)
      _wire->begin();
#else
      // On ESP32 arduino, -1 means 'don't change pins', someone else has called begin for us.
      if(this->_sda != -1)
        _wire->begin(this->_sda, this->_scl);
#endif
      // Let's use ~700khz if ESP8266 is in 160Mhz mode
      // this will be limited to ~400khz if the ESP8266 in 80Mhz mode.
      if(this->_frequency != -1)
        _wire->setClock(this->_frequency);
      return true;
    }

    void display(void) {
      initI2cIfNeccesary();
      const int x_offset = (128 - this->width()) / 2;
      #ifdef OLEDDISPLAY_DOUBLE_BUFFER
        uint8_t minBoundY = UINT8_MAX;
        uint8_t maxBoundY = 0;

        uint8_t minBoundX = UINT8_MAX;
        uint8_t maxBoundX = 0;
        uint8_t x, y;

        // Calculate the Y bounding box of changes
        // and copy buffer[pos] to buffer_back[pos];
        for (y = 0; y < (this->height() / 8); y++) {
          for (x = 0; x < this->width(); x++) {
           uint16_t pos = x + y * this->width();
           if (buffer[pos] != buffer_back[pos]) {
             minBoundY = std::min(minBoundY, y);
             maxBoundY = std::max(maxBoundY, y);
             minBoundX = std::min(minBoundX, x);
             maxBoundX = std::max(maxBoundX, x);
           }
           buffer_back[pos] = buffer[pos];
         }
         yield();
        }

        // If the minBoundY wasn't updated
        // we can savely assume that buffer_back[pos] == buffer[pos]
        // holdes true for all values of pos

        if (minBoundY == UINT8_MAX) return;

        sendCommand(COLUMNADDR);
        sendCommand(x_offset + minBoundX);
        sendCommand(x_offset + maxBoundX);

        sendCommand(PAGEADDR);
        sendCommand(minBoundY);
        sendCommand(maxBoundY);

        uint8_t k = 0;
        for (y = minBoundY; y <= maxBoundY; y++) {
          for (x = minBoundX; x <= maxBoundX; x++) {
            if (k == 0) {
              _wire->beginTransmission(_address);
              _wire->write(0x40);
            }

            _wire->write(buffer[x + y * this->width()]);
            k++;
            if (k == (I2C_MAX_TRANSFER_BYTE - 1))  {
              _wire->endTransmission();
              k = 0;
            }
          }
          yield();
        }

        if (k != 0) {
          _wire->endTransmission();
        }
      #else

        sendCommand(COLUMNADDR);
        sendCommand(x_offset);
        sendCommand(x_offset + (this->width() - 1));

        sendCommand(PAGEADDR);
        sendCommand(0x0);

        for (uint16_t i=0; i < displayBufferSize; i++) {
          _wire->beginTransmission(this->_address);
          _wire->write(0x40);
          for (uint8_t x = 0; x < (I2C_MAX_TRANSFER_BYTE - 1); x++) {
            _wire->write(buffer[i]);
            i++;
          }
          i--;
          _wire->endTransmission();
        }
      #endif
    }

    void setI2cAutoInit(bool doI2cAutoInit) {
      _doI2cAutoInit = doI2cAutoInit;
    }

  private:
	int getBufferOffset(void) {
		return 0;
	}
    inline void sendCommand(uint8_t command) __attribute__((always_inline)){
      initI2cIfNeccesary();
      _wire->beginTransmission(_address);
      _wire->write(0x80);
      _wire->write(command);
      _wire->endTransmission();
    }

    void initI2cIfNeccesary() {
      if (_doI2cAutoInit) {
#if !defined(ARDUINO_ARCH_ESP32) && !defined(ARDUINO_ARCH_ESP8266)
      	_wire->begin();
#else
      	_wire->begin(this->_sda, this->_scl);
#endif
      }
    }

};

#endif
