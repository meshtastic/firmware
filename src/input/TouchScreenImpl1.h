#pragma once
#include "TouchScreenBase.h"

class TouchScreenImpl1 : public TouchScreenBase
{
  public:
    TouchScreenImpl1(uint16_t width, uint16_t height, bool (*getTouch)(int16_t *, int16_t *));
    void init(void);

  protected:
    virtual bool getTouch(int16_t &x, int16_t &y);
    virtual void onEvent(const TouchEvent &event);

    bool (*_getTouch)(int16_t *, int16_t *);
};

extern TouchScreenImpl1 *touchScreenImpl1;
