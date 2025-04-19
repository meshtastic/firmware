#pragma once

#include "variant.h"

#ifdef HAS_CST226SE

#include "modules/CannedMessageModule.h"

#include "TouchDrvCSTXXX.hpp"
#include "TouchScreenBase.h"

class TouchScreenCST226SE : public TouchScreenBase
{
  public:
    TouchScreenCST226SE(uint16_t width, uint16_t height, bool (*getTouch)(int16_t *, int16_t *));
    void init(void);

    static bool forwardGetTouch(int16_t *x, int16_t *y);
    bool (*_getTouch)(int16_t *, int16_t *);
    virtual bool getTouch(int16_t &x, int16_t &y);
    virtual void onEvent(const TouchEvent &event);

  private:
    static TouchScreenCST226SE *instance;
    TouchDrvCSTXXX touch;
    uint8_t i2cAddress = 0;

    static constexpr uint8_t PossibleAddresses[2] = {CST226SE_ADDR, CST226SE_ADDR_ALT};
};

// For global reference in other code
extern TouchScreenCST226SE *touchScreenCST226SE;

#endif