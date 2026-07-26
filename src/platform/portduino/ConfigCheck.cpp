#include "ConfigCheck.h"

#ifndef ARCH_PORTDUINO_WASM

#include "configuration.h"

#include "PortduinoGlue.h"

#include "yaml-cpp/eventhandler.h"

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------
// These tables mirror the keys loadConfig() actually reads in PortduinoGlue.cpp,
// plus the pinMapping{section, name} members declared in PortduinoGlue.h. A key
// missing from here is reported as unknown, which is the honest answer: meshtasticd
// silently ignores anything it does not read, so an unlisted key does nothing.
//
// When you teach loadConfig() a new key, add it here too. CI runs --check over
// bin/config.d/**, so an omission surfaces as a failing build rather than as a
// misleading "unknown key" warning in front of a user.

const std::set<std::string> kLoraPinKeys = {"CS", "IRQ", "Busy", "Reset", "TXen", "RXen", "SX126X_ANT_SW", "GPIO_DETECT_PA"};

const std::map<std::string, std::set<std::string>> &schema()
{
    static const std::map<std::string, std::set<std::string>> s = {
        {"Lora",
         {"Module",
          "gpiochip",
          "spidev",
          "spiSpeed",
          "DIO2_AS_RF_SWITCH",
          "DIO3_TCXO_VOLTAGE",
          "Enable_Pins",
          "rfswitch_table",
          "LR1110_MAX_POWER",
          "LR1120_MAX_POWER",
          "RF95_MAX_POWER",
          "SX126X_MAX_POWER",
          "SX128X_MAX_POWER",
          "TX_GAIN_LORA",
          "USB_PID",
          "USB_VID",
          "USB_Serialnum",
          "CS",
          "IRQ",
          "Busy",
          "Reset",
          "TXen",
          "RXen",
          "SX126X_ANT_SW",
          "GPIO_DETECT_PA"}},
        {"General",
         {"MACAddress", "MACAddressSource", "MaxNodes", "MaxMessageQueue", "APIPort", "ConfigDirectory", "AvailableDirectory"}},
        {"Config", {"DisplayMode", "EnableUDP", "StatusMessage"}},
        {"Display",
         {"Panel", "spidev", "BusFrequency", "Width", "Height", "Invert", "Rotate", "OffsetX", "OffsetY", "OffsetRotate",
          "RGBOrder", "HUB75", "DC", "CS", "Backlight", "BacklightInvert", "BacklightPWMChannel", "Reset"}},
        {"Touchscreen", {"Module", "spidev", "BusFrequency", "I2CAddr", "Rotate", "CS", "IRQ"}},
        {"Input",
         {"KeyboardDevice", "PointerDevice", "JoystickDevice", "JoystickButtons", "TrackballDirection", "User", "TrackballUp",
          "TrackballDown", "TrackballLeft", "TrackballRight", "TrackballPress"}},
        {"GPIO", {"User", "ExtraPins"}},
        {"GPS", {"SerialPath", "GpsdHost", "GpsdPort"}},
        {"I2C", {"I2CDevice"}},
        {"Logging", {"LogLevel", "TraceFile", "JSONFile", "JSONFileRotate", "JSONFilter", "AsciiLogs"}},
        {"Webserver", {"Port", "RootPath", "SSLCert", "SSLKey"}},
        {"HostMetrics", {"ReportInterval", "Channel", "UserStringCommand"}},
        // Read by packaging/menu tooling rather than by meshtasticd itself.
        {"Meta", {}},
    };
    return s;
}

// Sections whose contents meshtasticd never reads, so unknown keys inside them are
// not worth reporting.
const std::set<std::string> kFreeFormSections = {"Meta"};

const std::set<std::string> kPinSubKeys = {"pin", "gpiochip", "line"};

const std::set<std::string> kHub75Keys = {
    "HardwareMapping",   "Rows",          "Cols",        "ChainLength",    "Parallel",     "PWMBits",
    "PWMLSBNanoseconds", "Brightness",    "ScanMode",    "RowAddressType", "Multiplexing", "DisableHardwarePulsing",
    "ShowRefreshRate",   "InverseColors", "RGBSequence", "PixelMapper",    "PanelType",    "LimitRefreshRateHz",
    "GPIOSlowdown"};

const std::set<std::string> kRfSwitchModes = {"MODE_STBY",  "MODE_RX",   "MODE_TX",  "MODE_TX_HP",
                                              "MODE_TX_HF", "MODE_GNSS", "MODE_WIFI"};

