/**
   The MIT License (MIT)

   Copyright (c) 2018 by ThingPulse, Daniel Eichhorn
   Copyright (c) 2018 by Fabrice Weinberg

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   ThingPulse invests considerable time and money to develop these open source libraries.
   Please support us by buying our products (and not the clones) from
   https://thingpulse.com

*/

// Include the correct display library
// For a connection via I2C using Wire include
#include <Wire.h>  // Only needed for Arduino 1.6.5 and earlier
#include "SSD1306Wire.h" // legacy include: `#include "SSD1306.h"`
// or #include "SH1106Wire.h", legacy include: `#include "SH1106.h"`
// For a connection via I2C using brzo_i2c (must be installed) include
// #include <brzo_i2c.h> // Only needed for Arduino 1.6.5 and earlier
// #include "SSD1306Brzo.h"
// #include "SH1106Brzo.h"
// For a connection via SPI include
// #include <SPI.h> // Only needed for Arduino 1.6.5 and earlier
// #include "SSD1306Spi.h"
// #include "SH1106SPi.h"

// Use the corresponding display class:

// Initialize the OLED display using SPI
// D5 -> CLK
// D7 -> MOSI (DOUT)
// D0 -> RES
// D2 -> DC
// D8 -> CS
// SSD1306Spi        display(D0, D2, D8);
// or
// SH1106Spi         display(D0, D2);

// Initialize the OLED display using brzo_i2c
// D3 -> SDA
// D5 -> SCL
// SSD1306Brzo display(0x3c, D3, D5);
// or
// SH1106Brzo  display(0x3c, D3, D5);

// Initialize the OLED display using Wire library
SSD1306Wire display(0x3c, SDA, SCL);   // ADDRESS, SDA, SCL  -  SDA and SCL usually populate automatically based on your board's pins_arduino.h e.g. https://github.com/esp8266/Arduino/blob/master/variants/nodemcu/pins_arduino.h
// SH1106Wire display(0x3c, SDA, SCL);

// Adapted from Adafruit_SSD1306
void drawLines() {
  for (int16_t i = 0; i < display.getWidth(); i += 4) {
    display.drawLine(0, 0, i, display.getHeight() - 1);
    display.display();
    delay(10);
  }
  for (int16_t i = 0; i < display.getHeight(); i += 4) {
    display.drawLine(0, 0, display.getWidth() - 1, i);
    display.display();
    delay(10);
  }
  delay(250);

  display.clear();
  for (int16_t i = 0; i < display.getWidth(); i += 4) {
    display.drawLine(0, display.getHeight() - 1, i, 0);
    display.display();
    delay(10);
  }
  for (int16_t i = display.getHeight() - 1; i >= 0; i -= 4) {
    display.drawLine(0, display.getHeight() - 1, display.getWidth() - 1, i);
    display.display();
    delay(10);
  }
  delay(250);

  display.clear();
  for (int16_t i = display.getWidth() - 1; i >= 0; i -= 4) {
    display.drawLine(display.getWidth() - 1, display.getHeight() - 1, i, 0);
    display.display();
    delay(10);
  }
  for (int16_t i = display.getHeight() - 1; i >= 0; i -= 4) {
    display.drawLine(display.getWidth() - 1, display.getHeight() - 1, 0, i);
    display.display();
    delay(10);
  }
  delay(250);
  display.clear();
  for (int16_t i = 0; i < display.getHeight(); i += 4) {
    display.drawLine(display.getWidth() - 1, 0, 0, i);
    display.display();
    delay(10);
  }
  for (int16_t i = 0; i < display.getWidth(); i += 4) {
    display.drawLine(display.getWidth() - 1, 0, i, display.getHeight() - 1);
    display.display();
    delay(10);
  }
  delay(250);
}

// Adapted from Adafruit_SSD1306
void drawRect(void) {
  for (int16_t i = 0; i < display.getHeight() / 2; i += 2) {
    display.drawRect(i, i, display.getWidth() - 2 * i, display.getHeight() - 2 * i);
    display.display();
    delay(10);
  }
}

// Adapted from Adafruit_SSD1306
void fillRect(void) {
  uint8_t color = 1;
  for (int16_t i = 0; i < display.getHeight() / 2; i += 3) {
    display.setColor((color % 2 == 0) ? BLACK : WHITE); // alternate colors
    display.fillRect(i, i, display.getWidth() - i * 2, display.getHeight() - i * 2);
    display.display();
    delay(10);
    color++;
  }
  // Reset back to WHITE
  display.setColor(WHITE);
}

// Adapted from Adafruit_SSD1306
void drawCircle(void) {
  for (int16_t i = 0; i < display.getHeight(); i += 2) {
    display.drawCircle(display.getWidth() / 2, display.getHeight() / 2, i);
    display.display();
    delay(10);
  }
  delay(1000);
  display.clear();

  // This will draw the part of the circel in quadrant 1
  // Quadrants are numberd like this:
  //   0010 | 0001
  //  ------|-----
  //   0100 | 1000
  //
  display.drawCircleQuads(display.getWidth() / 2, display.getHeight() / 2, display.getHeight() / 4, 0b00000001);
  display.display();
  delay(200);
  display.drawCircleQuads(display.getWidth() / 2, display.getHeight() / 2, display.getHeight() / 4, 0b00000011);
  display.display();
  delay(200);
  display.drawCircleQuads(display.getWidth() / 2, display.getHeight() / 2, display.getHeight() / 4, 0b00000111);
  display.display();
  delay(200);
  display.drawCircleQuads(display.getWidth() / 2, display.getHeight() / 2, display.getHeight() / 4, 0b00001111);
  display.display();
}

void printBuffer(void) {
  // Some test data
  const char* test[] = {
    "Hello",
    "World" ,
    "----",
    "Show off",
    "how",
    "the log buffer",
    "is",
    "working.",
    "Even",
    "scrolling is",
    "working"
  };
  display.clear();
  for (uint8_t i = 0; i < 11; i++) {
    // Print to the screen
    display.println(test[i]);
    delay(500);
  }
}

void setup() {
  display.init();

  // display.flipScreenVertically();

  display.setContrast(255);

  drawLines();
  delay(1000);
  display.clear();

  drawRect();
  delay(1000);
  display.clear();

  fillRect();
  delay(1000);
  display.clear();

  drawCircle();
  delay(1000);
  display.clear();

  printBuffer();
  delay(1000);
  display.clear();
}

void loop() { }
