// meshtastic/firmware/variants/unphone/variant.cpp

#include "unPhone.h"

unPhone u = unPhone("meshtastic_unphone");

void initVariant() {
  // say hi, init, blink etc.
  Serial.begin(115200);
  Serial.printf("meshtastic unphone initVariant: starting build from: %s\n", u.buildTime);
  u.begin();
  u.store(u.buildTime);

  // power management
  u.printWakeupReason();        // what woke us up?
  u.checkPowerSwitch();         // if power switch is off, shutdown
  Serial.printf("battery voltage = %3.3f\n", u.batteryVoltage());
  Serial.printf("enabling expander power\n");
  u.expanderPower(true);        // turn expander power on

  // buzz a bit
  for(int i = 0; i < 3; i++) {
    u.vibe(true);  delay(150);
    u.vibe(false); delay(150);
  }
  u.printStore();               // print out stored messages

  u.provisioned();              // redisplay the UI for example
  Serial.println("done with initVariant()");
}
