// meshtastic/firmware/variants/unphone/variant.cpp

#include "unPhone.h"
unPhone u = unPhone("meshtastic_unphone");

void initVariant() {
  u.begin();                    // initialise hardware etc.
  u.store(u.buildTime);
  u.printWakeupReason();        // what woke us up? (stored, not printed :|)
  u.checkPowerSwitch();         // if power switch is off, shutdown

  for(int i = 0; i < 3; i++) {  // buzz a bit
    u.vibe(true);  delay(150); u.vibe(false); delay(150);
  }
  u.printStore();               // print stored messages to Serial
  Serial.flush();
}
