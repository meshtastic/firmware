#ifndef SAMPLE_MODULE_H
#define SAMPLE_MODULE_H

#include "SinglePortModule.h"

class MySampleModule : public SinglePortModule
{
  public:
    MySampleModule() : SinglePortModule("my_sample_module", meshtastic_PortNum_REPLY_APP);
};

#endif