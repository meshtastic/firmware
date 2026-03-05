#include "configuration.h"

#include "modules/AppModule/AppModule.h"
#include "modules/AppModule/AppState.h"
#include "main.h"
#include "FSCommon.h"
#include "mapps/BerryRuntime.h"

#if HAS_SCREEN
#include "modules/AppModule/AppDisplay.h"
#include "graphics/Screen.h"
#endif

#if HAS_WIFI && defined(ARCH_ESP32)
#include "modules/AppModule/AppHttp.h"
#endif

#ifdef FSCom

// Decorating AppStateBackend that fires notifications on state changes.
// Wraps FlashAppStateBackend so that mapps' auto-registered app_state bindings
// (with proper type-safe encoding) also notify the OLED BUI queue and the
// global MeshEventHandler (TFT AppHost bridge).
class NotifyingAppStateBackend : public AppStateBackend
{
  public:
    NotifyingAppStateBackend(std::shared_ptr<AppStateBackend> inner, AppStateNotifyQueue *notifyQueue)
        : inner(inner), notifyQueue(notifyQueue)
    {
    }

    std::string get(const std::string &appSlug, const std::string &key, bool &found) override
    {
        return inner->get(appSlug, key, found);
    }

    bool set(const std::string &appSlug, const std::string &key, const std::string &value) override
    {
        bool ok = inner->set(appSlug, key, value);
        if (ok) {
            std::string decoded = stripTypeTag(value);
            notifyQueue->push(appSlug, key, decoded);
            MeshEventHandler *global = getGlobalMeshEventHandler();
            if (global)
                global->handleStateChanged(appSlug, key, decoded);
        }
        return ok;
    }

    bool remove(const std::string &appSlug, const std::string &key) override
    {
        bool ok = inner->remove(appSlug, key);
        if (ok) {
            notifyQueue->push(appSlug, key, "");
            MeshEventHandler *global = getGlobalMeshEventHandler();
            if (global)
                global->handleStateChanged(appSlug, key, "");
        }
        return ok;
    }

    bool clear(const std::string &appSlug) override { return inner->clear(appSlug); }

  private:
    // Strip type tag prefix (e.g. "s:hello" -> "hello", "i:42" -> "42")
    // so notifications carry human-readable values.
    static std::string stripTypeTag(const std::string &encoded)
    {
        if (encoded.size() >= 2 && encoded[1] == ':')
            return encoded.substr(2);
        return encoded;
    }

    std::shared_ptr<AppStateBackend> inner;
    AppStateNotifyQueue *notifyQueue;
};

AppModule *appModule = nullptr;

AppModule::AppModule() : MeshModule("app")
{
    isPromiscuous = true; // See all decoded packets so wantPacket can filter by portnum
}

// Helper: create permissions bindings for a runtime
static std::map<std::string, NativeAppFunction> createPermissionsBindings(AppRuntime *runtime)
{
    std::map<std::string, NativeAppFunction> permBindings;
    permBindings["is_allowed"] = [runtime](const std::vector<AppValue> &args) -> AppValue {
        if (args.size() >= 1 && args[0].type == AppValue::String) {
            return AppValue(runtime->hasPermission(args[0].strVal));
        }
        return AppValue(false);
    };
    return permBindings;
}

// --- Multi-handler management ---

bool AppModule::startHandler(const std::string &slug)
{
    if (!appLibrary)
        return false;

    // Idempotent: if already running, return true
    if (handlers.count(slug) && handlers[slug].runtime)
        return true;

    const AppEntry *entry = appLibrary->getAppBySlug(slug);
    if (!entry || entry->manifest.getEntryFile("handler").empty())
        return false;

    AppRuntime *runtime = appLibrary->createRuntime(slug, "handler");
    if (!runtime)
        return false;

    // Permissions bindings (json is auto-registered by BerryRuntime)
    runtime->addBindings("permissions", createPermissionsBindings(runtime));

    // app_state: use mapps' auto-registered bindings (proper type encoding)
    // via a notifying wrapper that bridges state changes to OLED + TFT
    auto notifyingBackend = std::make_shared<NotifyingAppStateBackend>(getFlashAppStateBackend(), &stateNotifyQueue);
    static_cast<BerryRuntime *>(runtime)->setAppStateBackend(notifyingBackend);

    // Parse mesh-receive permissions → subscribed ports
    HandlerInfo info;
    info.runtime = runtime;
    const std::string prefix = "mesh-receive:";
    for (const auto &perm : entry->manifest.permissions) {
        if (perm.size() > prefix.size() && perm.substr(0, prefix.size()) == prefix) {
            std::string suffix = perm.substr(prefix.size());
            int port = resolveReceivePortnum(suffix);
            if (port >= 0)
                info.subscribedPorts.push_back(port);
        }
    }

    if (!runtime->start()) {
        appLibrary->stopRuntime(slug, "handler");
        return false;
    }

    runtime->call("init");
    handlers[slug] = std::move(info);
    LOG_INFO("[AppModule] Handler started for '%s' (%u port(s))", slug.c_str(),
             (unsigned)handlers[slug].subscribedPorts.size());
    return true;
}