const std::set<std::string> kRfSwitchPins = {"DIO5", "DIO6", "DIO7", "DIO8", "DIO10"};

// Reverse index: key name -> sections it is valid in. Powers the "you probably meant
// to nest this under X" hint that turns a silent no-op into an actionable message.
const std::map<std::string, std::set<std::string>> &keyOwners()
{
    static const std::map<std::string, std::set<std::string>> owners = [] {
        std::map<std::string, std::set<std::string>> m;
        for (const auto &section : schema())
            for (const auto &key : section.second)
                m[key].insert(section.first);
        // rfswitch_table's sub-keys are worth hinting on too: a table indented one
        // level too far leaves MODE_* rows stranded directly under Lora.
        for (const auto &mode : kRfSwitchModes)
            m[mode].insert("Lora.rfswitch_table");
        m["pins"].insert("Lora.rfswitch_table");
        return m;
    }();
    return owners;
}

// ---------------------------------------------------------------------------
// Findings
// ---------------------------------------------------------------------------

enum Level { kInfo, kWarn, kError };

struct Finding {
    Level level;
    std::string file;
    int line; // 1-based; 0 when the finding is not tied to a line
    std::string message;
};

const char *levelName(Level l)
{
    switch (l) {
    case kError:
        return "ERROR";
    case kWarn:
        return "WARN ";
    default:
        return "note ";
    }
}

std::string joinSections(const std::set<std::string> &s)
{
    std::string out;
    for (const auto &item : s) {
        if (!out.empty())
            out += " or ";
        out += item;
    }
    return out;
}

int lineOf(const YAML::Node &node)
{
    const YAML::Mark mark = node.Mark();
    return mark.is_null() ? 0 : mark.line + 1;
}

// ---------------------------------------------------------------------------
// Duplicate key detection
// ---------------------------------------------------------------------------
// YAML forbids duplicate keys but yaml-cpp does not complain: it silently keeps the
// FIRST occurrence and discards the rest, so a later override looks applied but is
// not. The Node API cannot see this because the map is already collapsed by the time
// it exists, so walk the raw parser event stream instead.

class DuplicateKeyFinder : public YAML::EventHandler
{
  public:
    struct Duplicate {
        std::string path;
        int line;
        int firstLine;
    };
    std::vector<Duplicate> duplicates;

    void OnDocumentStart(const YAML::Mark &) override {}
    void OnDocumentEnd() override {}
    void OnNull(const YAML::Mark &, YAML::anchor_t) override { advance(); }
    void OnAlias(const YAML::Mark &, YAML::anchor_t) override { advance(); }
    void OnAnchor(const YAML::Mark &, const std::string &) override {}

    void OnScalar(const YAML::Mark &mark, const std::string &, YAML::anchor_t, const std::string &value) override
    {
        if (!stack.empty() && stack.back().isMap && stack.back().expectKey) {
            auto &seen = stack.back().seen;
            const auto existing = seen.find(value);
            if (existing != seen.end())
                duplicates.push_back({pathTo(value), static_cast<int>(mark.line) + 1, existing->second});
            else
                seen.emplace(value, static_cast<int>(mark.line) + 1);
            stack.back().key = value;
        }
        advance();
    }

    void OnSequenceStart(const YAML::Mark &, const std::string &, YAML::anchor_t, YAML::EmitterStyle::value) override
    {
        push(false);
    }
    void OnSequenceEnd() override { pop(); }
    void OnMapStart(const YAML::Mark &, const std::string &, YAML::anchor_t, YAML::EmitterStyle::value) override { push(true); }
    void OnMapEnd() override { pop(); }

  private:
    struct Context {
        bool isMap;
        bool expectKey;
        std::string key;
        std::map<std::string, int> seen;
    };
    std::vector<Context> stack;

    // Inside a mapping the parser alternates key, value, key, value...
    void advance()
    {
        if (!stack.empty() && stack.back().isMap)
            stack.back().expectKey = !stack.back().expectKey;
    }
    void push(bool isMap)
    {
        advance(); // the collection itself occupies a slot in its parent
        stack.push_back(Context{isMap, true, "", {}});
    }
    // No advance() here: push() already consumed the collection's slot in the parent.
    // Toggling again would leave the parent expecting a value, so the next key would
    // not be recognised as one.
    void pop() { stack.pop_back(); }
    std::string pathTo(const std::string &leaf) const
    {
        std::string path;
        for (size_t i = 0; i + 1 < stack.size(); i++)
            if (stack[i].isMap && !stack[i].key.empty())
                path += stack[i].key + ".";
        return path + leaf;
    }
};

