#pragma once

#ifndef ARCH_PORTDUINO_WASM

#include <string>
#include <vector>

// Validates the loaded meshtasticd YAML, prints a report, and returns an exit code: 0 when
// nothing fatal was found, 1 otherwise. configFiles lists every attempted file in load order.
int runConfigCheck(const std::vector<std::string> &configFiles);

#endif // !ARCH_PORTDUINO_WASM