void AppModule::stopHandler(const std::string &slug)
{
    auto it = handlers.find(slug);
    if (it == handlers.end())
        return;

    if (appLibrary)
        appLibrary->stopRuntime(slug, "handler");

    handlers.erase(it);
    LOG_INFO("[AppModule] Handler stopped for '%s'", slug.c_str());
}

bool AppModule::isHandlerRunning(const std::string &slug) const
{
    auto it = handlers.find(slug);
    return it != handlers.end() && it->second.runtime != nullptr;
}

void AppModule::startApprovedHandlers()
{
    if (!appLibrary) {
        LOG_WARN("[AppModule] No appLibrary set");
        return;
    }

    // Direct filesystem diagnostic — bypass AppLibrary to see what FSCom returns
    {
        File root = FSCom.open("/apps");
        if (!root) {
            LOG_WARN("[AppModule] FSCom.open('/apps') FAILED");
        } else if (!root.isDirectory()) {
            LOG_WARN("[AppModule] '/apps' is NOT a directory");
            root.close();
        } else {
            LOG_INFO("[AppModule] FSCom '/apps' directory listing:");
            File entry;
            while ((entry = root.openNextFile())) {
                LOG_INFO("[AppModule]   name='%s' isDir=%d size=%u", entry.name(), entry.isDirectory(), (unsigned)entry.size());
                if (entry.isDirectory()) {
                    // Try reading app.json inside
                    std::string jsonPath = std::string("/apps/") + entry.name() + "/app.json";
                    File manifest = FSCom.open(jsonPath.c_str());
                    if (manifest) {
                        LOG_INFO("[AppModule]   -> app.json found (%u bytes)", (unsigned)manifest.size());
                        manifest.close();
                    } else {
                        LOG_WARN("[AppModule]   -> app.json NOT found at '%s'", jsonPath.c_str());
                    }
                }
                entry.close();
            }
            root.close();
        }
    }

    const auto &apps = appLibrary->getApps();
    LOG_INFO("[AppModule] AppLibrary has %u app(s)", (unsigned)apps.size());
    for (int i = 0; i < (int)apps.size(); i++) {
        const auto &entry = apps[i];
        LOG_INFO("[AppModule]   [%d] '%s' slug='%s' status=%d entries=%u", i, entry.manifest.name.c_str(),
                 entry.manifest.slug.c_str(), (int)entry.status, (unsigned)entry.manifest.entries.size());
        for (const auto &ep : entry.manifest.entries) {
            LOG_INFO("[AppModule]     entry '%s' -> '%s'", ep.first.c_str(), ep.second.c_str());
        }
    }

    int count = 0;
    for (const auto &entry : apps) {
        if (entry.status == AppStatus::Ready && !entry.manifest.getEntryFile("handler").empty()) {
            if (startHandler(entry.manifest.slug))
                count++;
        }
    }

    LOG_INFO("[AppModule] Auto-started %d handler(s) at boot", count);

    // Register callback so future first-time approvals trigger handler start
    setAppReadyCallback([](const std::string &slug) {
        if (appModule)
            appModule->startHandler(slug);
    });
}

// --- BUI (OLED display) app launch ---

