#include "configuration.h"
#if ARCH_PORTDUINO && defined(__linux__)
#include "InputBroker.h"
#include "LinuxJoystick.h"
#include "platform/portduino/PortduinoGlue.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

LinuxJoystick *aLinuxJoystick;

LinuxJoystick::LinuxJoystick(const char *name) : concurrency::OSThread(name)
{
    this->_originName = name;
}

// Translate a config.yaml action name into a broker event. Returns INPUT_BROKER_NONE
// for unknown names so a typo simply leaves that button unmapped.
static input_broker_event joystickActionToEvent(const std::string &action)
{
    if (action == "select")
        return INPUT_BROKER_SELECT;
    if (action == "cancel")
        return INPUT_BROKER_CANCEL;
    if (action == "back")
        return INPUT_BROKER_BACK;
    if (action == "up")
        return INPUT_BROKER_UP;
    if (action == "down")
        return INPUT_BROKER_DOWN;
    if (action == "left")
        return INPUT_BROKER_LEFT;
    if (action == "right")
        return INPUT_BROKER_RIGHT;
    if (action == "user" || action == "userpress")
        return INPUT_BROKER_USER_PRESS;
    return INPUT_BROKER_NONE;
}

void LinuxJoystick::init()
{
    if (portduino_config.joystickButtons.empty()) {
        // No config override: fall back to the built-in defaults.
        buttonMap[JOY_BTN_A] = INPUT_BROKER_SELECT;
        buttonMap[JOY_BTN_B] = INPUT_BROKER_CANCEL;
    } else {
        for (const auto &button : portduino_config.joystickButtons) {
            input_broker_event event = joystickActionToEvent(button.second);
            if (event != INPUT_BROKER_NONE)
                buttonMap[button.first] = event;
        }
    }
    inputBroker->registerSource(this);
}

void LinuxJoystick::deInit()
{
    // reboot() on native does execv() in-place, which only closes O_CLOEXEC fds.
    // Close both descriptors here so we don't leak one per restart.
    if (fd >= 0)
        close(fd);
    if (epollfd >= 0)
        close(epollfd);
    fd = -1;
    epollfd = -1;
}

// Bucket a raw axis value into a direction zone: -1 (min edge), 0 (center), +1 (max edge).
static int axisZone(int value)
{
    if (value <= JOY_AXIS_LOW)
        return -1;
    if (value >= JOY_AXIS_HIGH)
        return 1;
    return 0;
}

void LinuxJoystick::emitEvent(input_broker_event event)
{
    InputEvent e = {};
    e.inputEvent = event;
    e.source = this->_originName;
    e.kbchar = 0;
    // LOG_DEBUG("joystick: %s event %d", this->_originName, event);
    this->notifyObservers(&e);
}

int32_t LinuxJoystick::runOnce()
{
    if (firstTime) {
        if (portduino_config.joystickDevice == "")
            return disable();
        fd = open(portduino_config.joystickDevice.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0)
            return disable();
        if (ioctl(fd, EVIOCGRAB, (void *)1) != 0)
            return disable();

        epollfd = epoll_create1(EPOLL_CLOEXEC);
        assert(epollfd >= 0);

        ev.events = EPOLLIN;
        ev.data.fd = fd;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev)) {
            perror("joystick: unable to epoll add");
            return disable();
        }
        // This is the first time the OSThread library has called this function, so do port setup
        firstTime = false;
    }

    int nfds = epoll_wait(epollfd, events, JOY_MAX_EVENTS, 1);
    if (nfds < 0) {
        perror("joystick: epoll_wait failed");
        return disable();
    }

    for (int i = 0; i < nfds; i++) {
        struct input_event evs[64];
        int rd = read(events[i].data.fd, evs, sizeof(evs));
        if (rd < (signed int)sizeof(struct input_event))
            continue;
        for (int j = 0; j < rd / ((signed int)sizeof(struct input_event)); j++) {
            unsigned int type = evs[j].type;
            unsigned int code = evs[j].code;
            int value = evs[j].value;

            if (type == EV_ABS) {
                // D-pad reports as ABS_X / ABS_Y with digital 0 / 127 / 255 values. Emit on the
                // transition to an edge and arm auto-repeat; a held direction repeats below.
                if (code == ABS_X) {
                    int zone = axisZone(value);
                    if (zone != heldX) {
                        heldX = zone;
                        if (zone != 0) {
                            emitEvent((zone < 0) ? INPUT_BROKER_LEFT : INPUT_BROKER_RIGHT);
                            nextRepeatX = millis() + JOY_REPEAT_DELAY_MS;
                        }
                    }
                } else if (code == ABS_Y) {
                    int zone = axisZone(value);
                    if (zone != heldY) {
                        heldY = zone;
                        if (zone != 0) {
                            emitEvent((zone < 0) ? INPUT_BROKER_UP : INPUT_BROKER_DOWN);
                            nextRepeatY = millis() + JOY_REPEAT_DELAY_MS;
                        }
                    }
                }
            } else if (type == EV_KEY && value == 1) {
                // Look up the configured action for this button; unmapped buttons are ignored.
                // Buttons fire once per press (no auto-repeat).
                auto mapped = buttonMap.find(code);
                if (mapped != buttonMap.end())
                    emitEvent(mapped->second);
            }
        }
    }

    // Auto-repeat held D-pad directions. Signed comparison is wraparound-safe.
    uint32_t now = millis();
    if (heldX != 0 && (int32_t)(now - nextRepeatX) >= 0) {
        emitEvent((heldX < 0) ? INPUT_BROKER_LEFT : INPUT_BROKER_RIGHT);
        nextRepeatX = now + JOY_REPEAT_INTERVAL_MS;
    }
    if (heldY != 0 && (int32_t)(now - nextRepeatY) >= 0) {
        emitEvent((heldY < 0) ? INPUT_BROKER_UP : INPUT_BROKER_DOWN);
        nextRepeatY = now + JOY_REPEAT_INTERVAL_MS;
    }

    return 50; // Poll every 50msec
}

#endif
