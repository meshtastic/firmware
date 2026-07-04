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

#ifndef SH1106Wire_h
#define SH1106Wire_h

#include "OLEDDisplay.h"
#include <Wire.h>

//--------------------------------------

class ST7567Wire : public OLEDDisplay
{
  private:
    uint8_t _address;
    int _sda;
    int _scl;
    boolean _doI2cAutoInit = false;

  public:
    ST7567Wire(uint8_t _address, int _sda = -1, int _scl = -1, OLEDDISPLAY_GEOMETRY g = GEOMETRY_128_64)
    {
        this->_address = _address;
        this->_sda = _sda;
        this->_scl = _scl;
    }

    bool connect()
    {
#if !defined(ARDUINO_ARCH_ESP32) && !defined(ARDUINO_ARCH8266)
        Wire.begin();
#else
        // On ESP32 arduino, -1 means 'don't change pins', someone else has called begin for us.
        if (this->_sda != -1)
            Wire.begin(this->_sda, this->_scl);
#endif
        // Let's use ~700khz if ESP8266 is in 160Mhz mode
        // this will be limited to ~400khz if the ESP8266 in 80Mhz mode.
        Wire.setClock(700000);
        return true;
    }

    virtual void setBrightness(uint8_t b) {
        uint8_t evLevel = b / 4; // convert 0-255 to 0 to 63 for the LCD controller
        sendCommand(SETCONTRAST); // Electronic Volume Mode Set
        sendCommand(evLevel); // Electronic Volume Register Set()	25H&31H 10.55V  26H&31H 11.5V
    }

    void display(void)
    {
        initI2cIfNeccesary();
#ifdef OLEDDISPLAY_DOUBLE_BUFFER
        uint8_t minBoundY = UINT8_MAX;
        uint8_t maxBoundY = 0;

        uint8_t minBoundX = UINT8_MAX;
        uint8_t maxBoundX = 0;

        uint8_t x, y;

        // Calculate the Y bounding box of changes
        // and copy buffer[pos] to buffer_back[pos];
        for (y = 0; y < (displayHeight / 8); y++) {
            for (x = 0; x < displayWidth; x++) {
                uint16_t pos = x + y * displayWidth;
                if (buffer[pos] != buffer_back[pos]) {
                    minBoundY = std::min(minBoundY, y);
                    maxBoundY = std::max(maxBoundY, y);
                    minBoundX = std::min(minBoundX, x);
                    maxBoundX = std::max(maxBoundX, x);
                }
                buffer_back[pos] = buffer[pos];
            }
            // our CPU is super fast and this array is small, no need to yield
            // yield();
        }

        // If the minBoundY wasn't updated
        // we can savely assume that buffer_back[pos] == buffer[pos]
        // holdes true for all values of pos
        if (minBoundY == UINT8_MAX)
            return;

        // Calculate the colum offset
        uint8_t offset = 0; // some displays need a slight extra offset
        uint8_t minBoundXp2H = (minBoundX + offset) & 0x0F;
        uint8_t minBoundXp2L = 0x10 | ((minBoundX + offset) >> 4);

        byte k = 0;
        for (y = minBoundY; y <= maxBoundY; y++) {
            sendCommand(0xB0 + y);
            sendCommand(minBoundXp2H);
            sendCommand(minBoundXp2L);
            for (x = minBoundX; x <= maxBoundX; x++) {
                if (k == 0) {
                    Wire.beginTransmission(_address);
                    Wire.write(0x40);
                }
                Wire.write(buffer[x + y * displayWidth]);
                k++;
                if (k == 16) {
                    Wire.endTransmission();
                    k = 0;
                }
            }
            if (k != 0) {
                Wire.endTransmission();
                k = 0;
            }
            // Our i2c bus is fast, don't stall until the whole frame is done
            // yield();
        }

        if (k != 0) {
            Wire.endTransmission();
        }
#else
        uint8_t *p = &buffer[0];
        for (uint8_t y = 0; y < 8; y++) {
            sendCommand(0xB0 + y);
            sendCommand(0x00); // col address LSB
            sendCommand(0x10); // col address MSB
            for (uint8_t x = 0; x < 8; x++) {
                Wire.beginTransmission(_address);
                Wire.write(0x40);
                for (uint8_t k = 0; k < 16; k++) {
                    Wire.write(*p++);
                }
                Wire.endTransmission();
            }
        }
#endif
    }

    void setI2cAutoInit(boolean doI2cAutoInit) { _doI2cAutoInit = doI2cAutoInit; }

  protected:
    // Send all the init commands
    virtual void sendInitCommands()
    {
        sendCommand(0xe2); // software reset
        delay(200);

        sendCommand(0xa2); // 1/9 Bias
        sendCommand(SEGREMAP); // 0xa1 SEG direction for columns mirrored (or 0xa0 for normal)
        sendCommand(COMSCANDEC); // COM direction reversed (for rows)

        sendCommand(0x24); // V0 Voltage Resistor Ratio Set(1+RA/RB=5.0)(3.5)

        setBrightness(128);

        sendCommand(0xf8); // The Booster set 4x
        sendCommand(0x01); // The Booster set 4x

        sendCommand(0x2c); // The Power Control Set
        delay(100);

        sendCommand(0x2e); //
        delay(100);

        sendCommand(0x2f); //
        delay(100);        //

        sendCommand(DISPLAYON); // Lcd Disply ON
    }

  private:
    int getBufferOffset(void) { return 0; }
    inline void sendCommand(uint8_t command) __attribute__((always_inline))
    {
        initI2cIfNeccesary();
        Wire.beginTransmission(_address);
        Wire.write(0x00);
        Wire.write(command);
        Wire.endTransmission();
    }

    void initI2cIfNeccesary()
    {
        if (_doI2cAutoInit) {
#if !defined(ARDUINO_ARCH_ESP32) && !defined(ARDUINO_ARCH8266)
            Wire.begin();
#else
            Wire.begin(this->_sda, this->_scl);
#endif
        }
    }
};

#endif
