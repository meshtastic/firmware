#pragma once
// Linux evdev gamepad/joystick input. Only compiled on Linux portduino targets;
// macOS / non-Linux builds have no <linux/input.h> or epoll. Unlike LinuxInput
// (keyboard, EV_KEY only) this decodes the D-pad from EV_ABS axes as well.
#if ARCH_PORTDUINO && defined(__linux__)
#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include <linux/input.h>
#include <map>
#include <stdint.h>
#include <sys/epoll.h>

#define JOY_MAX_EVENTS 10

// Default button map when config.yaml has no [Input] JoystickButtons override.
// Codes confirmed by evdev capture on a 0079:0011 "USB Gamepad".
#define JOY_BTN_A BTN_THUMB  // 0x121 -> INPUT_BROKER_SELECT
#define JOY_BTN_B BTN_THUMB2 // 0x122 -> INPUT_BROKER_CANCEL
#define JOY_AXIS_CENTER 127  // D-pad resting value
#define JOY_AXIS_LOW 64      // below this -> "min" edge (0)
#define JOY_AXIS_HIGH 192    // above this -> "max" edge (255)

// D-pad auto-repeat while a direction is held (typematic).
#define JOY_REPEAT_DELAY_MS 400    // hold this long before repeats start
#define JOY_REPEAT_INTERVAL_MS 150 // then repeat this often

class LinuxJoystick : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit LinuxJoystick(const char *name);
    void init();   // Registers this source with the InputBroker
    void deInit(); // Strictly for cleanly "rebooting" the binary on native

  protected:
    virtual int32_t runOnce() override;

  private:
    void emitEvent(input_broker_event event);

    const char *_originName;
    bool firstTime = true;

    // evdev button code -> broker event, built from config (or defaults) in init().
    std::map<int, input_broker_event> buttonMap;

    struct epoll_event events[JOY_MAX_EVENTS];
    struct epoll_event ev;
    int fd = -1;
    int epollfd = -1;

    // D-pad auto-repeat state: currently held zone per axis (-1 / 0 / +1) and the
    // next millis() timestamp at which to re-emit while the direction is held.
    int heldX = 0;
    int heldY = 0;
    uint32_t nextRepeatX = 0;
    uint32_t nextRepeatY = 0;
};
extern LinuxJoystick *aLinuxJoystick;
#endif
