#include "TestUtil.h"
#include "SerialConsole.h"
#include "concurrency/OSThread.h"

void initializeTestEnvironment()
{
    concurrency::hasBeenSetup = true;
    concurrency::OSThread::setup();
    consoleInit();
}