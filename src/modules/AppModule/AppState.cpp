#include "configuration.h"

#include "modules/AppModule/AppState.h"
#include "serialization/JSON.h"
#include "FSCommon.h"

#ifdef FSCom

static const char *STATE_DIR = "/apps_state";

std::string FlashAppStateBackend::statePath(const std::string &appSlug)
{
    return std::string(STATE_DIR) + "/" + appSlug + ".json";
}

bool FlashAppStateBackend::readState(const std::string &appSlug, std::string &json)
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

    json.resize(size);
    f.read((uint8_t *)json.data(), size);
    f.close();
    return true;
}

bool FlashAppStateBackend::writeState(const std::string &appSlug, const std::string &json)
{
    FSCom.mkdir(STATE_DIR);
    std::string path = statePath(appSlug);
    File f = FSCom.open(path.c_str(), FILE_O_WRITE);
    if (!f)
        return false;

    f.write((const uint8_t *)json.data(), json.size());
    f.close();
    return true;
}

std::string FlashAppStateBackend::get(const std::string &appSlug, const std::string &key, bool &found)
{
    found = false;
    std::string jsonStr;
    if (!readState(appSlug, jsonStr))
        return "";

    std::unique_ptr<JSONValue> root(JSON::Parse(jsonStr.c_str()));
    if (!root || !root->IsObject())
        return "";

    if (!root->HasChild(key.c_str()))
        return "";

    const JSONValue *val = root->Child(key.c_str());
    if (!val || !val->IsString())
        return "";

    found = true;
    return val->AsString();
}

bool FlashAppStateBackend::set(const std::string &appSlug, const std::string &key, const std::string &value)
{
    // Read existing state or start fresh
    std::string jsonStr;
    JSONObject obj;

    if (readState(appSlug, jsonStr)) {
        std::unique_ptr<JSONValue> root(JSON::Parse(jsonStr.c_str()));
        if (root && root->IsObject()) {
            // Copy existing keys
            auto keys = root->ObjectKeys();
            for (const auto &k : keys) {
                const JSONValue *v = root->Child(k.c_str());
                if (v && v->IsString())
                    obj[k] = new JSONValue(v->AsString());
            }
        }
    }

    obj[key] = new JSONValue(value);

    JSONValue rootVal(obj);
    std::string out = rootVal.Stringify(false);
    return writeState(appSlug, out);
}

bool FlashAppStateBackend::remove(const std::string &appSlug, const std::string &key)
{
    std::string jsonStr;
    if (!readState(appSlug, jsonStr))
        return true; // nothing to remove

    std::unique_ptr<JSONValue> root(JSON::Parse(jsonStr.c_str()));
    if (!root || !root->IsObject())
        return true;

    JSONObject obj;
    auto keys = root->ObjectKeys();
    for (const auto &k : keys) {
        if (k == key)
            continue;
        const JSONValue *v = root->Child(k.c_str());
        if (v && v->IsString())
            obj[k] = new JSONValue(v->AsString());
    }

    JSONValue rootVal(obj);
    std::string out = rootVal.Stringify(false);
    return writeState(appSlug, out);
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
