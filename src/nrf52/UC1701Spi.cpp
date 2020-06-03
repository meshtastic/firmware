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