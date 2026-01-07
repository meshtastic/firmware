#include "SerialConsole.h"
#include "concurrency/OSThread.h"
#include "gps/RTC.h"
#include "mesh/MeshRadio.h"
#include "mesh/NodeDB.h"

#include "TestUtil.h"

void initializeTestEnvironment()
{
    concurrency::hasBeenSetup = true;
    consoleInit();
#if ARCH_PORTDUINO
    struct timeval tv;
    tv.tv_sec = time(NULL);
    tv.tv_usec = 0;
    perhapsSetRTC(RTCQualityNTP, &tv);
#endif
    concurrency::OSThread::setup();
}

void initializeTestEnvironmentMinimal()
{
    // Only satisfy OSThread assertions; skip SerialConsole and platform-specific setup
    concurrency::hasBeenSetup = true;

    // Ensure region/config globals are sane before any RadioInterface instances compute slot timing
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    initRegion();

    concurrency::OSThread::setup();
}