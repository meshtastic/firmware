#include "ConfigCheck.h"

#ifndef ARCH_PORTDUINO_WASM

#include "configuration.h"

#include "PortduinoGlue.h"

#include "yaml-cpp/eventhandler.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------
// Mirrors the keys loadConfig() reads: meshtasticd silently ignores anything else, so
// an unlisted key is honestly "unknown". Teach loadConfig() a new key, add it here too --
// CI runs --check over bin/config.d/**, so an omission fails the build rather than a user.

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
        return "INFO ";
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
// yaml-cpp silently keeps the FIRST of duplicate keys, so a later override looks applied
// but is not. The Node API sees an already-collapsed map, so walk the parser events.

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
    // No advance(): push() already consumed the collection's slot in the parent. Guarded
    // because balance rests on yaml-cpp emitting matched start/end events -- its invariant.
    void pop()
    {
        if (!stack.empty())
            stack.pop_back();
    }
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

// ---------------------------------------------------------------------------
// Value types
// ---------------------------------------------------------------------------
// A wrong-typed value silently does nothing under .as<T>(default), but the two reads with
// NO default (Logging.AsciiLogs, TX_GAIN_LORA) throw and stop meshtasticd. Conversions are
// tested by asking yaml-cpp, so this cannot drift from what loadConfig() accepts.

enum ValueType { kBool, kInt, kFloat, kString, kIntList, kBoolOrFloat, kIntOrString };

struct ValueSpec {
    ValueType type;
    bool fatal; // read without a default: a bad value stops meshtasticd outright
};

