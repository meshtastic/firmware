#include "../configuration.h"
#include "../main.h"
#include <Wire.h>

#ifndef NO_WIRE
void scanI2Cdevice(void)
{
    byte err, addr;
    int nDevices = 0;
    for (addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        err = Wire.endTransmission();
        if (err == 0) {
            DEBUG_MSG("I2C device found at address 0x%x\n", addr);

            nDevices++;

            if (addr == SSD1306_ADDRESS) {
                screen_found = addr;
                DEBUG_MSG("ssd1306 display found\n");
            }
            if (addr == CARDKB_ADDR) {
                cardkb_found = addr;
                DEBUG_MSG("m5 cardKB found\n");
            }
            if (addr == ST7567_ADDRESS) {
                screen_found = addr;
                DEBUG_MSG("st7567 display found\n");
            }
#ifdef AXP192_SLAVE_ADDRESS
            if (addr == AXP192_SLAVE_ADDRESS) {
                axp192_found = true;
                DEBUG_MSG("axp192 PMU found\n");
            }
#endif
        } else if (err == 4) {
            DEBUG_MSG("Unknow error at address 0x%x\n", addr);
        }
    }

    if (nDevices == 0)
        DEBUG_MSG("No I2C devices found\n");
    else
        DEBUG_MSG("done\n");
}
#else
void scanI2Cdevice(void) {}
#endif