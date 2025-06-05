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

#ifndef SSD1306WireDiy_h
#define SSD1306WireDiy_h

#include <SSD1306Wire.h>
#include <U8g2lib.h>
#include <DebugConfiguration.h>

//--------------------------------------

// 中文字库（可根据需要替换）
#define FONT u8g2_font_wqy12_t_chinese3
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define BUFFER_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 8)

class SSD1306WireDiy : public SSD1306Wire {
  protected:
      U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2; // 添加u8g2对象

  public:

    /**
     * Create and initialize the Display using Wire library
     *
     * Beware for retro-compatibility default values are provided for all parameters see below.
     * Please note that if you don't wan't SD1306Wire to initialize and change frequency speed ot need to
     * ensure -1 value are specified for all 3 parameters. This can be usefull to control TwoWire with multiple
     * device on the same bus.
     *
     * @param _address I2C Display address
     * @param _sda I2C SDA pin number, default to -1 to skip Wire begin call
     * @param _scl I2C SCL pin number, default to -1 (only SDA = -1 is considered to skip Wire begin call)
     * @param g display geometry dafault to generic GEOMETRY_128_64, see OLEDDISPLAY_GEOMETRY definition for other options
     * @param _i2cBus on ESP32 with 2 I2C HW buses, I2C_ONE for 1st Bus, I2C_TWO fot 2nd bus, default I2C_ONE
     * @param _frequency for Frequency by default Let's use ~700khz if ESP8266 is in 160Mhz mode, this will be limited to ~400khz if the ESP8266 in 80Mhz mode
     */
    SSD1306WireDiy(uint8_t _address, int _sda = -1, int _scl = -1, OLEDDISPLAY_GEOMETRY g = GEOMETRY_128_64, HW_I2C _i2cBus = I2C_ONE, int _frequency = 700000) 
    : SSD1306Wire(_address, _sda, _scl, g, _i2cBus, _frequency), u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE)
    {
      // 初始化u8g2以支持drawStringMaxWidth的中文渲染
      u8g2.begin();
      u8g2.enableUTF8Print();
      u8g2.setFontPosBaseline();
      u8g2.setFont(FONT);
    }

    virtual uint16_t drawStringMaxWidth(int16_t xMove, int16_t yMove, uint16_t maxLineWidth, const String &strUser) override 
    {
        uint16_t u8g2LineHeight = u8g2.getAscent() - u8g2.getDescent();
        LOG_INFO("drawStringMaxWidth: %d,%d", u8g2.getAscent(), u8g2.getDescent());
        int16_t x = xMove;
        int16_t y = yMove + u8g2LineHeight;

        const char* text = strUser.c_str();
        size_t len = strUser.length();

        
        u8g2.setFont(u8g2_font_wqy12_t_chinese3); // 支持中英文

        int lastBreakPos = -1;
        int charsThisLine = 0;
        int firstLineCharCount = 0;
        int totalCharCount = 0;

        for (size_t i = 0; i < len;) {
            uint8_t c = (uint8_t)text[i];

            // UTF-8 编码长度判断
            uint8_t utf8_len = 1;
            if (c < 16) {
                ++i;
                continue;
            } else if (c == '\n') {
                y += u8g2LineHeight - u8g2.getDescent();
                x = xMove;
                if (firstLineCharCount == 0) firstLineCharCount = totalCharCount;
                charsThisLine = 0;
                ++i;
                continue;
            } else if (c < 128) {
                utf8_len = 1;
            } else if ((c & 0xE0) == 0xC0) {
                utf8_len = 2;
            } else if ((c & 0xF0) == 0xE0) {
                utf8_len = 3;
            } else if ((c & 0xF8) == 0xF0) {
                utf8_len = 4;
            }

            if (i + utf8_len > len) break;

            char utf8_buf[5] = {0};
            memcpy(utf8_buf, &text[i], utf8_len);
            uint16_t charWidth = u8g2.getUTF8Width(utf8_buf);

            if (x + charWidth > xMove + maxLineWidth) {
                // 尝试换行
                y += u8g2LineHeight - u8g2.getDescent();
                x = xMove;
                if (firstLineCharCount == 0) {
                    firstLineCharCount = totalCharCount;
                }
                charsThisLine = 0;
            }

            u8g2.drawUTF8(x, y, utf8_buf);
            x += charWidth;
            i += utf8_len;
            totalCharCount++;
            charsThisLine++;

            // 记录可换行的位置（空格或 -）
            if (utf8_buf[0] == ' ' || utf8_buf[0] == '-') {
                lastBreakPos = totalCharCount;
            }
        }

        // 拷贝缓冲到原库
        uint8_t* u8g2Buffer = u8g2.getBufferPtr();
        for (int i = 0; i < BUFFER_SIZE; ++i) {
            this->buffer[i] |= u8g2Buffer[i];
        }

        u8g2.clearBuffer();

        // // 使用原库发送 I2C 数据到屏幕
        // this->display();

        // // 更新显示
        // u8g2.sendBuffer();

        // 返回值逻辑
        if (firstLineCharCount == 0) {
            return 0; // 没有换行，全部内容 fit
        } else {
            return firstLineCharCount; // 换行了，返回第一行字符数
        }
    }
};

#endif
