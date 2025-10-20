#pragma once

#include "ScanI2C.h"
#include <stddef.h>

class ScanI2CConsumer
{
  public:
    ScanI2CConsumer();
    virtual void i2cScanFinished(ScanI2C *i2cScanner) = 0;
};

void ScanI2CCompleted(ScanI2C *i2cScanner);