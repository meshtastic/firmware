#pragma once
#include "TrackballInterruptBase.h"

class TrackballInterruptImpl1 : public TrackballInterruptBase
{
  public:
    TrackballInterruptImpl1();
    void init(uint8_t pinDown, uint8_t pinUp, uint8_t pinLeft, uint8_t pinRight, uint8_t pinPress);
    static void handleIntDown();
    static void handleIntUp();
    static void handleIntLeft();
    static void handleIntRight();
    static void handleIntPressed();
};

extern TrackballInterruptImpl1 *trackballInterruptImpl1;
