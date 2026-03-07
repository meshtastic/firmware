#pragma once

#include "mapps/AppStateBackend.h"
#include <memory>

// Flash-filesystem-backed AppStateBackend.
// Stores each app's state as a key-value file at /apps_state/<slug>.state.
class FlashAppStateBackend : public AppStateBackend
{
  public:
    std::string get(const std::string &appSlug, const std::string &key, bool &found) override;
    bool set(const std::string &appSlug, const std::string &key, const std::string &value) override;
    bool remove(const std::string &appSlug, const std::string &key) override;
    bool clear(const std::string &appSlug) override;

  private:
    static std::string statePath(const std::string &appSlug);

    // Read/write the entire state for a slug
    bool readState(const std::string &appSlug, std::string &content);
    bool writeState(const std::string &appSlug, const std::string &content);
};

// Singleton shared across all runtimes
std::shared_ptr<FlashAppStateBackend> getFlashAppStateBackend();
