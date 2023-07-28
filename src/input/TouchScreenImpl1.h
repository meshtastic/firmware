#pragma once
#include "TouchScreenBase.h"

class TouchScreenImpl1 : public TouchScreenBase
{
  public:
    TouchScreenImpl1(uint16_t width, uint16_t height, bool (*getTouch)(uint16_t *, uint16_t *));
    void init(void);

  protected:
    virtual bool getTouch(uint16_t &x, uint16_t &y);
    virtual void onEvent(const TouchEvent &event);

    bool (*_getTouch)(uint16_t *, uint16_t *);
};

extern TouchScreenImpl1 *touchScreenImpl1;
