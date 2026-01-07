#pragma once
#include "UpDownInterruptBase.h"

class UpDownInterruptImpl1 : public UpDownInterruptBase
{
  public:
    UpDownInterruptImpl1();
    bool init();
    static void handleIntDown();
    static void handleIntUp();
    static void handleIntPressed();
};

extern UpDownInterruptImpl1 *upDownInterruptImpl1;