void checkDuplicateKeys(const std::string &file, std::vector<Finding> &findings)
{
    std::ifstream stream(file);
    if (!stream)
        return;
    DuplicateKeyFinder finder;
    try {
        YAML::Parser parser(stream);
        while (parser.HandleNextDocument(finder)) {
        }
    } catch (const std::exception &) {
        return; // reported separately by checkFile()
    }
    for (const auto &dup : finder.duplicates)
        findings.push_back({kError, file, dup.line,
                            "duplicate key '" + dup.path + "' (first defined on line " + std::to_string(dup.firstLine) +
                                "). yaml-cpp keeps the FIRST occurrence, so this one is silently discarded"});
}

// ---------------------------------------------------------------------------
// Structural checks
// ---------------------------------------------------------------------------

void checkPinNode(const std::string &file, const std::string &path, const YAML::Node &node, std::vector<Finding> &findings)
{
    if (!node.IsMap())
        return; // plain scalar pin number, always fine
    for (const auto &entry : node) {
        const std::string key = entry.first.as<std::string>("");
        if (!kPinSubKeys.count(key))
            findings.push_back({kError, file, lineOf(entry.first),
                                "unknown key '" + path + "." + key + "'. A pin mapping accepts only pin, gpiochip and line"});
    }
}

void checkRfSwitchTable(const std::string &file, const YAML::Node &table, std::vector<Finding> &findings)
{
    if (!table.IsMap()) {
        findings.push_back({kError, file, lineOf(table), "Lora.rfswitch_table must be a mapping"});
        return;
    }

    size_t pinCount = 0;
    if (const YAML::Node pins = table["pins"]) {
        if (!pins.IsSequence()) {
            findings.push_back({kError, file, lineOf(pins), "Lora.rfswitch_table.pins must be a list"});
        } else {
            pinCount = pins.size();
            for (const auto &pin : pins) {
                const std::string name = pin.as<std::string>("");
                if (!kRfSwitchPins.count(name))
                    findings.push_back({kError, file, lineOf(pin),
                                        "Lora.rfswitch_table.pins: '" + name +
                                            "' is not a recognised pin. Valid values are DIO5, DIO6, DIO7, DIO8 and DIO10"});
            }
            if (pinCount > 5)
                findings.push_back(
                    {kError, file, lineOf(pins),
                     "Lora.rfswitch_table.pins lists " + std::to_string(pinCount) + " pins but only the first 5 are read"});
        }
    } else {
        findings.push_back({kError, file, lineOf(table), "Lora.rfswitch_table has no 'pins' list, so no switch pins are driven"});
    }

    for (const auto &entry : table) {
        const std::string key = entry.first.as<std::string>("");
        if (key == "pins")
            continue;
        if (!kRfSwitchModes.count(key)) {
            findings.push_back({kError, file, lineOf(entry.first), "unknown key 'Lora.rfswitch_table." + key + "'"});
            continue;
        }
        const YAML::Node &row = entry.second;
        if (!row.IsSequence()) {
            findings.push_back({kError, file, lineOf(row), "Lora.rfswitch_table." + key + " must be a list"});
            continue;
        }
        if (pinCount && row.size() != pinCount)
            findings.push_back({kError, file, lineOf(row),
                                "Lora.rfswitch_table." + key + " has " + std::to_string(row.size()) + " values but " +
                                    std::to_string(pinCount) + " pins are declared"});
        for (const auto &value : row) {
            const std::string level = value.as<std::string>("");
            if (level != "HIGH" && level != "LOW")
                findings.push_back({kError, file, lineOf(value),
                                    "Lora.rfswitch_table." + key + ": '" + level +
                                        "' is not HIGH or LOW. Anything that is not exactly \"HIGH\" is treated as LOW"});
        }
    }

    // Every mode absent from the table defaults to all-LOW, which for most modules is
    // the shutdown state. Worth saying out loud rather than leaving to be discovered.
    std::vector<std::string> missing;
    for (const auto &mode : kRfSwitchModes)
        if (!table[mode])
            missing.push_back(mode);
    if (!missing.empty()) {
        std::string list;
        for (const auto &mode : missing)
            list += (list.empty() ? "" : ", ") + mode;
        findings.push_back(
            {kInfo, file, lineOf(table), "Lora.rfswitch_table omits " + list + "; those modes default to all pins LOW"});
    }
}

