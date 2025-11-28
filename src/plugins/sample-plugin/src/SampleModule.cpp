#include "ModuleRegistry.h"
#include "SinglePortModule.h"

class MySampleModule : public SinglePortModule
{
  public:
    MySampleModule() : SinglePortModule("my_sample_module", meshtastic_PortNum_REPLY_APP) {
        LOG_INFO("MySampleModule constructor");
    }
};


MESHTASTIC_REGISTER_MODULE(MySampleModule)