#include "../configuration.h"

#ifdef RAK_4631
#include "../main.h"
#include <SPI.h>

void d_writeCommand(uint8_t c)
{
    SPI1.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    if (PIN_EINK_DC >= 0)
        digitalWrite(PIN_EINK_DC, LOW);
    if (PIN_EINK_CS >= 0)
        digitalWrite(PIN_EINK_CS, LOW);
    SPI1.transfer(c);
    if (PIN_EINK_CS >= 0)
        digitalWrite(PIN_EINK_CS, HIGH);
    if (PIN_EINK_DC >= 0)
        digitalWrite(PIN_EINK_DC, HIGH);
    SPI1.endTransaction();
}

void d_writeData(uint8_t d)
{
    SPI1.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    if (PIN_EINK_CS >= 0)
        digitalWrite(PIN_EINK_CS, LOW);
    SPI1.transfer(d);
    if (PIN_EINK_CS >= 0)
        digitalWrite(PIN_EINK_CS, HIGH);
    SPI1.endTransaction();
}

unsigned long d_waitWhileBusy(uint16_t busy_time)
{
    if (PIN_EINK_BUSY >= 0) {
        delay(1); // add some margin to become active
        unsigned long start = micros();
        while (1) {
            if (digitalRead(PIN_EINK_BUSY) != HIGH)
                break;
            delay(1);
            if (digitalRead(PIN_EINK_BUSY) != HIGH)
                break;
            if (micros() - start > 10000000)
                break;
        }
        unsigned long elapsed = micros() - start;
        (void)start;
        return elapsed;
    } else
        return busy_time;
}

void scanEInkDevice(void)
{
    SPI1.begin();
    d_writeCommand(0x22);
    d_writeData(0x83);
    d_writeCommand(0x20);
    eink_found = (d_waitWhileBusy(150) > 0) ? true : false;
    if (eink_found)
        LOG_DEBUG("EInk display found");
    else
        LOG_DEBUG("EInk display not found");
    SPI1.end();
}
#endif