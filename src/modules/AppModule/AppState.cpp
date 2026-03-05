#include "configuration.h"

#include "modules/AppModule/AppState.h"
#include "FSCommon.h"

#ifdef FSCom

static const char *STATE_DIR = "/apps_state";

// Simple line-based key=value storage to avoid dependency on serialization/ (excluded on nrf52).
// Each line is: key\tvalue\n
// Keys and values must not contain \t or \n.

std::string FlashAppStateBackend::statePath(const std::string &appSlug)
{
    return std::string(STATE_DIR) + "/" + appSlug + ".state";
}

bool FlashAppStateBackend::readState(const std::string &appSlug, std::string &content)
{
    std::string path = statePath(appSlug);
    File f = FSCom.open(path.c_str(), FILE_O_READ);
    if (!f)
        return false;

    size_t size = f.size();
    if (size == 0 || size > 8192) {
        f.close();
        return false;
    }

    content.resize(size);
    f.read((uint8_t *)content.data(), size);
    f.close();
    return true;
}

bool FlashAppStateBackend::writeState(const std::string &appSlug, const std::string &content)
{
    FSCom.mkdir(STATE_DIR);
    std::string path = statePath(appSlug);
    File f = FSCom.open(path.c_str(), FILE_O_WRITE);
    if (!f)
        return false;

    f.write((const uint8_t *)content.data(), content.size());
    f.close();
    return true;
}

// Parse "key\tvalue\n" lines into a map
static std::map<std::string, std::string> parseState(const std::string &content)
{
    std::map<std::string, std::string> m;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t nl = content.find('\n', pos);
        if (nl == std::string::npos)
            nl = content.size();
        size_t tab = content.find('\t', pos);
        if (tab != std::string::npos && tab < nl) {
            m[content.substr(pos, tab - pos)] = content.substr(tab + 1, nl - tab - 1);
        }
        pos = nl + 1;
    }
    return m;
}

// Serialize a map back to "key\tvalue\n" format
static std::string serializeState(const std::map<std::string, std::string> &m)
{
    std::string out;
    for (const auto &kv : m) {
        out += kv.first;
        out += '\t';
        out += kv.second;
        out += '\n';
    }
    return out;
}

std::string FlashAppStateBackend::get(const std::string &appSlug, const std::string &key, bool &found)
{
    found = false;
    std::string content;
    if (!readState(appSlug, content))
        return "";

    auto m = parseState(content);
    auto it = m.find(key);
    if (it == m.end())
        return "";

    found = true;
    return it->second;
}

bool FlashAppStateBackend::set(const std::string &appSlug, const std::string &key, const std::string &value)
{
    std::string content;
    readState(appSlug, content);
    auto m = parseState(content);
    m[key] = value;
    return writeState(appSlug, serializeState(m));
}

bool FlashAppStateBackend::remove(const std::string &appSlug, const std::string &key)
{
    std::string content;
    if (!readState(appSlug, content))
        return true;

    auto m = parseState(content);
    m.erase(key);
    return writeState(appSlug, serializeState(m));
}

bool FlashAppStateBackend::clear(const std::string &appSlug)
{
    std::string path = statePath(appSlug);
    FSCom.remove(path.c_str());
    return true;
}

static std::shared_ptr<FlashAppStateBackend> sharedBackend;

std::shared_ptr<FlashAppStateBackend> getFlashAppStateBackend()
{
    if (!sharedBackend)
        sharedBackend = std::make_shared<FlashAppStateBackend>();
    return sharedBackend;
}

#endif // FSCom