bool AppModule::launchApp(const std::string &slug)
{
    if (!appLibrary)
        return false;

    buiRuntime = appLibrary->createRuntime(slug, "bui");
    if (!buiRuntime)
        return false;

    // Provide display drawing functions to the app runtime
    std::map<std::string, NativeAppFunction> displayBindings;

#if HAS_SCREEN
    displayBindings["draw_string"] = [](const std::vector<AppValue> &args) -> AppValue {
        if (args.size() >= 3 && args[0].type == AppValue::Int && args[1].type == AppValue::Int &&
            args[2].type == AppValue::String) {
            app_display_draw_string(args[0].intVal, args[1].intVal, args[2].strVal.c_str());
        }
        return AppValue();
    };
    displayBindings["draw_line"] = [](const std::vector<AppValue> &args) -> AppValue {
        if (args.size() >= 4 && args[0].type == AppValue::Int && args[1].type == AppValue::Int &&
            args[2].type == AppValue::Int && args[3].type == AppValue::Int) {
            app_display_draw_line(args[0].intVal, args[1].intVal, args[2].intVal, args[3].intVal);
        }
        return AppValue();
    };
    displayBindings["draw_rect"] = [](const std::vector<AppValue> &args) -> AppValue {
        if (args.size() >= 4 && args[0].type == AppValue::Int && args[1].type == AppValue::Int &&
            args[2].type == AppValue::Int && args[3].type == AppValue::Int) {
            app_display_draw_rect(args[0].intVal, args[1].intVal, args[2].intVal, args[3].intVal);
        }
        return AppValue();
    };
    displayBindings["fill_rect"] = [](const std::vector<AppValue> &args) -> AppValue {
        if (args.size() >= 4 && args[0].type == AppValue::Int && args[1].type == AppValue::Int &&
            args[2].type == AppValue::Int && args[3].type == AppValue::Int) {
            app_display_fill_rect(args[0].intVal, args[1].intVal, args[2].intVal, args[3].intVal);
        }
        return AppValue();
    };
    displayBindings["draw_circle"] = [](const std::vector<AppValue> &args) -> AppValue {
        if (args.size() >= 3 && args[0].type == AppValue::Int && args[1].type == AppValue::Int &&
            args[2].type == AppValue::Int) {
            app_display_draw_circle(args[0].intVal, args[1].intVal, args[2].intVal);
        }
        return AppValue();
    };
    displayBindings["fill_circle"] = [](const std::vector<AppValue> &args) -> AppValue {
        if (args.size() >= 3 && args[0].type == AppValue::Int && args[1].type == AppValue::Int &&
            args[2].type == AppValue::Int) {
            app_display_fill_circle(args[0].intVal, args[1].intVal, args[2].intVal);
        }
        return AppValue();
    };
    displayBindings["set_color"] = [](const std::vector<AppValue> &args) -> AppValue {
        if (args.size() >= 1 && args[0].type == AppValue::Int) {
            app_display_set_color(args[0].intVal);
        }
        return AppValue();
    };
    displayBindings["set_font"] = [](const std::vector<AppValue> &args) -> AppValue {
        if (args.size() >= 1 && args[0].type == AppValue::String) {
            app_display_set_font(args[0].strVal.c_str());
        }
        return AppValue();
    };
    displayBindings["draw_string_wrapped"] = [](const std::vector<AppValue> &args) -> AppValue {
        if (args.size() >= 4 && args[0].type == AppValue::Int && args[1].type == AppValue::Int &&
            args[2].type == AppValue::Int && args[3].type == AppValue::String) {
            app_display_draw_string_wrapped(args[0].intVal, args[1].intVal, args[2].intVal, args[3].strVal.c_str());
        }
        return AppValue();
    };
    displayBindings["width"] = [](const std::vector<AppValue> &args) -> AppValue {
        return AppValue(app_display_width());
    };
    displayBindings["height"] = [](const std::vector<AppValue> &args) -> AppValue {
        return AppValue(app_display_height());
    };
#endif

    buiRuntime->addBindings("display", displayBindings);

    std::map<std::string, NativeAppFunction> httpBindings;
#if HAS_WIFI && defined(ARCH_ESP32)
    httpBindings["request"] = [this](const std::vector<AppValue> &args) -> AppValue {
        if (!buiRuntime->hasPermission("http-client"))
            return AppValue(false);
        if (args.size() >= 1 && args[0].type == AppValue::String) {
            return AppValue(app_http_request(args[0].strVal.c_str()));
        }
        return AppValue(false);
    };
    httpBindings["response"] = [](const std::vector<AppValue> &args) -> AppValue {
        std::string result;
        if (app_http_response(result)) {
            return AppValue(std::move(result));
        }
        return AppValue(); // nil while still pending
    };
    httpBindings["is_connected"] = [](const std::vector<AppValue> &args) -> AppValue {
        return AppValue(app_http_is_connected());
    };
#endif
    buiRuntime->addBindings("http", httpBindings);

    // Permissions introspection binding
    buiRuntime->addBindings("permissions", createPermissionsBindings(buiRuntime));

    // Menu bindings: let apps register custom hold-down menu items
    std::map<std::string, NativeAppFunction> menuBindings;
    menuBindings["set_items"] = [this](const std::vector<AppValue> &args) -> AppValue {
        appMenuItems.clear();
        for (const auto &arg : args) {
            if (arg.type == AppValue::String)
                appMenuItems.push_back(arg.strVal);
        }
        return AppValue(true);
    };
    buiRuntime->addBindings("menu", menuBindings);

    // Custom bootstrap: http.get(url) returns an HttpRequest object with .response()
    buiRuntime->setBootstrap("http",
                             "class HttpRequest\n"
                             "  def response() return _http_response() end\n"
                             "end\n"
                             "class http\n"
                             "  static def get(url)\n"
                             "    if _http_request(url) return HttpRequest() end\n"
                             "    return nil\n"
                             "  end\n"
                             "  static def is_connected() return _http_is_connected() end\n"
                             "end\n");

    if (!buiRuntime->start()) {
        appLibrary->stopRuntime(slug, "bui");
        buiRuntime = nullptr;
        return false;
    }

    buiRuntime->call("init");

    currentAppSlug = slug;

    // Start handler if not already running (may have been auto-started at boot)
    startHandler(slug);

    return true;
}