const std::map<std::string, ValueSpec> &valueSpecs()
{
    static const std::map<std::string, ValueSpec> s = {
        {"Lora.spiSpeed", {kInt, false}},
        {"Lora.gpiochip", {kInt, false}},
        {"Lora.spidev", {kString, false}},
        {"Lora.DIO2_AS_RF_SWITCH", {kBool, false}},
        // Accepts a float (volts) or `true` (meaning 1.8V), so both are allowed here.
        {"Lora.DIO3_TCXO_VOLTAGE", {kBoolOrFloat, false}},
        {"Lora.LR1110_MAX_POWER", {kInt, false}},
        {"Lora.LR1120_MAX_POWER", {kInt, false}},
        {"Lora.RF95_MAX_POWER", {kInt, false}},
        {"Lora.SX126X_MAX_POWER", {kInt, false}},
        {"Lora.SX128X_MAX_POWER", {kInt, false}},
        // TX_GAIN_LORA is not in this table: it accepts a list OR a bare scalar, and
        // only the list path is fatal. See checkTxGain().
        {"Lora.USB_PID", {kInt, false}},
        {"Lora.USB_VID", {kInt, false}},
        {"Lora.USB_Serialnum", {kString, false}},
        {"General.MaxNodes", {kInt, false}},
        {"General.MaxMessageQueue", {kInt, false}},
        {"General.APIPort", {kInt, false}},
        {"General.ConfigDirectory", {kString, false}},
        {"General.AvailableDirectory", {kString, false}},
        {"Config.DisplayMode", {kString, false}},
        {"Config.EnableUDP", {kBool, false}},
        {"Config.StatusMessage", {kString, false}},
        {"Display.Panel", {kString, false}},
        {"Display.spidev", {kString, false}},
        {"Display.BusFrequency", {kInt, false}},
        {"Display.Width", {kInt, false}},
        {"Display.Height", {kInt, false}},
        {"Display.Invert", {kBool, false}},
        {"Display.Rotate", {kInt, false}},
        {"Display.OffsetX", {kInt, false}},
        {"Display.OffsetY", {kInt, false}},
        {"Display.OffsetRotate", {kInt, false}},
        {"Display.RGBOrder", {kBool, false}},
        {"Display.BacklightInvert", {kBool, false}},
        {"Touchscreen.Module", {kString, false}},
        {"Touchscreen.spidev", {kString, false}},
        {"Touchscreen.BusFrequency", {kInt, false}},
        {"Touchscreen.I2CAddr", {kInt, false}},
        {"Touchscreen.Rotate", {kBool, false}},
        {"Input.KeyboardDevice", {kString, false}},
        {"Input.PointerDevice", {kString, false}},
        {"Input.JoystickDevice", {kString, false}},
        {"Input.TrackballDirection", {kString, false}},
        {"GPS.SerialPath", {kString, false}},
        {"GPS.GpsdHost", {kString, false}},
        {"GPS.GpsdPort", {kInt, false}},
        {"I2C.I2CDevice", {kString, false}},
        {"Logging.LogLevel", {kString, false}},
        {"Logging.TraceFile", {kString, false}},
        {"Logging.JSONFile", {kString, false}},
        {"Logging.JSONFileRotate", {kInt, false}},
        // Read as an int and as a string (the "textmessage"-style aliases).
        {"Logging.JSONFilter", {kIntOrString, false}},
        {"Logging.AsciiLogs", {kBool, true}},
        {"Webserver.Port", {kInt, false}},
        {"Webserver.RootPath", {kString, false}},
        {"Webserver.SSLCert", {kString, false}},
        {"Webserver.SSLKey", {kString, false}},
        {"HostMetrics.ReportInterval", {kInt, false}},
        {"HostMetrics.Channel", {kInt, false}},
        {"HostMetrics.UserStringCommand", {kString, false}},
        {"Display.HUB75.Rows", {kInt, false}},
        {"Display.HUB75.Cols", {kInt, false}},
        {"Display.HUB75.ChainLength", {kInt, false}},
        {"Display.HUB75.Parallel", {kInt, false}},
        {"Display.HUB75.PWMBits", {kInt, false}},
        {"Display.HUB75.PWMLSBNanoseconds", {kInt, false}},
        {"Display.HUB75.Brightness", {kInt, false}},
        {"Display.HUB75.ScanMode", {kInt, false}},
        {"Display.HUB75.RowAddressType", {kInt, false}},
        {"Display.HUB75.Multiplexing", {kInt, false}},
        {"Display.HUB75.GPIOSlowdown", {kInt, false}},
        {"Display.HUB75.LimitRefreshRateHz", {kInt, false}},
        {"Display.HUB75.DisableHardwarePulsing", {kBool, false}},
        {"Display.HUB75.ShowRefreshRate", {kBool, false}},
        {"Display.HUB75.InverseColors", {kBool, false}},
        {"Display.HUB75.HardwareMapping", {kString, false}},
        {"Display.HUB75.RGBSequence", {kString, false}},
        {"Display.HUB75.PixelMapper", {kString, false}},
        {"Display.HUB75.PanelType", {kString, false}},
    };
    return s;
}

const char *typeName(ValueType type)
{
    switch (type) {
    case kBool:
        return "a true/false value";
    case kInt:
        return "a whole number";
    case kFloat:
        return "a number";
    case kIntList:
        return "a list of whole numbers";
    case kBoolOrFloat:
        return "a number or true/false";
    case kIntOrString:
        return "a whole number or a name";
    default:
        return "a text value";
    }
}

// Ask yaml-cpp to perform the same conversion loadConfig() will.
bool converts(const YAML::Node &node, ValueType type)
{
    try {
        switch (type) {
        case kBool:
            node.as<bool>();
            return true;
        case kInt:
            node.as<int>();
            return true;
        case kFloat:
            node.as<float>();
            return true;
        case kBoolOrFloat:
            try {
                node.as<float>();
                return true;
            } catch (const std::exception &) {
                node.as<bool>();
                return true;
            }
        case kIntOrString:
            try {
                node.as<int>();
                return true;
            } catch (const std::exception &) {
                node.as<std::string>();
                return true;
            }
        case kIntList:
            if (!node.IsSequence())
                return false;
            for (const auto &item : node)
                item.as<int>();
            return true;
        default:
            node.as<std::string>();
            return true;
        }
    } catch (const std::exception &) {
        return false;
    }
}

