// meshtastic/firmware/variants/unphone/variant.cpp

#include "unPhone.h"

unPhone u = unPhone("meshtastic_unphone");

void initVariant() {
  u.begin();
  u.store(u.buildTime);
  u.printWakeupReason();        // what woke us up? stored, not printed :|
  u.checkPowerSwitch();         // if power switch is off, shutdown

  // buzz a bit
  for(int i = 0; i < 3; i++) {
    u.vibe(true);  delay(150); u.vibe(false); delay(150);
  }
}
