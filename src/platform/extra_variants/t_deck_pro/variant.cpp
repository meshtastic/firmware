#include "configuration.h"

#ifdef T_DECK_PRO

#include "input/TouchScreenImpl1.h"
#include <CSE_CST328.h>
#include <Wire.h>

CSE_CST328 tsPanel = CSE_CST328(EINK_WIDTH, EINK_HEIGHT, &Wire, CST328_PIN_RST, CST328_PIN_INT);

volatile bool intReceived = false;

void touchISR()
{
    // Detach the interrupt to prevent multiple interrupts
    detachInterrupt(digitalPinToInterrupt(CST328_PIN_INT));
    intReceived = true;
}

bool readTouch(int16_t *x, int16_t *y)
{
    if (intReceived) {
        intReceived = false;
        // Reattach the interrupt for the next touch
        attachInterrupt(digitalPinToInterrupt(CST328_PIN_INT), touchISR, FALLING);

        // Read the touch point
        if (tsPanel.isTouched(0)) {
            *x = tsPanel.getPoint(0).x;
            *y = tsPanel.getPoint(0).y;
            return true;
        }
    }
    return false;
}

// T-Deck Pro specific init
void lateInitVariant()
{
    tsPanel.begin();
    attachInterrupt(digitalPinToInterrupt(CST328_PIN_INT), touchISR, FALLING);
    touchScreenImpl1 = new TouchScreenImpl1(EINK_WIDTH, EINK_HEIGHT, readTouch);
    touchScreenImpl1->init();
}
#endif