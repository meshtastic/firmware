#ifdef ARCH_PORTDUINO
#include "SeesawRotary.h"
#include "input/InputBroker.h"

using namespace concurrency;

SeesawRotary *seesawRotary;

SeesawRotary::SeesawRotary(const char *name) : OSThread(name)
{
    _originName = name;
}

bool SeesawRotary::init()
{
    if (inputBroker)
        inputBroker->registerSource(this);

    if (!ss.begin(SEESAW_ADDR)) {
        return false;
    }
    // attachButtonInterrupts();

    uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
    if (version != 4991) {
        LOG_WARN("Wrong firmware loaded? %u", version);
    } else {
        LOG_INFO("Found Product 4991");
    }
    /*
    #ifdef ARCH_ESP32
        // Register callbacks for before and after lightsleep
        // Used to detach and reattach interrupts
        lsObserver.observe(&notifyLightSleep);
        lsEndObserver.observe(&notifyLightSleepEnd);
    #endif
    */
    ss.pinMode(SS_SWITCH, INPUT_PULLUP);

    // get starting position
    encoder_position = ss.getEncoderPosition();

    ss.setGPIOInterrupts((uint32_t)1 << SS_SWITCH, 1);
    ss.enableEncoderInterrupt();
    canSleep = true; // Assume we should not keep the board awake

    return true;
}

int32_t SeesawRotary::runOnce()
{
    InputEvent e;
    e.inputEvent = INPUT_BROKER_NONE;
    bool currentlyPressed = !ss.digitalRead(SS_SWITCH);

    if (currentlyPressed && !wasPressed) {
        e.inputEvent = INPUT_BROKER_SELECT;
    }
    wasPressed = currentlyPressed;

    int32_t new_position = ss.getEncoderPosition();
    // did we move arounde?
    if (encoder_position != new_position) {
        if (encoder_position == 0 && new_position != 1) {
            e.inputEvent = INPUT_BROKER_ALT_PRESS;
        } else if (new_position == 0 && encoder_position != 1) {
            e.inputEvent = INPUT_BROKER_USER_PRESS;
        } else if (new_position > encoder_position) {
            e.inputEvent = INPUT_BROKER_USER_PRESS;
        } else {
            e.inputEvent = INPUT_BROKER_ALT_PRESS;
        }
        encoder_position = new_position;
    }
    if (e.inputEvent != INPUT_BROKER_NONE) {
        e.source = this->_originName;
        e.kbchar = 0x00;
        this->notifyObservers(&e);
    }

    return 50;
}
#endif