void checkValueType(const std::string &file, const std::string &path, const YAML::Node &value, std::vector<Finding> &findings)
{
    const auto spec = valueSpecs().find(path);
    if (spec == valueSpecs().end() || converts(value, spec->second.type))
        return;

    if (spec->second.fatal)
        findings.push_back({kError, file, lineOf(value),
                            path + " is not " + typeName(spec->second.type) +
                                ". This one is read without a fallback, so the conversion throws and meshtasticd "
                                "refuses to start on this file"});
    else
        findings.push_back({kWarn, file, lineOf(value),
                            path + " is not " + typeName(spec->second.type) +
                                ", so it is silently replaced by the default and the setting does nothing"});
}

// The PA gain table's two shapes fail differently: a bad list entry stops meshtasticd (no
// default), a bad scalar falls back to 0. Backed by uint16_t[22], so extras drop and values wrap.
void checkTxGain(const std::string &file, const YAML::Node &node, std::vector<Finding> &findings)
{
    constexpr size_t kMaxPaPoints = 22;

    if (node.IsSequence()) {
        if (node.size() > kMaxPaPoints)
            findings.push_back({kWarn, file, lineOf(node),
                                "Lora.TX_GAIN_LORA lists " + std::to_string(node.size()) + " points but only the first " +
                                    std::to_string(kMaxPaPoints) + " are stored; the rest are dropped"});
        for (const auto &point : node) {
            if (!converts(point, kInt)) {
                findings.push_back({kError, file, lineOf(point),
                                    "Lora.TX_GAIN_LORA entry '" + point.as<std::string>("") +
                                        "' is not a whole number. List entries are read without a fallback, so this "
                                        "throws and meshtasticd refuses to start on this file"});
                continue;
            }
            const int value = point.as<int>();
            // Stored into a uint16_t, so anything outside the range silently wraps.
            if (value < 0 || value > 65535)
                findings.push_back({kError, file, lineOf(point),
                                    "Lora.TX_GAIN_LORA entry " + std::to_string(value) +
                                        " does not fit the 0-65535 range it is stored in, so it wraps to a different "
                                        "gain than the one written"});
        }
        return;
    }

    if (node.IsMap()) {
        findings.push_back({kError, file, lineOf(node), "Lora.TX_GAIN_LORA must be a list of gain points, or a single number"});
        return;
    }

    if (!converts(node, kInt))
        findings.push_back({kWarn, file, lineOf(node),
                            "Lora.TX_GAIN_LORA is not a whole number, so it is silently read as 0 and no PA gain is applied"});
}

// Module names match exactly and are inconsistently cased (RF95 upper, sx1262 lower),
// and loadConfig() exits on an unknown name without printing the valid set, so name it here.
void checkLoraModule(const std::string &file, const YAML::Node &module, std::vector<Finding> &findings)
{
    const std::string name = module.as<std::string>("");
    if (name.empty())
        return;

    std::string valid;
    std::string caseHint;
    for (const auto &known : portduino_config.loraModules) {
        if (name == known.second)
            return;
        valid += (valid.empty() ? "" : ", ") + known.second;
        if (caseHint.empty() && name.size() == known.second.size() &&
            std::equal(name.begin(), name.end(), known.second.begin(), [](char a, char b) {
                // Cast first: tolower() on a negative char is undefined, and a stray
                // non-ASCII byte in a hand-edited config is exactly how that happens.
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            }))
            caseHint = known.second;
    }

    std::string message =
        "Lora.Module '" + name + "' is not a module meshtasticd knows, and it refuses to start. Valid: " + valid;
    if (!caseHint.empty())
        message += ". The name is matched exactly -- did you mean '" + caseHint + "'?";
    findings.push_back({kError, file, lineOf(module), message});
}

