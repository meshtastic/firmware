#pragma once

#ifndef ARCH_PORTDUINO_WASM

#include <string>
#include <vector>

/**
 * Validate the meshtasticd YAML configuration and print a human-readable report.
 *
 * Called from portduinoSetup() after every config file has been loaded, so that
 * portduino_config already reflects the merged result of config.yaml plus each
 * file in the config directory. configFiles lists those files in load order.
 *
 * Returns a process exit code: 0 when nothing fatal was found, 1 when at least
 * one error was reported.
 */
int runConfigCheck(const std::vector<std::string> &configFiles);

#endif // !ARCH_PORTDUINO_WASM