std::string AppModule::getCurrentAppName() const
{
    if (!currentAppSlug.empty() && appLibrary) {
        const AppEntry *entry = appLibrary->getAppBySlug(currentAppSlug);
        if (entry)
            return entry->manifest.name;
    }
    return "App";
}

void AppModule::callMenuHandler(int index)
{
    if (buiRuntime)
        buiRuntime->call("on_menu", {AppValue(index)});
}

void AppModule::stopCurrentApp()
{
#if HAS_WIFI && defined(ARCH_ESP32)
    app_http_cleanup();
#endif
    // Stop BUI only — handler persists in background
    if (!currentAppSlug.empty() && appLibrary) {
        appLibrary->stopRuntime(currentAppSlug, "bui");
    }
    buiRuntime = nullptr;
    currentAppSlug.clear();
    appMenuItems.clear();
    stateNotifyQueue.clear();
}

// --- Mesh event receive ---

bool AppModule::wantPacket(const meshtastic_MeshPacket *p)
{
    if (!p || p->which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return false;

    int portnum = (int)p->decoded.portnum;

    // Check all running handlers
    for (const auto &kv : handlers) {
        for (int port : kv.second.subscribedPorts) {
            if (port == portnum) {
                LOG_DEBUG("[AppModule] wantPacket: YES portnum=%d from=0x%08x (handler '%s')", portnum, p->from,
                          kv.first.c_str());
                return true;
            }
        }
    }

    return false;
}

ProcessMessage AppModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    LOG_INFO("[AppModule] handleReceived: portnum=%d from=0x%08x id=0x%x", (int)mp.decoded.portnum, mp.from, mp.id);

    // Serialize the mesh event to JSON once
    std::string eventJson = serializeMeshEvent(mp);
    if (eventJson.empty()) {
        LOG_WARN("[AppModule] handleReceived: serializeMeshEvent returned empty for portnum=%d", (int)mp.decoded.portnum);
        return ProcessMessage::CONTINUE;
    }

    LOG_DEBUG("[AppModule] handleReceived: json=%s", eventJson.c_str());

    int portnum = (int)mp.decoded.portnum;

    // Forward to all handlers subscribed to this port
    for (auto &kv : handlers) {
        bool subscribed = false;
        for (int port : kv.second.subscribedPorts) {
            if (port == portnum) {
                subscribed = true;
                break;
            }
        }
        if (subscribed && kv.second.runtime) {
            LOG_INFO("[AppModule] handleReceived: dispatching to handler '%s'", kv.first.c_str());
            AppValue result = kv.second.runtime->call("on_mesh_event", {AppValue(eventJson)});
            LOG_INFO("[AppModule] handleReceived: handler '%s' returned type=%d", kv.first.c_str(), (int)result.type);
        }
    }

    return ProcessMessage::CONTINUE;
}

#if HAS_SCREEN

std::string AppModule::approvalBannerMsg;
std::string AppModule::pendingApprovalSlug;

void AppModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (!buiRuntime)
        return;

    // Drain state-change notifications from handler → bui, filtered by current app
    AppStateNotification notification;
    while (stateNotifyQueue.pop(notification)) {
        if (notification.slug == currentAppSlug) {
            buiRuntime->call("on_state_changed", {AppValue(notification.key), AppValue(notification.value)});
        }
    }

    app_display_set(display, x, y);
    buiRuntime->call("draw");
}

