#include "ConfigCheckInput.h"

#ifndef ARCH_PORTDUINO_WASM

#include "configuration.h"

#include "PortduinoGlue.h"

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__linux__)
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/input.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace configcheck
{

namespace
{

const char kOtherHost[] = ". Expected if you are checking this config on a different host";

const char *roleKey(InputRole role)
{
    switch (role) {
    case InputRole::kKeyboard:
        return "Input.KeyboardDevice";
    case InputRole::kPointer:
        return "Input.PointerDevice";
    default:
        return "Input.JoystickDevice";
    }
}

bool isBareEventNode(const std::string &path)
{
    static const std::string prefix = "/dev/input/event";
    return path.rfind(prefix, 0) == 0 && path.find_first_not_of("0123456789", prefix.size()) == std::string::npos &&
           path.size() > prefix.size();
}

bool isEvdevNode(const std::string &resolved)
{
    return isBareEventNode(resolved);
}

#if defined(__linux__)

constexpr const char *kByIdDir = "/dev/input/by-id/";

std::string userName(uid_t uid)
{
    const passwd *pw = getpwuid(uid);
    return pw ? pw->pw_name : std::to_string(uid);
}

std::string groupName(gid_t gid)
{
    const group *gr = getgrgid(gid);
    return gr ? gr->gr_name : std::to_string(gid);
}

bool inGroup(gid_t gid)
{
    if (geteuid() == 0 || getegid() == gid)
        return true;
    gid_t groups[NGROUPS_MAX];
    const int count = getgroups(NGROUPS_MAX, groups);
    for (int i = 0; i < count; i++)
        if (groups[i] == gid)
            return true;
    return false;
}

std::string realPathOf(const std::string &path)
{
    char buffer[PATH_MAX];
    return realpath(path.c_str(), buffer) ? std::string(buffer) : std::string();
}

std::vector<std::string> byIdEntries()
{
    std::vector<std::string> names;
    if (DIR *dir = opendir(kByIdDir)) {
        while (const dirent *entry = readdir(dir))
            if (entry->d_name[0] != '.')
                names.push_back(entry->d_name);
        closedir(dir);
    }
    return names;
}

bool testBit(const uint8_t *bits, unsigned bit)
{
    return bits[bit / 8] & (1u << (bit % 8));
}

bool reportsLetterKeys(const uint8_t *keyBits)
{
    static const unsigned letters[] = {KEY_A, KEY_E, KEY_M, KEY_Q, KEY_Z};
    for (unsigned key : letters)
        if (!testBit(keyBits, key))
            return false;
    return true;
}

// A by-id sibling of this node (same device, other USB interface) that reports letter keys.
std::string findLetterSibling(const std::string &path, const std::string &resolved)
{
    const std::string base = path.substr(path.find_last_of('/') + 1);
    const size_t cut = base.find("-event");
    if (cut == std::string::npos)
        return "";
    const std::string prefix = base.substr(0, cut);
    for (const auto &name : byIdEntries()) {
        if (name.rfind(prefix, 0) != 0 || name.find("-event") == std::string::npos)
            continue;
        const std::string candidate = kByIdDir + name;
        if (realPathOf(candidate) == resolved)
            continue;
        if (probeInputDevice(candidate, false).hasLetters)
            return candidate;
    }
    return "";
}

std::string findStableAlias(const std::string &resolved)
{
    for (const auto &name : byIdEntries())
        if (realPathOf(kByIdDir + name) == resolved && name.find("-event") != std::string::npos)
            return kByIdDir + name;
    return "";
}

#endif // __linux__

std::string octal(unsigned mode)
{
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%04o", mode & 07777);
    return buffer;
}

} // namespace

InputFacts probeInputDevice(const std::string &path, bool tryGrab)
{
    InputFacts facts;
#if defined(__linux__)
    struct stat st = {};
    if (stat(path.c_str(), &st) != 0)
        return facts;

    facts.exists = true;
    facts.charDevice = S_ISCHR(st.st_mode);
    facts.resolved = realPathOf(path);
    facts.mode = st.st_mode;
    facts.owner = userName(st.st_uid);
    facts.group = groupName(st.st_gid);
    facts.caller = userName(geteuid());
    facts.callerInGroup = inGroup(st.st_gid);

    // Same flags as LinuxInput, so a failure here is the failure the daemon would hit.
    int fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd >= 0) {
        facts.openOk = true;
    } else {
        facts.openErrno = errno;
        fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        facts.readOnlyOk = fd >= 0;
    }
    if (fd < 0)
        return facts;

    if (tryGrab) {
        facts.grabProbed = true;
        if (ioctl(fd, EVIOCGRAB, (void *)1) == 0) {
            facts.grabOk = true;
            ioctl(fd, EVIOCGRAB, (void *)0);
        } else {
            facts.grabErrno = errno;
        }
    }

    char name[256] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0)
        facts.name = name;

    input_id id = {};
    if (ioctl(fd, EVIOCGID, &id) == 0) {
        facts.idKnown = true;
        facts.vendor = id.vendor;
        facts.product = id.product;
    }

    uint8_t eventTypes[EV_MAX / 8 + 1] = {};
    uint8_t keyBits[KEY_MAX / 8 + 1] = {};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(eventTypes)), eventTypes) >= 0 &&
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0) {
        facts.capsKnown = true;
        facts.hasLetters = testBit(eventTypes, EV_KEY) && reportsLetterKeys(keyBits);
        facts.hasPointerAxes = testBit(eventTypes, EV_REL) || testBit(eventTypes, EV_ABS);
        facts.hasMouseButton = testBit(keyBits, BTN_MOUSE);
    }
    close(fd);

    if (isBareEventNode(path))
        facts.stableAlias = findStableAlias(facts.resolved);
    if (facts.capsKnown && !facts.hasLetters)
        facts.letterSibling = findLetterSibling(path, facts.resolved);