// loadConfig() rejects a bad MACAddress or MACAddressSource silently, falling through to the
// BlueZ and LoRa-serial fallbacks; if those yield nothing, meshtasticd exits on a blank MAC.
void checkMacAddress(const std::string &file, const YAML::Node &general, std::vector<Finding> &findings)
{
    const YAML::Node address = general["MACAddress"];
    const YAML::Node source = general["MACAddressSource"];
    const std::string addressText = address ? address.as<std::string>("") : "";
    const std::string sourceText = source ? source.as<std::string>("") : "";

    if (!addressText.empty() && !sourceText.empty()) {
        findings.push_back({kError, file, lineOf(source),
                            "General.MACAddress and General.MACAddressSource are both set. meshtasticd refuses to start "
                            "with both; keep whichever one you want the MAC to come from"});
        return;
    }

    if (!addressText.empty()) {
        std::string digits;
        for (const char c : addressText)
            if (c != ':' && c != '-')
                digits += c;
        const bool hex = digits.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
        // loadConfig() strips the colons and then requires more than 11 characters;
        // anything shorter is dropped without a word and the MAC comes from elsewhere.
        if (digits.size() < 12 || !hex)
            findings.push_back({kError, file, lineOf(address),
                                "General.MACAddress '" + addressText +
                                    "' is not 12 hex digits, so it is ignored and the MAC falls back to the Bluetooth "
                                    "adapter or the LoRa device serial. If neither yields one, meshtasticd exits with "
                                    "'Blank MAC Address not allowed!'"});
    }

    if (!sourceText.empty()) {
        const std::string path = "/sys/class/net/" + sourceText + "/address";
        std::ifstream probe(path);
        if (!probe.good())
            findings.push_back({kWarn, file, lineOf(source),
                                "General.MACAddressSource '" + sourceText + "' has no " + path +
                                    " on this machine, so the MAC reads back empty and meshtasticd asks you to set one. "
                                    "Expected if you are checking this config on a different host"});
    }
}

void checkSection(const std::string &file, const std::string &section, const YAML::Node &body, std::vector<Finding> &findings)
{
    const auto &allowed = schema().at(section);
    if (kFreeFormSections.count(section))
        return;
    // An empty section (`Lora:` with no body) is null, not a map, and loadConfig() tests
    // before reading it. Any other non-map shape is unreadable, so do not call the file clean.
    if (body.IsNull())
        return;
    if (!body.IsMap()) {
        findings.push_back({kError, file, lineOf(body), "'" + section + "' is not a mapping, so nothing in it is read"});
        return;
    }

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

        checkValueType(file, section + "." + key, value, findings);

        if (section == "Lora" && key == "rfswitch_table") {
            checkRfSwitchTable(file, value, findings);
        } else if (section == "Lora" && key == "Module") {
            checkLoraModule(file, value, findings);
        } else if (section == "Lora" && key == "TX_GAIN_LORA") {
            checkTxGain(file, value, findings);
        } else if (section == "Display" && key == "HUB75") {
            if (value.IsMap())
                for (const auto &hub : value) {
                    const std::string hubKey = hub.first.as<std::string>("");
                    if (!kHub75Keys.count(hubKey))
                        findings.push_back(
                            {kWarn, file, lineOf(hub.first), "unknown key 'Display.HUB75." + hubKey + "', ignored"});
                    else
                        checkValueType(file, "Display.HUB75." + hubKey, hub.second, findings);
                }
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
// Every .yaml loads into the same portduino_config, so two files setting the same key
// are not merged: the one loaded LAST wins -- the opposite of the within-file rule.

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
            if (section == "General" && entry.second.IsMap())
                checkMacAddress(file, entry.second, findings);
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
            ancestor.resize(dot);
            const auto found = paths.find(ancestor);
            if (found != paths.end() && found->second.size() >= 2) {
                coveredByAncestor = true;
                break;
            }
        }
        if (coveredByAncestor)
            continue;

        // The one place "last wins" is untrue: the loader only ever writes HIGH, so the effective
        // table is the OR of every table loaded -- a switch state belonging to none of them.
        if (entry.first == "Lora.rfswitch_table") {
            findings.push_back({kError, across, 0,
                                "'Lora.rfswitch_table' is set in " + std::to_string(entry.second.size()) + " files (" +
                                    describeOwners(entry.second) +
                                    "). These do NOT override each other: the loader only ever writes HIGH, so a HIGH "
                                    "from an earlier file survives a later file that sets LOW there, and the radio ends "
                                    "up driving the OR of every table. Enable exactly one"});
            continue;
        }

        findings.push_back({kInfo, across, 0,
                            "'" + entry.first + "' is set in " + std::to_string(entry.second.size()) + " files (" +
                                describeOwners(entry.second) + "). The file loaded last wins"});
    }

    // Sections do not merge, and these keys are assigned unconditionally with a default, so a
    // later file with a Lora section that omits them silently resets them.
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