bool AppModule::startApprovalFlow(const std::string &slug)
{
    if (!appLibrary)
        return false;

    int index = appLibrary->getIndexBySlug(slug);
    if (index < 0)
        return false;

    pendingApprovalSlug = slug;
    AppStatus status = appLibrary->verifyAndCheckTrust(index);

    switch (status) {
    case AppStatus::Ready:
        return launchApp(slug);

    case AppStatus::SignatureFailed:
        screen->showSimpleBanner("App signature\nverification failed.", 3000);
        return false;

    case AppStatus::Unsigned:
        screen->showSimpleBanner("App is unsigned\nand cannot be run.", 3000);
        return false;

    case AppStatus::SignatureValid:
        showDeveloperTrustPrompt(index);
        return true;

    case AppStatus::SignatureApproved:
        showPermissionsPrompt(index);
        return true;

    default:
        screen->showSimpleBanner("App cannot be launched.", 3000);
        return false;
    }
}

void AppModule::showDeveloperTrustPrompt(int appIndex)
{
    const AppEntry *entry = appLibrary->getApp(appIndex);
    if (!entry)
        return;

    std::string author = entry->manifest.author;
    if (author.size() > 20)
        author = author.substr(0, 20);

    std::string fingerprint = entry->pubKeyFingerprint;
    if (fingerprint.size() > 16)
        fingerprint = fingerprint.substr(0, 16);

    if (author.empty()) {
        approvalBannerMsg = "Do you trust\ndeveloper\n" + fingerprint + "?";
    } else {
        approvalBannerMsg = "Do you trust\n" + author + "\n(" + fingerprint + ")?";
    }

    static const char *options[] = {"No", "Yes"};
    static const int optionsEnum[] = {0, 1};

    graphics::BannerOverlayOptions bannerOptions;
    bannerOptions.message = approvalBannerMsg.c_str();
    bannerOptions.durationMs = 0; // show until user responds
    bannerOptions.optionsArrayPtr = options;
    bannerOptions.optionsCount = 2;
    bannerOptions.optionsEnumPtr = optionsEnum;
    bannerOptions.bannerCallback = [appIndex](int selected) -> void {
        if (selected != 1 || !appModule || !appLibrary)
            return; // User chose No or dismissed

        appLibrary->approveSignature(appIndex);

        const AppEntry *e = appLibrary->getApp(appIndex);
        if (!e)
            return;

        if (e->manifest.permissions.empty()) {
            // No permissions to approve — auto-approve and launch
            appLibrary->approvePermissions(appIndex);
            appModule->completeApprovalAndLaunch(pendingApprovalSlug);
        } else {
            appModule->showPermissionsPrompt(appIndex);
        }
    };
    screen->showOverlayBanner(bannerOptions);
}

void AppModule::showPermissionsPrompt(int appIndex)
{
    const AppEntry *entry = appLibrary->getApp(appIndex);
    if (!entry)
        return;

    // Build comma-separated permissions string, truncated to 180 chars
    std::string perms;
    for (size_t i = 0; i < entry->manifest.permissions.size(); i++) {
        if (i > 0)
            perms += ", ";
        perms += entry->manifest.permissions[i];
    }
    if (perms.size() > 180)
        perms = perms.substr(0, 177) + "...";

    approvalBannerMsg = entry->manifest.name + " requests:\n" + perms + "\nApprove?";

    static const char *options[] = {"No", "Yes"};
    static const int optionsEnum[] = {0, 1};

    graphics::BannerOverlayOptions bannerOptions;
    bannerOptions.message = approvalBannerMsg.c_str();
    bannerOptions.durationMs = 0;
    bannerOptions.optionsArrayPtr = options;
    bannerOptions.optionsCount = 2;
    bannerOptions.optionsEnumPtr = optionsEnum;
    bannerOptions.bannerCallback = [appIndex](int selected) -> void {
        if (selected != 1 || !appModule || !appLibrary)
            return;

        appLibrary->approvePermissions(appIndex);
        appModule->completeApprovalAndLaunch(pendingApprovalSlug);
    };
    screen->showOverlayBanner(bannerOptions);
}

void AppModule::completeApprovalAndLaunch(const std::string &slug)
{
    if (!launchApp(slug)) {
        screen->showSimpleBanner("Failed to launch app.", 3000);
    }
    pendingApprovalSlug.clear();
}

#endif // HAS_SCREEN

#endif // FSCom