void checkSection(const std::string &file, const std::string &section, const YAML::Node &body, std::vector<Finding> &findings)
{
    const auto &allowed = schema().at(section);
    if (kFreeFormSections.count(section) || !body.IsMap())
        return;

    for (const auto &entry : body) {
        const std::string key = entry.first.as<std::string>("");
        const YAML::Node &value = entry.second;

        if (!allowed.count(key)) {
            std::string message = "unknown key '" + section + "." + key + "', ignored by meshtasticd";
            const auto owner = keyOwners().find(key);
            if (owner != keyOwners().end() && !owner->second.count(section))
                message += ". It is a valid key of " + joinSections(owner->second);
            findings.push_back({kWarn, file, lineOf(entry.first), message});
            continue;
        }

        if (section == "Lora" && key == "rfswitch_table") {
            checkRfSwitchTable(file, value, findings);
        } else if (section == "Display" && key == "HUB75") {
            if (value.IsMap())
                for (const auto &hub : value)
                    if (!kHub75Keys.count(hub.first.as<std::string>("")))
                        findings.push_back({kWarn, file, lineOf(hub.first),
                                            "unknown key 'Display.HUB75." + hub.first.as<std::string>("") + "', ignored"});
        } else if (key == "Enable_Pins" || key == "ExtraPins") {
            if (value.IsSequence())
                for (const auto &pin : value)
                    checkPinNode(file, section + "." + key, pin, findings);
        } else if (key == "JoystickButtons") {
            // Free-form: any action name mapped to an evdev code.
        } else if ((section == "Lora" && kLoraPinKeys.count(key)) ||
                   (section == "Display" &&
                    (key == "DC" || key == "CS" || key == "Backlight" || key == "BacklightPWMChannel" || key == "Reset")) ||
                   (section == "Touchscreen" && (key == "CS" || key == "IRQ")) ||
                   (section == "Input" && (key == "User" || key.rfind("Trackball", 0) == 0)) ||
                   (section == "GPIO" && key == "User")) {
            checkPinNode(file, section + "." + key, value, findings);
        }
    }
}

// ---------------------------------------------------------------------------
// Cross-file overlap
// ---------------------------------------------------------------------------
// Every .yaml in the config directory is loaded into the same portduino_config, so
// two files that set the same key are not merged: the one loaded LAST wins. Note
// this is the opposite of the within-file rule, where yaml-cpp keeps the first.

// path (below the top-level section) -> file -> line of its first appearance there
using PathIndex = std::map<std::string, std::map<std::string, int>>;

void collectPaths(const std::string &file, const YAML::Node &node, const std::string &prefix, int depth, PathIndex &index)
{
    if (!node.IsMap())
        return;
    for (const auto &entry : node) {
        const std::string key = entry.first.as<std::string>("");
        const std::string path = prefix.empty() ? key : prefix + "." + key;
        // depth 0 is the top-level section; several files each having a "Lora:" is
        // normal, so only record what lives inside a section.
        if (depth >= 1)
            index[path].emplace(file, lineOf(entry.first));
        collectPaths(file, entry.second, path, depth + 1, index);
    }
}

void checkFile(const std::string &file, std::vector<Finding> &findings, PathIndex &paths,
               std::map<std::string, std::vector<std::string>> &sectionOwners)
{
    YAML::Node doc;
    try {
        doc = YAML::LoadFile(file);
    } catch (const std::exception &e) {
        findings.push_back({kError, file, 0, std::string("could not be parsed, so it is being ignored entirely: ") + e.what()});
        return;
    }

    if (doc.IsNull()) {
        findings.push_back({kWarn, file, 0, "is empty"});
        return;
    }
    if (!doc.IsMap()) {
        findings.push_back({kError, file, 0, "top level is not a mapping, so nothing in it is read"});
        return;
    }

    checkDuplicateKeys(file, findings);
    collectPaths(file, doc, "", 0, paths);

    for (const auto &entry : doc) {
        const std::string sectionName = entry.first.as<std::string>("");
        // A file that repeats a section still counts once here; the repeat itself is
        // reported separately as a duplicate key.
        auto &owners = sectionOwners[sectionName];
        if (schema().count(sectionName) && (owners.empty() || owners.back() != file))
            owners.push_back(file);
    }

    for (const auto &entry : doc) {
        const std::string section = entry.first.as<std::string>("");
        if (schema().count(section)) {
            checkSection(file, section, entry.second, findings);
            continue;
        }
        std::string message = "unknown top-level section '" + section + "', ignored by meshtasticd";
        const auto owner = keyOwners().find(section);
        if (owner != keyOwners().end())
            message += ". '" + section + "' is a key of " + joinSections(owner->second) +
                       " -- indent it one level so it sits "
                       "inside that section";
        findings.push_back({kError, file, lineOf(entry.first), message});
    }
}

