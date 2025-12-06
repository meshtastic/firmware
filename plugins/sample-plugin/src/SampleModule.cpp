#include "SampleModule.h"
#include "SinglePortModule.h"
#include "configuration.h"

MySampleModule *sampleModule;

MySampleModule::MySampleModule() : SinglePortModule("my_sample_module", meshtastic_PortNum_REPLY_APP)
{
    LOG_INFO("MySampleModule constructor");
}