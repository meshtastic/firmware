#ifdef CRYPTO_TEST_MINIMAL
// The crypto-<board> envs link only the crypto sources, so the firmware symbols they reference
// without using are stubbed here.
#include "SerialConsole.h"
#include "mesh/NodeDB.h"
#include <stdint.h>
#include <stdlib.h>

SerialConsole *console = nullptr;

void RedirectablePrint::log(const char *, const char *, ...) {}

static meshtastic_MyNodeInfo myNodeInfoStorage = meshtastic_MyNodeInfo_init_zero;
meshtastic_MyNodeInfo &myNodeInfo = myNodeInfoStorage;

// RadioLibInterface is not linked; HardwareRNG only needs the instance pointer to be null.
void *radioLibInstanceStub asm("_ZN17RadioLibInterface8instanceE") = nullptr;
bool radioLibRandomBytesStub(void *, uint8_t *, size_t) asm("_ZN17RadioLibInterface11randomBytesEPhj");
bool radioLibRandomBytesStub(void *, uint8_t *, size_t)
{
    return false;
}

#ifdef ARCH_STM32WL
// stm32_base links with --wrap=__assert_func; the real wrapper lives in main-stm32wl.cpp.
extern "C" void __wrap___assert_func(const char *, int, const char *, const char *)
{
    for (;;) {
    }
}
#endif
#endif
