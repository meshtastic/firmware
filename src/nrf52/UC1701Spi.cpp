#include <OLEDDisplay.h>

class UC1701Spi : public OLEDDisplay
{
  private:
    uint8_t _rst;
    uint8_t _dc;
    uint8_t _cs;

  public:
    UC1701Spi() { setGeometry(GEOMETRY_128_64); }

    bool connect()
    {
        /*
        pinMode(_dc, OUTPUT);
        pinMode(_cs, OUTPUT);
        pinMode(_rst, OUTPUT);

        SPI.begin();
        SPI.setClockDivider(SPI_CLOCK_DIV2);

        // Pulse Reset low for 10ms
        digitalWrite(_rst, HIGH);
        delay(1);
        digitalWrite(_rst, LOW);
        delay(10);
        digitalWrite(_rst, HIGH);
        */
        return true;
    }

    void display(void) {}

  private:
};

#include "variant.h"

#ifdef ERC12864_CS
#include <UC1701.h>
static UC1701 lcd(PIN_SPI_SCK, PIN_SPI_MOSI, ERC12864_CS, ERC12864_CD);

void testLCD()
{
    // PCD8544-compatible displays may have a different resolution...
    lcd.begin();

    // Write a piece of text on the first line...
    lcd.setCursor(0, 0);
    lcd.print("Hello, World!");
}
#endif