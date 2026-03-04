#pragma once

#include "mapps/AppLibrary.h"
#include "mapps/AppRuntime.h"
#include "mapps/MeshEventHandler.h"
#include "mesh/MeshModule.h"
#include "modules/AppModule/AppMesh.h"

#include <map>
#include <string>
#include <vector>

// Per-handler tracking info
struct HandlerInfo {
    AppRuntime *runtime = nullptr;
    std::vector<int> subscribedPorts;
};

class AppModule : public MeshModule
{
  public:
    AppModule();

    // Launch app by slug (directory name)
    bool launchApp(const std::string &slug);

    // Stop the currently running app (BUI only — handler persists)
    void stopCurrentApp();

    // Check if an app is currently running
    bool isAppRunning() const { return buiRuntime != nullptr && buiRuntime->isRunning(); }

    // Get the display name of the currently running app (from manifest)
    std::string getCurrentAppName() const;

    // Custom menu items registered by the running BUI app
    const std::vector<std::string> &getAppMenuItems() const { return appMenuItems; }

    // Call the app's on_menu(index) handler
    void callMenuHandler(int index);

    // --- Multi-handler management ---

    // Start a handler runtime for the given app slug. Idempotent.
    bool startHandler(const std::string &slug);

    // Stop and remove a single handler runtime.
    void stopHandler(const std::string &slug);

    // Check if a handler is currently running for the given slug.
    bool isHandlerRunning(const std::string &slug) const;

    // Start handlers for all approved apps at boot.
    void startApprovedHandlers();

#if HAS_SCREEN
    // The app draws inside the existing apps frame (via UIRenderer::drawAppsFrame)
    // rather than adding a separate module frame.
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

    // App approval UI flow — called from menu instead of launchApp() directly
    bool startApprovalFlow(const std::string &slug);

  private:
    void showDeveloperTrustPrompt(int appIndex);
    void showPermissionsPrompt(int appIndex);
    void completeApprovalAndLaunch(const std::string &slug);

    static std::string approvalBannerMsg;
    static std::string pendingApprovalSlug;
#endif

  protected:
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  private:
    AppRuntime *buiRuntime = nullptr; // owned by AppLibrary::activeRuntimes
    std::string currentAppSlug;
    std::vector<std::string> appMenuItems;

    // Multi-handler map: slug -> HandlerInfo
    std::map<std::string, HandlerInfo> handlers;

    // OLED state notification queue (handler → bui bridge)
    AppStateNotifyQueue stateNotifyQueue;
};

extern AppModule *appModule;