std::string describeOwners(const std::map<std::string, int> &owners)
{
    std::string out;
    for (const auto &owner : owners) {
        if (!out.empty())
            out += ", ";
        out += owner.first;
        if (owner.second)
            out += " line " + std::to_string(owner.second);
    }
    return out;
}

void checkCrossFileOverlap(const PathIndex &paths, const std::map<std::string, std::vector<std::string>> &sectionOwners,
                           std::vector<Finding> &findings)
{
    const std::string across = "(across configuration files)";

    for (const auto &entry : paths) {
        if (entry.second.size() < 2)
            continue;
        // If an ancestor also collides then the whole subtree is replaced together;
        // reporting the parent once is clearer than repeating every leaf under it.
        bool coveredByAncestor = false;
        std::string ancestor = entry.first;
        for (size_t dot = ancestor.rfind('.'); dot != std::string::npos; dot = ancestor.rfind('.')) {
            ancestor = ancestor.substr(0, dot);
            const auto found = paths.find(ancestor);
            if (found != paths.end() && found->second.size() >= 2) {
                coveredByAncestor = true;
                break;
            }
        }
        if (coveredByAncestor)
            continue;

        findings.push_back({kInfo, across, 0,
                            "'" + entry.first + "' is set in " + std::to_string(entry.second.size()) + " files (" +
                                describeOwners(entry.second) + "). The file loaded last wins"});
    }

    // Re-reading a "Lora:" section does not merge into the previous one. These keys
    // are assigned unconditionally with a default (PortduinoGlue.cpp), so a later
    // file that has a Lora section but omits them silently resets them.
    const auto loraOwners = sectionOwners.find("Lora");
    if (loraOwners != sectionOwners.end() && loraOwners->second.size() > 1) {
        std::string files;
        for (const auto &file : loraOwners->second)
            files += (files.empty() ? "" : ", ") + file;
        findings.push_back(
            {kWarn, across, 0,
             std::to_string(loraOwners->second.size()) + " files define a 'Lora:' section (" + files +
                 "). These keys are re-read with a default every time a Lora section is seen, so any of them not repeated "
                 "in the last file loaded is reset: spidev, spiSpeed, gpiochip, DIO2_AS_RF_SWITCH, DIO3_TCXO_VOLTAGE, "
                 "USB_PID, USB_VID, USB_Serialnum. Normally exactly one Lora config should be enabled at a time"});
    }
}

// ---------------------------------------------------------------------------
// Semantic checks against the merged configuration
// ---------------------------------------------------------------------------

bool isLR11xx(lora_module_enum module)
{
    return module == use_lr1110 || module == use_lr1120 || module == use_lr1121;
}

std::string moduleName()
{
    const auto it = portduino_config.loraModules.find(portduino_config.lora_module);
    return it == portduino_config.loraModules.end() ? "unknown" : it->second;
}

// A pin key that is present but whose value will not convert to a number falls back
// to RADIOLIB_NC (-1) while still being marked enabled, and initGPIOPin() then trips
// an assertion inside LinuxGPIOPin rather than failing cleanly. Catching it here is
// the difference between a readable report and a stack trace from a library file.
void checkPinValues(std::vector<Finding> &findings)
{
    const std::string merged = "(merged configuration)";

    auto report = [&](const std::string &name) {
        findings.push_back({kError, merged, 0,
                            name + " is set, but its value could not be read as a pin number so it resolves to -1. "
                                   "meshtasticd aborts with an assertion when it tries to claim that line. Check for a "
                                   "non-numeric value, or a stray line folded into it by YAML indentation"});
    };

    for (const auto *pin : portduino_config.all_pins)
        if (pin->enabled && pin->pin < 0)
            report(pin->config_section + "." + pin->config_name);
    for (const auto &pin : portduino_config.extra_pins)
        if (pin.enabled && pin.pin < 0)
            report(pin.config_section + "." + pin.config_name);
}

