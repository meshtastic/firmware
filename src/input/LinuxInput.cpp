#include "configuration.h"
#if ARCH_PORTDUINO
#include "LinuxInput.h"
#include "platform/portduino/PortduinoGlue.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Inspired by https://github.com/librerpi/rpi-tools/blob/master/keyboard-proxy/main.c which is GPL-v2

LinuxInput::LinuxInput(const char *name) : concurrency::OSThread(name)
{
    this->_originName = name;
}

void LinuxInput::deInit()
{
    if (fd >= 0)
        close(fd);
}

int32_t LinuxInput::runOnce()
{

    if (firstTime) {
        if (settingsStrings[keyboardDevice] == "")
            return disable();
        fd = open(settingsStrings[keyboardDevice].c_str(), O_RDWR);
        if (fd < 0)
            return disable();
        ret = ioctl(fd, EVIOCGRAB, (void *)1);
        if (ret != 0)
            return disable();

        epollfd = epoll_create1(0);
        assert(epollfd >= 0);

        ev.events = EPOLLIN;
        ev.data.fd = fd;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev)) {
            perror("unable to epoll add");
            return disable();
        }
        // This is the first time the OSThread library has called this function, so do port setup
        firstTime = 0;
    }

    int nfds = epoll_wait(epollfd, events, MAX_EVENTS, 1);
    if (nfds < 0) {
        printf("%d ", nfds);
        perror("epoll_wait failed");
        return disable();
    } else if (nfds == 0) {
        return 50;
    }

    int keys = 0;
    memset(report, 0, 8);
    for (int i = 0; i < nfds; i++) {

        struct input_event ev[64];
        int rd = read(events[i].data.fd, ev, sizeof(ev));
        assert(rd > ((signed int)sizeof(struct input_event)));
        for (int j = 0; j < rd / ((signed int)sizeof(struct input_event)); j++) {
            InputEvent e;
            e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
            e.source = this->_originName;
            e.kbchar = 0;
            unsigned int type, code;
            type = ev[j].type;
            code = ev[j].code;
            int value = ev[j].value;
            // printf("Event: time %ld.%06ld, ", ev[j].time.tv_sec, ev[j].time.tv_usec);

            if (type == EV_KEY) {
                uint8_t mod = 0;

                switch (code) {
                case KEY_LEFTCTRL:
                    mod = 0x01;
                    break;
                case KEY_RIGHTCTRL:
                    mod = 0x10;
                    break;
                case KEY_LEFTSHIFT:
                    mod = 0x02;
                    break;
                case KEY_RIGHTSHIFT:
                    mod = 0x20;
                    break;
                case KEY_LEFTALT:
                    mod = 0x04;
                    break;
                case KEY_RIGHTALT:
                    mod = 0x40;
                    break;
                case KEY_LEFTMETA:
                    mod = 0x08;
                    break;
                }
                if (value == 1) {
                    switch (code) {
                    case KEY_LEFTCTRL:
                        mod = 0x01;
                        break;
                    case KEY_RIGHTCTRL:
                        mod = 0x10;
                        break;
                    case KEY_LEFTSHIFT:
                        mod = 0x02;
                        break;
                    case KEY_RIGHTSHIFT:
                        mod = 0x20;
                        break;
                    case KEY_LEFTALT:
                        mod = 0x04;
                        break;
                    case KEY_RIGHTALT:
                        mod = 0x40;
                        break;
                    case KEY_LEFTMETA:
                        mod = 0x08;
                        break;
                    case KEY_ESC: // ESC
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL;
                        break;
                    case KEY_BACK: // Back
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_BACK;
                        // e.kbchar = key;
                        break;

                    case KEY_UP: // Up
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP;
                        break;
                    case KEY_DOWN: // Down
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN;
                        break;
                    case KEY_LEFT: // Left
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT;
                        break;
                        e.kbchar = 0xb4;
                    case KEY_RIGHT: // Right
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT;
                        break;
                        e.kbchar = 0xb7;
                    case KEY_ENTER: // Enter
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT;
                        break;
                    case KEY_POWER:
                        system("poweroff");
                        break;
                    default: // all other keys
                        if (keymap[code]) {
                            e.inputEvent = ANYKEY;
                            e.kbchar = keymap[code];
                        }
                        break;
                    }
                }
                if (ev[j].value) {
                    modifiers |= mod;
                } else {
                    modifiers &= ~mod;
                }
                report[0] = modifiers;
            }
            if (e.inputEvent != meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE) {
                if (e.inputEvent == ANYKEY && (modifiers && 0x22))
                    e.kbchar = uppers[e.kbchar]; // doesn't get punctuation. Meh.
                this->notifyObservers(&e);
            }
        }
    }

    return 50; // Keyscan every 50msec to avoid key bounce
}

#endif