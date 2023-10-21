#pragma once
#include "TrackballInterruptBase.h"

class TrackballInterruptImpl1 : public TrackballInterruptBase
{
  public:
    TrackballInterruptImpl1();
    void init();
    static void handleIntDown();
    static void handleIntUp();
    static void handleIntLeft();
    static void handleIntRight();
    static void handleIntPressed();
};

extern TrackballInterruptImpl1 *trackballInterruptImpl1;