void checkMergedConfig(std::vector<Finding> &findings)
{
    const std::string merged = "(merged configuration)";

    checkPinValues(findings);

    if (isLR11xx(portduino_config.lora_module) && !portduino_config.has_rfswitch_table)
        findings.push_back({kWarn, merged, 0,
                            "Module is " + moduleName() +
                                " but no Lora.rfswitch_table is set, so setRfSwitchTable() is never called. Most LR11xx "
                                "modules cannot transmit or receive without one"});

    if (!isLR11xx(portduino_config.lora_module) && portduino_config.has_rfswitch_table)
        findings.push_back(
            {kWarn, merged, 0,
             "a Lora.rfswitch_table is set but Module is " + moduleName() + ", and the table is only applied to LR11xx radios"});

    if (portduino_config.lora_cs_pin.enabled && !portduino_config.lora_spi_dev.empty() &&
        portduino_config.lora_spi_dev != "ch341")
        findings.push_back({kInfo, merged, 0,
                            "both Lora.spidev (" + portduino_config.lora_spi_dev +
                                ") and Lora.CS are set. If your device tree already assigns a chip select to that spidev "
                                "node, meshtasticd will fail to claim the CS line at startup; if it does not, CS must be "
                                "set here. Check with 'gpioinfo'"});
}

void printSummary()
{
    std::cout << "\nEffective radio configuration:\n";
    std::cout << "  Module            : " << moduleName() << "\n";
    if (!portduino_config.lora_spi_dev.empty())
        std::cout << "  spidev            : " << portduino_config.lora_spi_dev << "\n";
    std::cout << "  SPI speed         : " << portduino_config.spiSpeed << "\n";
    if (portduino_config.dio3_tcxo_voltage)
        std::cout << "  DIO3 TCXO voltage : " << portduino_config.dio3_tcxo_voltage << " mV\n";

    std::cout << "  RF switch table   : " << (portduino_config.has_rfswitch_table ? "set" : "not set") << "\n";

    std::cout << "\nResolved GPIO lines (what meshtasticd will try to claim):\n";
    bool any = false;
    for (const auto *pin : portduino_config.all_pins) {
        if (!pin->enabled || pin->config_section != "Lora")
            continue;
        any = true;
        std::cout << "  " << pin->config_name;
        for (size_t i = pin->config_name.size(); i < 18; i++)
            std::cout << ' ';
        std::cout << ": pin " << pin->pin << "  gpiochip" << pin->gpiochip << " line " << pin->line << "\n";
    }
    if (!any)
        std::cout << "  (none configured)\n";
    std::cout << "\n  Confirm these against 'gpiodetect' and 'gpioinfo' on this machine. A line that\n"
                 "  exists on the wrong chip is claimed successfully and silently does nothing.\n";
}

} // namespace

int runConfigCheck(const std::vector<std::string> &configFiles)
{
    std::vector<Finding> findings;

    std::cout << "meshtasticd configuration check\n";
    std::cout << "===============================\n\n";

    if (configFiles.empty()) {
        std::cout << "No configuration files were found.\n";
        return 1;
    }

    std::cout << "Configuration files, in load order (later files override earlier ones):\n";
    for (size_t i = 0; i < configFiles.size(); i++)
        std::cout << "  " << (i + 1) << ". " << configFiles[i] << "\n";
    if (configFiles.size() > 1)
        std::cout << "\n  Files in the config directory are read in whatever order the filesystem\n"
                     "  returns them, which is not necessarily alphabetical and can differ between\n"
                     "  machines. Avoid relying on one file overriding another.\n";

    PathIndex paths;
    std::map<std::string, std::vector<std::string>> sectionOwners;
    for (const auto &file : configFiles)
        checkFile(file, findings, paths, sectionOwners);
    checkCrossFileOverlap(paths, sectionOwners, findings);
    checkMergedConfig(findings);

    int errors = 0, warnings = 0;
    std::string currentFile;
    for (const auto &finding : findings) {
        if (finding.level == kError)
            errors++;
        else if (finding.level == kWarn)
            warnings++;

        if (finding.file != currentFile) {
            currentFile = finding.file;
            std::cout << "\n" << currentFile << "\n";
        }
        std::cout << "  " << levelName(finding.level) << ' ';
        if (finding.line)
            std::cout << "line " << finding.line << ": ";
        std::cout << finding.message << "\n";
    }

    printSummary();

    std::cout << "\nResult: " << errors << (errors == 1 ? " error, " : " errors, ") << warnings
              << (warnings == 1 ? " warning" : " warnings") << "\n";
    if (errors == 0 && warnings == 0)
        std::cout << "Configuration looks good.\n";

    return errors ? 1 : 0;
}

#endif // !ARCH_PORTDUINO_WASM
