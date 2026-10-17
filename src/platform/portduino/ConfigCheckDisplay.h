#pragma once

#ifndef ARCH_PORTDUINO_WASM

#include "ConfigCheckFinding.h"

#include <string>
#include <vector>

namespace configcheck
{

// loadConfig() leaves the display off, with no message, for a Panel name it does not know.
void checkDisplayPanelName(const std::string &file, int line, const std::string &name, std::vector<Finding> &findings);

// Same for Touchscreen.Module, which falls back to no touchscreen.
void checkTouchscreenModuleName(const std::string &file, int line, const std::string &name, std::vector<Finding> &findings);

// Cross-checks the merged Display, Touchscreen, Lora and Input settings for an SPI or framebuffer panel.
void checkDisplay(std::vector<Finding> &findings);

} // namespace configcheck

#endif // !ARCH_PORTDUINO_WASM
