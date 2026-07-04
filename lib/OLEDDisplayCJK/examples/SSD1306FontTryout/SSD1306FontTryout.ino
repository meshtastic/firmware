/**
   The MIT License (MIT)

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

/**
   Using this sketch, you can try out the fonts. Simply generate new font files
   at https://oleddisplay.squix.ch/ , download them and add to your sketch with
   "Sketch / Add file" in the Arduino IDE. Then include them and add their names
   to loop(). You'll see the name of the font on the serial monitor as the
   sketch cycles through them.
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

// Include all the font files you add to the Sketch
#include "Dialog_plain_8.h"
#include "Dialog_plain_7.h"
#include "Dialog_plain_6.h"


// This will call the function doPrint() with the name;
// once without and once with quotes.
#define DO_PRINT(a) doPrint(a, #a); 


void setup() {
  display.init();

  // display.flipScreenVertically();

  display.setContrast(255);
}

void loop() {
  
  // These three fonts come with the display driver
  DO_PRINT(ArialMT_Plain_10);
  DO_PRINT(ArialMT_Plain_16);
  DO_PRINT(ArialMT_Plain_24);

  // These three I had generated and added as files
  DO_PRINT(Dialog_plain_6);
  DO_PRINT(Dialog_plain_7);
  DO_PRINT(Dialog_plain_8);

}


void doPrint(const uint8_t* font, String fontname) {
  Serial.println(fontname);

  display.cls();
  display.setFont(font);

  display.println("Lorem ipsum dolor sit amet, consectetur");
  display.println("adipiscing elit, sed do eiusmod tempor");
  display.println("incididunt ut labore et dolore magna aliqua.");
  display.println("Ut enim ad minim veniam, quis nostrud exercitation");
  display.println("ullamco laboris nisi ut aliquip ex ea commodo");
  display.println("consequat. Duis aute irure dolor in reprehenderit");
  display.println("in voluptate velit esse cillum dolore eu fugiat");
  display.println("nulla pariatur. Excepteur sint occaecat cupidatat");
  display.println("non proident, sunt in culpa qui officia deserunt.");

  delay(5000);

}

// The library comes with fonts as a const uint8_t array, the site at
// https://oleddisplay.squix.ch/ generates them as const char array. This code
// converts one in the other to make sure both work here.
void doPrint(const char* font, String fontname) {
  doPrint(static_cast<const uint8_t*>(reinterpret_cast<const void*>(font)), fontname);
}
