#ifndef SAMPLE_MODULE_H
#define SAMPLE_MODULE_H

#pragma MPM_MODULE(MySampleModule, sampleModule)

#include "SinglePortModule.h"

class MySampleModule : public SinglePortModule
{
  public:
    MySampleModule();
};

extern MySampleModule *sampleModule;

#endif