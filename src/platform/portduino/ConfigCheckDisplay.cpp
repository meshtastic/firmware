#include "ConfigCheckDisplay.h"

#ifndef ARCH_PORTDUINO_WASM

#include "configuration.h"

#include "ConfigCheckInput.h"
#include "PortduinoGlue.h"

#include <algorithm>
#include <cctype>
#include <set>

#if defined(__linux__)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace configcheck
{

namespace
{

const char kOtherHost[] = ". Expected if you are checking this config on a different host";
const char kMerged[] = "(merged configuration)";

const std::set<std::string> &touchModules()
{
    static const std::set<std::string> names = {"XPT2046", "STMPE610", "GT911", "FT5x06"};
    return names;
}

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

// Returns the spelling in names that matches text ignoring case, or "".
std::string caseVariant(const std::set<std::string> &names, const std::string &text)
{
    for (const auto &name : names)
        if (lower(name) == lower(text))
            return name;
    return "";
}

std::string joined(const std::set<std::string> &names)
{
    std::string out;
    for (const auto &name : names)
        out += (out.empty() ? "" : ", ") + name;
    return out;
}

bool isSpiPanel(screen_modules panel)
{
    return panel >= st7789 && panel <= hx8357d;
}

// loadConfig() decodes /dev/spidevB.C by position; any other length leaves the bus number at 0.
bool spidevNameDecodes(const std::string &path)
{
    return path.length() == 14 && std::isdigit(static_cast<unsigned char>(path[11])) &&
           std::isdigit(static_cast<unsigned char>(path[13]));
}

void checkSpiNode(const std::string &key, const std::string &path, std::vector<Finding> &findings)
{
    if (!spidevNameDecodes(path))
        findings.push_back(
            {kWarn, kMerged, 0, key + " '" + path.substr(5) + "' is not of the form spidevB.C, so the SPI bus number stays 0"});
#if defined(__linux__)
    struct stat st = {};
    if (stat(path.c_str(), &st) != 0) {
        findings.push_back({kWarn, kMerged, 0,
                            key + " resolves to " + path +
                                ", which does not exist on this machine. Enable SPI (raspi-config or "
                                "dtparam=spi=on) so the node appears" +
                                kOtherHost});
    } else if (access(path.c_str(), R_OK | W_OK) != 0) {
        findings.push_back({kWarn, kMerged, 0,
                            key + " resolves to " + path + ", which this user cannot open read/write (" +
                                describeNodeAccess(path) + "). Add the user to the spi group"});
    }
#endif
}

} // namespace

void checkDisplayPanelName(const std::string &file, int line, const std::string &name, std::vector<Finding> &findings)
{
    if (name.empty())
        return;
    std::set<std::string> known;
    for (const auto &entry : portduino_config.screen_names)
        known.insert(entry.second);
    if (known.count(name))
        return;

    std::string message = "Display.Panel '" + name + "' is not a known panel, so meshtasticd runs with no display";
    const std::string variant = caseVariant(known, name);
    if (!variant.empty())
        message += ". Names are case-sensitive; did you mean '" + variant + "'?";
    else
        message += ". Known panels: " + joined(known);
    findings.push_back({kError, file, line, message});
}

void checkTouchscreenModuleName(const std::string &file, int line, const std::string &name, std::vector<Finding> &findings)
{
    if (name.empty() || touchModules().count(name))
        return;

    std::string message = "Touchscreen.Module '" + name + "' is not a known controller, so meshtasticd runs with no touchscreen";
    const std::string variant = caseVariant(touchModules(), name);
    if (!variant.empty())
        message += ". Names are case-sensitive; did you mean '" + variant + "'?";
    else
        message += ". Known controllers: " + joined(touchModules());
    findings.push_back({kWarn, file, line, message});
}

void checkDisplay(std::vector<Finding> &findings)
{
    const screen_modules panel = portduino_config.displayPanel;

    if (isSpiPanel(panel)) {
        if (portduino_config.displayWidth <= 0 || portduino_config.displayHeight <= 0)
            findings.push_back({kError, kMerged, 0,
                                "Display.Width and Display.Height must both be set for an SPI panel; they are " +
                                    std::to_string(portduino_config.displayWidth) + " and " +
                                    std::to_string(portduino_config.displayHeight) + ", so the panel cannot be initialised"});
        if (!portduino_config.displayDC.enabled)
            findings.push_back(
                {kWarn, kMerged, 0, "Display.DC is not set. SPI panels need a data/command pin, so the panel stays blank"});
        if (portduino_config.displayOffsetRotate < 0 || portduino_config.displayOffsetRotate > 3)
            findings.push_back({kWarn, kMerged, 0,
                                "Display.OffsetRotate is " + std::to_string(portduino_config.displayOffsetRotate) +
                                    "; it selects 0, 90, 180 or 270 degrees as 0 to 3"});
        if (!portduino_config.display_spi_dev.empty())
            checkSpiNode("Display.spidev", portduino_config.display_spi_dev, findings);

        const std::string &loraNode = portduino_config.lora_spi_dev;
        if (!portduino_config.display_spi_dev.empty() && loraNode != "ch341" && loraNode == portduino_config.display_spi_dev)
            findings.push_back({kWarn, kMerged, 0,
                                "Display.spidev and Lora.spidev are both " + loraNode +
                                    ", so the display and the radio contend for one chip select"});
    }

    if (panel == x11 || panel == fb) {
        const char *name = panel == x11 ? "X11" : "FB";
        if (!portduino_config.pointerDevice.empty())
            findings.push_back({kWarn, kMerged, 0,
                                "Input.PointerDevice is not passed to the " + std::string(name) +
                                    " display driver, which reads its own pointer, so this line does nothing"});
        if (!portduino_config.keyboardDevice.empty())
            findings.push_back({kInfo, kMerged, 0,
                                "Input.KeyboardDevice is read by meshtasticd's own input handler, not by the " +
                                    std::string(name) + " display driver"});
    }

    const touchscreen_modules touch = portduino_config.touchscreenModule;
    if (touch != no_touchscreen) {
        const bool i2cPart = touch == gt911 || touch == ft5x06;
        const bool hasI2cAddr = portduino_config.touchscreenI2CAddr != -1;
        // tftSetup.cpp picks the bus from I2CAddr alone, whatever the module is.
        if (i2cPart && !hasI2cAddr)
            findings.push_back({kWarn, kMerged, 0,
                                "Touchscreen.Module is an I2C controller but Touchscreen.I2CAddr is not set, so it is configured "
                                "as an SPI device"});
        if (!i2cPart && hasI2cAddr)
            findings.push_back({kWarn, kMerged, 0,
                                "Touchscreen.Module is an SPI controller but Touchscreen.I2CAddr is set, so it is configured "
                                "as an I2C device"});
        if (panel == no_screen)
            findings.push_back({kWarn, kMerged, 0, "Touchscreen.Module is set but there is no Display.Panel, so it is not used"});
    }
}

} // namespace configcheck

#endif // !ARCH_PORTDUINO_WASM
