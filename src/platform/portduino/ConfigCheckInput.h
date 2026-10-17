#pragma once

#ifndef ARCH_PORTDUINO_WASM

#include "ConfigCheckFinding.h"

#include <string>
#include <vector>

namespace configcheck
{

enum class InputRole { kKeyboard, kPointer, kJoystick };

// What probing a device node found. Gathered by probeInputDevice(), judged by judgeInputDevice().
struct InputFacts {
    bool exists = false;
    bool charDevice = false;
    std::string resolved;    // realpath of the configured path
    std::string stableAlias; // a /dev/input/by-id name for the same node, when the path is a bare eventN
    std::string owner, group;
    unsigned mode = 0;
    bool openOk = false;
    int openErrno = 0;
    bool readOnlyOk = false; // O_RDWR failed but O_RDONLY worked
    bool grabProbed = false;
    bool grabOk = false;
    int grabErrno = 0;
    std::string name;
    unsigned vendor = 0, product = 0;
    bool idKnown = false;
    bool capsKnown = false;
    bool hasLetters = false;
    bool hasPointerAxes = false; // EV_REL or EV_ABS
    bool hasMouseButton = false;
    std::string letterSibling; // a by-id sibling of this node that does report letter keys
    std::string caller;        // user the probe ran as
    bool callerInGroup = false;
};

InputFacts probeInputDevice(const std::string &path, bool tryGrab);

std::vector<Finding> judgeInputDevice(InputRole role, const std::string &key, const std::string &path, const InputFacts &facts);

// "owner:group mode, checked as user" for a node the caller cannot open, empty when it cannot be stat'ed.
std::string describeNodeAccess(const std::string &path);

// Probes Input.KeyboardDevice, Input.PointerDevice and Input.JoystickDevice on this host.
void checkInputDevices(std::vector<Finding> &findings);

} // namespace configcheck

#endif // !ARCH_PORTDUINO_WASM
