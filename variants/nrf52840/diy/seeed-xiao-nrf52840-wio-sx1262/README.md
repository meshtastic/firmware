# XIAO nRF52840 + XIAO Wio SX1262

For a mere doubling in price you too can swap out the XIAO ESP32S3 for a XIAO nRF52840, stack the Wio SX1262 radio board either above or underneath the nRF52840, solder the pins, and achieve a massive improvement in battery life!

I'm not really sure why else you would want to as the ESP32S3 is perfectly cromulent, easily connects to the Wio SX1262 via the B2B connector and has an onboard IPEX connector for the included Bluetooth antenna. So you'll also lose BT range, but you will also have working ADC for the battery in Meshtastic and also have an ESP32S3 to use for something else!

If you're still reading you are clearly gonna do it anyway, so...mount the Wio SX1262 either on top or underneath depending on your preference. The `variant.h` will work with either configuration though it does map the Wio SX1262's button to nRF52840 Pin `D5` as it can still be used as a user button and it's nice to be able to gracefully shutdown a node by holding it down for 5 seconds.

If you do decide to wire up the button, orient it so looking straight-down at the Wio SX1262 the radio chip is at the bottom, button in the middle and the hole is at the top - the **left** side of the button should be soldered to `GND` (e.g. the 2nd pin down the top on the **right** row of pins) and the **right** side of the button should be soldered to `D5` (e.g. the 2nd pin up from the button on the **left** row of pins.). This mirrors the original wiring and wiring it in reverse could end up connecting GND to voltage and that's no beuno.

Serial Pins remain available on `D6` (TX) and `D7` (RX) should you want to use them, and I2C has been mapped to NFC1 (SDA, D30) and NFC2 (SCL, D31)

The same pins could be reordered if you would like to have a different arrangement, in `variant.h` you would just need to change the relevant lines:

```cpp
#define GPS_TX_PIN D6 // This is data from the MCU
#define GPS_RX_PIN D7 // This is data from the GNSS module

#define PIN_WIRE_SDA D6
#define PIN_WIRE_SCL D7
```
