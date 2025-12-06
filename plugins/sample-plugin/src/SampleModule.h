#ifndef SAMPLE_MODULE_H
#define SAMPLE_MODULE_H

#include "SinglePortModule.h"

class MySampleModule : public SinglePortModule
{
  public:
    MySampleModule();
};

extern MySampleModule *sampleModule;

#endif