// A pin whose value will not convert falls back to RADIOLIB_NC (-1) while still marked enabled,
// and initGPIOPin() then trips an assertion inside LinuxGPIOPin rather than failing cleanly.
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

void checkMergedConfig(const PathIndex &paths, std::vector<Finding> &findings)
{
    const std::string merged = "(merged configuration)";

    checkPinValues(findings);

    // portduinoSetup() skips initGPIOPin() for every Lora pin when spidev is ch341, so a
    // gpiochip or line mapping written next to one is read, stored, and never used.
    if (portduino_config.lora_spi_dev == "ch341") {
        auto endsWith = [](const std::string &text, const std::string &suffix) {
            return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        std::string ignored;
        for (const auto &entry : paths) {
            if (entry.first.rfind("Lora.", 0) != 0)
                continue;
            if (entry.first == "Lora.gpiochip" || endsWith(entry.first, ".gpiochip") || endsWith(entry.first, ".line"))
                ignored += (ignored.empty() ? "" : ", ") + entry.first;
        }
        if (!ignored.empty())
            findings.push_back({kWarn, merged, 0,
                                "Lora.spidev is ch341, so the Lora pins are indexes on the USB adapter and are driven by "
                                "the usermode driver rather than claimed from a gpiochip. " +
                                    ignored + " are read but never used"});
    }

    if (isLR11xx(portduino_config.lora_module) && !portduino_config.has_rfswitch_table)
        findings.push_back({kWarn, merged, 0,
                            "Module is " + moduleName() +
                                " but no Lora.rfswitch_table is set, so setRfSwitchTable() is never called. Most LR11xx "
                                "modules cannot transmit or receive without one"});

    if (!isLR11xx(portduino_config.lora_module) && portduino_config.has_rfswitch_table)
        findings.push_back(
            {kWarn, merged, 0,
             "a Lora.rfswitch_table is set but Module is " + moduleName() + ", and the table is only applied to LR11xx radios"});

    // Either way -- the old uncaught filesystem_error abort or today's clean exit -- the files
    // meant to configure the radio are not being loaded.
    if (!portduino_config.config_directory.empty()) {
        std::error_code error;
        if (!std::filesystem::is_directory(portduino_config.config_directory, error))
            findings.push_back({kError, merged, 0,
                                "General.ConfigDirectory '" + portduino_config.config_directory +
                                    "' is not a directory that can be read, so none of the files that were meant to be "
                                    "loaded from it are being loaded and meshtasticd stops at startup"});
    }

    // strncpy into a char[80] that is then hard null-terminated: safe, but silently
    // shortened, and the operator never sees the message they actually configured.
    if (portduino_config.has_statusMessage && portduino_config.statusMessage.size() > 79)
        findings.push_back({kWarn, merged, 0,
                            "Config.StatusMessage is " + std::to_string(portduino_config.statusMessage.size()) +
                                " characters and is truncated to 79 when it is stored"});

    // DIO3_TCXO_VOLTAGE is in VOLTS while every other Meshtastic surface uses millivolts, so
    // the natural "1800" silently asks for 1800V and nothing downstream range-checks it.
    if (portduino_config.dio3_tcxo_voltage > 3600)
        findings.push_back({kError, merged, 0,
                            "Lora.DIO3_TCXO_VOLTAGE resolves to " + std::to_string(portduino_config.dio3_tcxo_voltage) +
                                " mV, which no TCXO runs at. The value is in VOLTS and is multiplied by 1000, so write "
                                "1.8 (or true) for a 1.8V part, not 1800"});

    // loadConfig() only adopts APIPort inside this range and otherwise keeps the
    // default without a word, so an out-of-range port silently does nothing.
    if (portduino_config.api_port != -1 && (portduino_config.api_port <= 1023 || portduino_config.api_port >= 65536))
        findings.push_back({kWarn, merged, 0,
                            "General.APIPort " + std::to_string(portduino_config.api_port) +
                                " is outside 1024-65535, so it is ignored and the default port is used instead"});

    if (portduino_config.webserverport != -1 && (portduino_config.webserverport <= 0 || portduino_config.webserverport >= 65536))
        findings.push_back({kError, merged, 0,
                            "Webserver.Port " + std::to_string(portduino_config.webserverport) + " is not a usable TCP port"});

    if (portduino_config.MaxNodes <= 0)
        findings.push_back({kError, merged, 0,
                            "General.MaxNodes is " + std::to_string(portduino_config.MaxNodes) +
                                ", which leaves no room for even this node's own entry"});

#if !defined(HAS_HUB75_NATIVE)
    // A build-time gap rather than a config error: the same file is valid on a
    // meshtasticd built with rpi-rgb-led-matrix present.
    if (portduino_config.displayPanel == hub75)
        findings.push_back({kError, merged, 0,
                            "Display.Panel is HUB75 but this meshtasticd was built without HUB75 support, so it exits at "
                            "startup. Rebuild with hzeller/rpi-rgb-led-matrix installed (it provides rgbmatrix.pc)"});
#endif

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

    // setRfSwitchTable() is only called for an LR11xx, so "not set" is no gap elsewhere; "auto"
    // has not resolved to a module yet, so absence cannot be called either way.
    const char *rfSwitch = "not needed for this module";
    if (portduino_config.has_rfswitch_table)
        rfSwitch = "set";
    else if (isLR11xx(portduino_config.lora_module))
        rfSwitch = "not set";
    else if (portduino_config.lora_module == use_autoconf)
        rfSwitch = "not set (module not resolved yet)";
    std::cout << "  RF switch table   : " << rfSwitch << "\n";

    // A ch341 adapter's Lora pins are adapter indexes handed to Ch341Hal, not gpiochip lines
    // (portduinoSetup() skips initGPIOPin()), so pointing the user at gpioinfo would be wrong.
    const bool usbAdapter = portduino_config.lora_spi_dev == "ch341";

    std::cout << (usbAdapter ? "\nCH341 adapter pins (driven over USB, not claimed from a gpiochip):\n"
                             : "\nResolved GPIO lines (what meshtasticd will try to claim):\n");
    bool any = false;
    for (const auto *pin : portduino_config.all_pins) {
        if (!pin->enabled || pin->config_section != "Lora")
            continue;
        any = true;
        std::cout << "  " << pin->config_name;
        for (size_t i = pin->config_name.size(); i < 18; i++)
            std::cout << ' ';
        std::cout << ": pin " << pin->pin;
        if (!usbAdapter)
            std::cout << "  gpiochip" << pin->gpiochip << " line " << pin->line;
        std::cout << "\n";
    }
    if (!any)
        std::cout << "  (none configured)\n";
    if (usbAdapter)
        std::cout << "\n  These are pin indexes on the CH341 itself, so 'gpiodetect' and 'gpioinfo' say\n"
                     "  nothing about them. Lora.gpiochip and any per-pin gpiochip/line mapping are\n"
                     "  ignored for a ch341 device.\n";
    else
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
    checkMergedConfig(paths, findings);

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
