#include "RadioLibInterface.h"
#include <configuration.h>

// FIXME, we default to 4MHz SPI, SPI mode 0, check if the datasheet says it can really do that
static SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

RadioLibInterface::RadioLibInterface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy,
                                     SPIClass &spi, PhysicalLayer *_iface)
    : module(cs, irq, rst, busy, spi, spiSettings), iface(*_iface)
{
}

ErrorCode RadioLibInterface::send(MeshPacket *p)
{
    return ERR_NONE;
}

/**
 *
 *
 *
// include the library



void loop() {
  Serial.print(F("[SX1262] Transmitting packet ... "));

  // you can transmit C-string or Arduino string up to
  // 256 characters long
  // NOTE: transmit() is a blocking method!
  //       See example SX126x_Transmit_Interrupt for details
  //       on non-blocking transmission method.
  int state = lora.transmit("Hello World!");

  // you can also transmit byte array up to 256 bytes long

    byte byteArr[] = {0x01, 0x23, 0x45, 0x56, 0x78, 0xAB, 0xCD, 0xEF};
    int state = lora.transmit(byteArr, 8);


if (state == ERR_NONE) {
    // the packet was successfully transmitted
    Serial.println(F("success!"));

    // print measured data rate
    Serial.print(F("[SX1262] Datarate:\t"));
    Serial.print(lora.getDataRate());
    Serial.println(F(" bps"));

} else if (state == ERR_PACKET_TOO_LONG) {
    // the supplied packet was longer than 256 bytes
    Serial.println(F("too long!"));

} else if (state == ERR_TX_TIMEOUT) {
    // timeout occured while transmitting packet
    Serial.println(F("timeout!"));

} else {
    // some other error occurred
    Serial.print(F("failed, code "));
    Serial.println(state);
}

// wait for a second before transmitting again
delay(1000);
}
 */