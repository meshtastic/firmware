// Unit tests for the RadioLib driver calls Meshtastic's radio interfaces make, one set per chip
// family: SX126x (sx126x_tests.h), SX127x (sx127x_tests.h), SX128x (sx128x_tests.h), LR11x0
// (lr11x0_tests.h) and LR2021 (lr2021_tests.h), against the RadioLib pinned in platformio.ini.
//
// No chip is needed. RecordingHal (RecordingHal.h) logs every SPI transaction and answers with a
// status byte every status-byte family reads as success, plus the few scripted replies the setters
// check before acting (the packet type); for SX127x it keeps a register file instead. That reaches
// every setter and mode command the interfaces use, and the tests pin what reaches the chip: the
// opcode, the frame length, and where the value is fixed, the payload.
//
// The regressions guarded are driver changes that send the wrong bytes, or read or write past a
// buffer, on paths no hardware-free test reached before. Under [env:coverage] (-fsanitize=address)
// an out-of-bounds access aborts the program at the call. The first such bug is jgromes/RadioLib#1864:
// the LR2021 DC-DC workaround passes sizeof(uint32_t) as a word count, overrunning the stack on
// every setRxPath() and LoRa modulation change. RadioLib 7.8.0 carries it, so the LR2021 set fails
// until the pin moves to a fix; the set runs last so the other families report first.
//
// Anything that decodes a chip reply (begin(), readData(), getRSSI(), updateFirmware()) needs replies
// scripted per opcode; each family's header lists those under "Grows here".
#include "TestUtil.h"
#include <RadioLib.h>
#include <unity.h>

#include "lr11x0_tests.h"
#include "lr2021_tests.h"
#include "sx126x_tests.h"
#include "sx127x_tests.h"
#include "sx128x_tests.h"

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    runSx126xTests();
    runSx127xTests();
    runSx128xTests();
    runLr11x0Tests();
    runLr2021Tests(); // last: carries the known RadioLib 7.8.0 overrun
    exit(UNITY_END());
}

void loop() {}