#else
    (void)path;
    (void)tryGrab;
#endif
    return facts;
}

std::string describeNodeAccess(const std::string &path)
{
    const InputFacts facts = probeInputDevice(path, false);
    if (!facts.exists)
        return "";
    return facts.owner + ":" + facts.group + " mode " + octal(facts.mode) + ", checked as user '" + facts.caller + "'" +
           (facts.callerInGroup ? "" : " (not in group '" + facts.group + "')");
}

std::vector<Finding> judgeInputDevice(InputRole role, const std::string &key, const std::string &path, const InputFacts &facts)
{
    const std::string merged = "(merged configuration)";
    const std::string subject = key + " '" + path + "'";
    std::vector<Finding> out;

    if (!facts.exists) {
        out.push_back({kWarn, merged, 0,
                       subject + " does not exist on this machine, so meshtasticd silently opens no input device" + kOtherHost});
        return out;
    }
    if (!facts.charDevice) {
        out.push_back({kWarn, merged, 0, subject + " is not a device node, so meshtasticd cannot read input events from it"});
        return out;
    }
    if (!isEvdevNode(facts.resolved))
        out.push_back({kWarn, merged, 0,
                       subject + " resolves to " + facts.resolved +
                           ", which is not an evdev node (/dev/input/eventN). meshtasticd reads evdev events, so use the "
                           "-event-* entry in /dev/input/by-id"});

    if (!facts.stableAlias.empty())
        out.push_back(
            {kWarn, merged, 0,
             subject + " is a bare eventN number, which changes with USB order and between boots. Use " + facts.stableAlias});

    if (!facts.openOk) {
        std::string why = facts.openErrno == EACCES || facts.openErrno == EPERM ? "permission denied" : strerror(facts.openErrno);
        std::string message = subject + " cannot be opened read/write (" + why + "), so meshtasticd silently disables it. " +
                              facts.owner + ":" + facts.group + " mode " + octal(facts.mode) + ", checked as user '" +
                              facts.caller + "'";
        if (!facts.callerInGroup)
            message += ". Add that user to group '" + facts.group + "' (usermod -aG " + facts.group + " <user>) and log in again";
        if (facts.readOnlyOk)
            message += ". Read-only access works, but meshtasticd opens it read/write";
        out.push_back({kWarn, merged, 0, message + ". A service may run as a different user than this check"});
        if (!facts.readOnlyOk)
            return out;
    }

    // LinuxInput and LinuxJoystick take the grab; the pointer is read non-exclusively by the display driver.
    if (facts.grabProbed && !facts.grabOk)
        out.push_back({kWarn, merged, 0,
                       subject + " cannot be grabbed exclusively (" + strerror(facts.grabErrno) +
                           "), so meshtasticd cannot read it. If meshtasticd is already running it holds the grab; "
                           "otherwise find the holder with 'fuser -v " +
                           facts.resolved + "'"});

    if (!facts.name.empty() || facts.idKnown) {
        char ids[16] = "";
        if (facts.idKnown)
            snprintf(ids, sizeof(ids), "%04x:%04x", facts.vendor, facts.product);
        out.push_back({kInfo, merged, 0, std::string(roleKey(role)) + " is " + facts.resolved + ": '" + facts.name + "' " + ids});
    }

    if (!facts.capsKnown)
        return out;

    if (role == InputRole::kKeyboard && !facts.hasLetters) {
        std::string message = subject + " reports no letter keys (KEY_A..KEY_Z), so typing will not work from it";
        if (!facts.letterSibling.empty())
            message += ". The same device exposes " + facts.letterSibling + ", which does report letter keys";
        out.push_back({kWarn, merged, 0, message});
    }
    if (role == InputRole::kPointer && !facts.hasPointerAxes && !facts.hasMouseButton)
        out.push_back({kWarn, merged, 0, subject + " reports no pointer axes or buttons, so it will not move a cursor"});
    return out;
}

