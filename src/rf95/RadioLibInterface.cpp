#include "RadioLibInterface.h"
#include <configuration.h>

// FIXME, we default to 4MHz SPI, SPI mode 0, check if the datasheet says it can really do that
static SPISettings spiSettings;

RadioLibInterface::RadioLibInterface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy,
                                     SPIClass &spi)
    : module(cs, irq, rst, busy, spi, spiSettings), lora(&module)
{
}

ErrorCode RadioLibInterface::send(MeshPacket *p)
{
    return ERR_NONE;
}

/// Initialise the Driver transport hardware and software.
/// Make sure the Driver is properly configured before calling init().
/// \return true if initialisation succeeded.
bool RadioLibInterface::init()
{
    // FIXME, move this to main
    SPI.begin();

    int res = lora.begin();
    DEBUG_MSG("LORA init result %d\n", res);

    return res == ERR_NONE;
}

/**
 *
 *
 *
// include the library


// SX1262 has the following connections:
// NSS pin:   10
// DIO1 pin:  2
// NRST pin:  3
// BUSY pin:  9
SX1262 lora = new Module(10, 2, 3, 9);

// or using RadioShield
// https://github.com/jgromes/RadioShield
//SX1262 lora = RadioShield.ModuleA;

void setup() {
  Serial.begin(9600);

  // initialize SX1262 with default settings
  Serial.print(F("[SX1262] Initializing ... "));
  // carrier frequency:           434.0 MHz
  // bandwidth:                   125.0 kHz
  // spreading factor:            9
  // coding rate:                 7
  // sync word:                   0x12 (private network)
  // output power:                14 dBm
  // current limit:               60 mA
  // preamble length:             8 symbols
  // TCXO voltage:                1.6 V (set to 0 to not use TCXO)
  // regulator:                   DC-DC (set to true to use LDO)
  // CRC:                         enabled
  int state = lora.begin();
  if (state == ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true);
  }
}

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