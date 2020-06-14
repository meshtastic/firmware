#ifdef ARDUINO_NRF52840_PPR
#include "PmuBQ25703A.h"
#include <assert.h>

// Default address for device. Note, it is without read/write bit. When read with analyser,
// this will appear 1 bit shifted to the left
#define BQ25703ADevaddr 0xD6

const byte Lorro_BQ25703A::BQ25703Aaddr = BQ25703ADevaddr;

void PmuBQ25703A::init()
{
    // Set the watchdog timer to not have a timeout
    regs.chargeOption0.set_WDTMR_ADJ(0);
    assert(writeRegEx(regs.chargeOption0)); // FIXME, instead log a critical hw failure and reboot
    delay(15);                              // FIXME, why are these delays required? - check datasheet

    // Set the ADC on IBAT and PSYS to record values
    // When changing bitfield values, call the writeRegEx function
    // This is so you can change all the bits you want before sending out the byte.
    regs.chargeOption1.set_EN_IBAT(1);
    regs.chargeOption1.set_EN_PSYS(1);
    assert(writeRegEx(regs.chargeOption1));
    delay(15);

    // Set ADC to make continuous readings. (uses more power)
    regs.aDCOption.set_ADC_CONV(1);
    // Set individual ADC registers to read. All have default off.
    regs.aDCOption.set_EN_ADC_VBUS(1);
    regs.aDCOption.set_EN_ADC_PSYS(1);
    regs.aDCOption.set_EN_ADC_IDCHG(1);
    regs.aDCOption.set_EN_ADC_ICHG(1);
    regs.aDCOption.set_EN_ADC_VSYS(1);
    regs.aDCOption.set_EN_ADC_VBAT(1);
    // Once bits have been twiddled, send bytes to device
    assert(writeRegEx(regs.aDCOption));
    delay(15);
}

#endif

/*


//Initialise the device and library
Lorro_BQ25703A BQ25703A;

//Instantiate with reference to global set
extern Lorro_BQ25703A::Regt BQ25703Areg;

uint32_t previousMillis;
uint16_t loopInterval = 1000;

void setup() {

  Serial.begin(115200);


}

void loop() {

  uint32_t currentMillis = millis();

  if( currentMillis - previousMillis > loopInterval ){
    previousMillis = currentMillis;

    Serial.print( "Voltage of VBUS: " );
    Serial.print( BQ25703Areg.aDCVBUSPSYS.get_VBUS() );
    Serial.println( "mV" );
    delay( 15 );

    Serial.print( "System power usage: " );
    Serial.print( BQ25703Areg.aDCVBUSPSYS.get_sysPower() );
    Serial.println( "W" );
    delay( 15 );

    Serial.print( "Voltage of VBAT: " );
    Serial.print( BQ25703Areg.aDCVSYSVBAT.get_VBAT() );
    Serial.println( "mV" );
    delay( 15 );

    Serial.print( "Voltage of VSYS: " );
    Serial.print( BQ25703Areg.aDCVSYSVBAT.get_VSYS() );
    Serial.println( "mV" );
    delay( 15 );

    Serial.print( "Charging current: " );
    Serial.print( BQ25703Areg.aDCIBAT.get_ICHG() );
    Serial.println( "mA" );
    delay( 15 );

    Serial.print( "Voltage of VSYS: " );
    Serial.print( BQ25703Areg.aDCIBAT.get_IDCHG() );
    Serial.println( "mA" );
    delay( 15 );

  }

}*/