void checkInputDevices(std::vector<Finding> &findings)
{
    struct Entry {
        InputRole role;
        std::string path;
    };
    const std::vector<Entry> entries = {{InputRole::kKeyboard, portduino_config.keyboardDevice},
                                        {InputRole::kPointer, portduino_config.pointerDevice},
                                        {InputRole::kJoystick, portduino_config.joystickDevice}};

#if !defined(__linux__)
    for (const auto &entry : entries)
        if (!entry.path.empty())
            findings.push_back({kInfo, "(merged configuration)", 0,
                                std::string(roleKey(entry.role)) + " is set, but device checks need Linux evdev"});
    return;
#else
    bool probed = false;
    std::string keyboardNode, joystickNode;
    for (const auto &entry : entries) {
        if (entry.path.empty())
            continue;
        probed = true;
        const InputFacts facts = probeInputDevice(entry.path, entry.role != InputRole::kPointer);
        for (auto &finding : judgeInputDevice(entry.role, roleKey(entry.role), entry.path, facts))
            findings.push_back(std::move(finding));
        if (entry.role == InputRole::kKeyboard)
            keyboardNode = facts.resolved;
        if (entry.role == InputRole::kJoystick)
            joystickNode = facts.resolved;
    }

    if (!keyboardNode.empty() && keyboardNode == joystickNode)
        findings.push_back({kWarn, "(merged configuration)", 0,
                            "Input.KeyboardDevice and Input.JoystickDevice are the same node (" + keyboardNode +
                                "). Both readers grab it exclusively, so only one gets events"});

    if (probed)
        findings.push_back({kInfo, "(merged configuration)", 0,
                            "Input devices were probed as user '" + userName(geteuid()) +
                                "'. If meshtasticd runs as another user (a systemd User=, for instance), its access differs"});
#endif
}

} // namespace configcheck

#endif // !ARCH_PORTDUINO_WASM
